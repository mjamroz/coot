/* ligand/maps-spherical.cc
 * 
 * Copyright 2014 by The Medical Research Council
 * Author: Paul Emsley
  * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include <gsl/gsl_sf_bessel.h>

#include <clipper/mmdb/clipper_mmdb.h>
#include <clipper/ccp4/ccp4_map_io.h>
#include <clipper/clipper-contrib.h> // sfc

#include "utils/coot-utils.hh"
#include "coot-map-utils.hh"
#include "emma.hh"
#include "peak-search.hh"


void
coot::util::emma::sfs_from_boxed_molecule(CMMDBManager *mol_orig, float border) {

   CMMDBManager *mol = new CMMDBManager;
   mol->Copy(mol_orig, MMDBFCM_All);
   PPCAtom atom_selection = 0;
   int n_selected_atoms;
   // now do selection
   int SelHnd = mol->NewSelection(); // d
   mol->SelectAtoms(SelHnd, 1, "*",
		    ANY_RES, "*",
		    ANY_RES, "*",
		    "*", "*", 
		    "*", // elements
		    "*", // alt loc.
		    SKEY_NEW);
   mol->GetSelIndex(SelHnd, atom_selection, n_selected_atoms);

   std::pair<bool, clipper::Coord_orth> centre = centre_of_molecule(mol);
   if (centre.first) { 

      // move the coordinates so that the middle of the molecule is at the origin.
      shift(mol, centre.second);
      std::pair<clipper::Coord_orth, clipper::Coord_orth> e = extents(mol, SelHnd);
	 double x_range = e.second.x() - e.first.x();
	 double y_range = e.second.y() - e.first.y();
	 double z_range = e.second.z() - e.first.z();

      double nr = clipper::Util::d2rad(90);
      clipper::Cell_descr cell_descr(x_range, y_range, z_range, nr, nr, nr);
      cell = clipper::Cell(cell_descr);
      spacegroup = clipper::Spacegroup::p1();
      reso = clipper::Resolution(2.5);

      // calculate structure factors
      hkl_info = clipper::HKL_info(spacegroup, cell, reso);
      hkl_info.generate_hkl_list();
      std::cout << "P1-sfs: num_reflections: " << hkl_info.num_reflections() << std::endl;

      // with bulking
      // clipper::SFcalc_obs_bulk<float> sfcb;
      // sfcb( fc, fo, atoms );
      // bulkfrc = sfcb.bulk_frac();
      // bulkscl = sfcb.bulk_scale();

      // debug
      //
      std::cout << "P1-sfs: cell " << cell.format() << std::endl;
      std::cout << "P1-sfs: resolution limit " << reso.limit() << std::endl;

      clipper::MMDBAtom_list atoms(atom_selection, n_selected_atoms);
      std::cout << "P1-sfs: n_selected_atoms: " << n_selected_atoms << std::endl;

      fc = clipper::HKL_data<clipper::data32::F_phi>(hkl_info, cell);

      clipper::SFcalc_aniso_fft<float> sfc;
      sfc(fc, atoms);
      std::cout << "P1-sfs: done sfs calculation " << std::endl;

      if (0) { // debug
	 clipper::HKL_info::HKL_reference_index hri;
	 for (hri = fc.first(); !hri.last(); hri.next()) {
	    std::cout << "fc " << hri.hkl().format() << " " << fc[hri].f() << " " << fc[hri].phi() << "\n";
	 }
      }
   }

   mol->DeleteSelection(SelHnd);
   delete mol;

}


void
coot::util::emma::integrate(const clipper::Xmap<float> &xmap) const {

   // sfs from map
   clipper::Resolution reso(2.5);
   clipper::HKL_info hkl_info(xmap.spacegroup(), xmap.cell(), reso);
   hkl_info.generate_hkl_list();
   clipper::HKL_data< clipper::datatypes::F_phi<double> > map_fphidata(hkl_info);
   xmap.fft_to(map_fphidata, clipper::Xmap_base::Normal);

   double x = 5.0;
   double y = gsl_sf_bessel_J0(x);

   clipper::Range<double>    fc_range = fc.invresolsq_range();
   clipper::Range<double> map_f_range = map_fphidata.invresolsq_range();
   std::cout << "fc resolution ranges " << fc_range.min() << " " << fc_range.max() << std::endl;
   std::cout << "fc resolution ranges " << 1/sqrt(fc_range.min()) << " " << 1/sqrt(fc_range.max())
	     << std::endl;

   // debug
   if (0) { 
      clipper::HKL_info::HKL_reference_index hri;
      for (hri = fc.first(); !hri.last(); hri.next()) {
	 std::complex<float> sp = fc[hri];
	 std::cout << "   fcs: " << hri.hkl().format() << " "
		   << fc[hri].f() << " "
		   << fc[hri].phi() << " " << std::abs(sp) << " " << std::arg(sp)
		   << std::endl;
      }
   }


   // N is number of points for Gauss-Legendre Integration.
   // Run over model reflection list, to make T(k), k = 1,N
   // 
   int N = 16;
   float f = 1/float(N);
   glwa_t gauss_legendre; // 1-indexed
   // store T(k) and the weights
   std::complex<double> zero_c(0,0);
   float x_rad_max = 15; // max integration radius
   float x_rad_min =  0; // min integration radius
   float x_rad_range = x_rad_max - x_rad_min;
   float x_rad_mid = (x_rad_max + x_rad_min)*0.5;
   std::vector<std::complex<double> > T(N+1, zero_c);
   for (float r_k_i=1; r_k_i<=N; r_k_i++) {
      float w_k = gauss_legendre.weight  (r_k_i);
      float r_k = gauss_legendre.abscissa(r_k_i);
      float x_gl = x_rad_range*gauss_legendre.abscissa(r_k_i)*0.5 + x_rad_mid;
      
      // scaled so that when there is a reflection at max resolution, r * r_k is 1:
      // float r_k = f * r_k_i/fc_range.max();
      clipper::HKL_info::HKL_reference_index hri;
      for (hri = fc.first(); !hri.last(); hri.next()) {
	 // r and r_k are in inversely-related spaces: when
	 // fc_range.max() is 0.0623598, the max value of r is
	 // 0.0623598 (=1/(4*4))
	 float r = hri.invresolsq();
	 float x = 2*M_PI*x_gl*r;
	 float y = gsl_sf_bessel_J0(x);
	 if (0) 
	    std::cout << r_k_i <<  " for " << hri.hkl().format() << " y: " << y << std::endl;
	 std::complex<float> t = fc[hri];
	 std::complex<double> yt(y*t.real(), y*t.imag());
	 if (0) 
	    std::cout << r_k_i <<  " for " << hri.hkl().format() << " x: " << x << " r: " << r
		      << " adding " << yt << std::endl;
	 T[r_k_i] += yt; // sum now (then multiply after looping over reflection list)
      }
      float func_r_k = 1;
      T[r_k_i] *= w_k * func_r_k * r_k *r_k;
   }

   std::cout << "---- T(k) table ----- " << std::endl;
   for (float r_k_i=1; r_k_i<=N; r_k_i++) {
      std::cout << "   rki " << r_k_i << "  val: " << T[r_k_i] << std::endl;
   }

   clipper::HKL_data< clipper::datatypes::F_phi<double> > A_data = map_fphidata; 
   
   // run over the "map fragment" reflection list
   clipper::HKL_info::HKL_reference_index hri;
   for (hri = map_fphidata.first(); !hri.last(); hri.next()) {
      std::complex<double> sum(0,0);
      for (float r_k_i=1; r_k_i<=N; r_k_i++) {
	 // scaled so that when there is a reflection at max resolution, r * r_k is 1:
	 float r_k = f * r_k_i/map_f_range.max();
	 float r = hri.invresolsq();
	 float x = 2*M_PI*r_k*r;
	 float y = gsl_sf_bessel_J0(x);
	 std::complex<double> prod(T[r_k_i].real() * y, T[r_k_i].imag() * y);
	 sum += prod;
      }
      A_data[hri] = std::complex<double>(map_fphidata[hri]) * sum;
      if (0) 
	 std::cout << "   A_data: " << hri.hkl().format()
		   << " f: " << A_data[hri].f() << " phi: " << A_data[hri].phi()
		   << " sum_abs: " << std::abs(sum) << " sum_arg: " << std::arg(sum)
		   << "\n";
   }

   // scale down A_data to "sensible" numbers:
   // for (hri = map_fphidata.first(); !hri.last(); hri.next()) A_data[hri].f() *= 0.0000001;


   // compare phases:
   if (1)
      for (hri = map_fphidata.first(); !hri.last(); hri.next())
	 std::cout << "  A_phase vs map phase " << hri.hkl().format() << " "
		   << clipper::Util::rad2d(map_fphidata[hri].phi()) << " "
		   << clipper::Util::rad2d(      A_data[hri].phi()) << std::endl;
   


   clipper::Xmap<float> A_map(xmap.spacegroup(), xmap.cell(), xmap.grid_sampling());

   // debug
   { 
      clipper::HKL_data< clipper::datatypes::F_phi<double> > A_inv_data = A_data;
      for (hri = map_fphidata.first(); !hri.last(); hri.next()) A_inv_data[hri].phi() = -A_data[hri].phi();
      clipper::Xmap<float> A_inv_map(xmap.spacegroup(), xmap.cell(), xmap.grid_sampling());
      A_inv_map.fft_from(A_inv_data);
      clipper::CCP4MAPfile mapout;
      mapout.open_write("A_inv.map");
      mapout.export_xmap(A_inv_map);
      mapout.close_write();
   }
   
   A_map.fft_from(A_data);
   peak_search ps(A_map);
   float n_sigma = 4;
   std::vector<std::pair<clipper::Coord_grid, float> > peaks = ps.get_peak_grid_points(A_map, n_sigma);
   std::cout << "==== peaks ==== " << std::endl;
   for (unsigned int ipeak=0; ipeak<peaks.size(); ipeak++)
      std::cout << ipeak << " " << peaks[ipeak].second << " " << peaks[ipeak].first.format() << std::endl;
   

   clipper::CCP4MAPfile mapout;
   mapout.open_write("A.map");
   mapout.export_xmap(A_map);
   mapout.close_write();
   
   mapout.open_write("map_fragment.map");
   mapout.export_xmap(xmap);
   mapout.close_write();
   
}

void
coot::util::emma::test() const {

   std::cout << "--------------------- start test -------------" << std::endl;
   std::cout << "--------------------- done test -------------" << std::endl;
}