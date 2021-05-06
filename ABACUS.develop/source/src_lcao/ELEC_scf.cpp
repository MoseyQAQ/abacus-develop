#include "ELEC_scf.h"
#include "src_pw/global.h"
#include "src_io/chi0_hilbert.h"
#include "src_pw/symmetry_rho.h"
#include "dftu.h"
#include "LCAO_evolve.h"
#include "ELEC_cbands_k.h"
#include "ELEC_cbands_gamma.h"
#include "ELEC_evolve.h"
#include "input_update.h"
#include "src_pw/occupy.h"
//new
#include "src_pw/H_Ewald_pw.h"

ELEC_scf::ELEC_scf(){}
ELEC_scf::~ELEC_scf(){}

int ELEC_scf::iter=0;

void ELEC_scf::scf(const int &istep)
{
	TITLE("ELEC_scf","scf");
	timer::tick("ELEC_scf","scf",'D');

	// (1) calculate ewald energy.
	// mohan update 2021-02-25
	H_Ewald_pw::compute_ewald(ucell,pw); 

	// mohan add 2012-02-08
    set_ethr(); 

	// the electron charge density should be symmetrized,
	// here is the initialization
	Symmetry_rho srho;
	for(int is=0; is<NSPIN; is++)
	{
		srho.begin(is);
	}

//	cout << scientific;
//	cout << setiosflags(ios::fixed);

	if(OUT_LEVEL=="ie" ||OUT_LEVEL=="m") 
	{
		if(COLOUR && MY_RANK==0)
		{
			printf( " [33m%-7s[0m", "ITER");	
			printf( "[33m%-15s[0m", "ETOT(Ry)");	
			if(NSPIN==2)
			{
				printf( "[33m%-10s[0m", "TMAG");	
				printf( "[33m%-10s[0m", "AMAG");	
			}
			printf( "[33m%-14s[0m", "DRHO2");	
			printf( "[33m%-15s[0m", "ETOT(eV)");	
			printf( "\e[33m%-11s\e[0m\n", "TIME(s)");	
		}
		else
		{	
			cout << " " << setw(7)<< "ITER";

			if(NSPIN==2)
			{
				cout<<setw(10)<<"TMAG";
				cout<<setw(10)<<"AMAG";
			}

			cout << setw(15) << "ETOT(eV)";
			cout << setw(15) << "EDIFF(eV)";
			cout << setw(11) << "DRHO2";
			cout << setw(11) << "TIME(s)" << endl;
		}
	}// end OUT_LEVEL


	for(iter=1; iter<=NITER; iter++)
	{
        if(CALCULATION=="scf")
        {
            ofs_running
            << "\n LCAO ALGORITHM ------------- ELEC=" << setw(4) << iter
            << "--------------------------------\n";

            ofs_warning
            << "\n LCAO ALGORITHM ------------- ELEC=" << setw(4) << iter
            << "--------------------------------\n";
        }
        else if(CALCULATION=="relax" || CALCULATION=="cell-relax")
		{
			ofs_running 
			<< "\n LCAO ALGORITHM ------------- ION=" << setw(4) << istep+1 
			<< "  ELEC=" << setw(4) << iter 
			<< "--------------------------------\n";

			ofs_warning 
			<< "\n LCAO ALGORITHM ------------- ION=" << setw(4) << istep+1 
			<< "  ELEC=" << setw(4) << iter 
			<< "--------------------------------\n";
		}
		else if(CALCULATION=="md")
		{
			ofs_running 
			<< "\n LCAO ALGORITHM ------------- MD=" << setw(4) << istep+1 
			<< "  ELEC=" << setw(4) << iter 
			<< "--------------------------------\n";

			ofs_warning 
			<< "\n LCAO ALGORITHM ------------- MD=" << setw(4) << istep+1 
			<< "  ELEC=" << setw(4) << iter 
			<< "--------------------------------\n";
		}

		//time_t time_start, time_finish;
		clock_t clock_start;

		string ufile = "CHANGE";
		Update_input UI;
		UI.init(ufile);
			
		if(INPUT.dft_plus_u) dftu.iter_dftu = iter;
		//time_start= std::time(NULL);
		clock_start = std::clock();
		conv_elec = false;//mohan add 2008-05-25

		// mohan add 2010-07-16
		// used for pulay mixing.
		if(iter==1) 
		{
			CHR.set_new_e_iteration(true);
		}
		else 
		{
			CHR.set_new_e_iteration(false);
		}

		// set converged threshold, 
		// automatically updated during self consistency, only for CG.
        this->update_ethr(iter);
        if(FINAL_SCF && iter==1)
        {
            init_mixstep_final_scf();
            //CHR.irstep=0;
            //CHR.idstep=0;
            //CHR.totstep=0;
        }

		// mohan update 2012-06-05
		en.calculate_harris(1);

		// mohan move it outside 2011-01-13
		// first need to calculate the weight according to 
		// electrons number.
		// mohan add iter > 1 on 2011-04-02
		// because the en.ekb has not value now.
		// so the smearing can not be done.
		if(iter>1)Occupy::calculate_weights();
		
		if(wf.start_wfc == "file")
		{
			if(iter==1)
			{
				cout << " WAVEFUN -> CHARGE " << endl;

				// The occupation should be read in together.
				// Occupy::calculate_weights(); //mohan add 2012-02-15
				
				// calculate the density matrix using read in wave functions
				// and the ncalculate the charge density on grid.
				LOC.sum_bands();
				// calculate the local potential(rho) again.
				// the grid integration will do in later grid integration.


				// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
				// a puzzle remains here.
				// if I don't renew potential,
				// The dr2 is very small.
				// OneElectron, Hartree and
				// Exc energy are all correct
				// except the band energy.
				//
				// solved by mohan 2010-09-10
				// there are there rho here:
				// rho1: formed by read in orbitals.
				// rho2: atomic rho, used to construct H
				// rho3: generated by after diagonalize
				// here converged because rho3 and rho1
				// are very close.
				// so be careful here, make sure
				// rho1 and rho2 are the same rho.
				// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
				pot.vr = pot.v_of_rho(CHR.rho, CHR.rho_core);
				en.delta_escf();
				if (vext == 0)	
				{
					pot.set_vr_eff();
				}
				else		
				{
					pot.set_vrs_tddft(istep);
				}
			}
		}

		// fuxiang add 2016-11-1
		// need reconstruction in near future -- mohan 2021-02-09
		// the initialization of wave functions should be moved to 
		// somewhere else
		if(tddft==1 && iter == 1)
		{
			this->WFC_init = new complex<double>**[kv.nks];
			for(int ik=0; ik<kv.nks; ik++)
			{
				this->WFC_init[ik] = new complex<double>*[NBANDS];
			}
			for(int ik=0; ik<kv.nks; ik++)
			{
				for(int ib=0; ib<NBANDS; ib++)
				{
					this->WFC_init[ik][ib] = new complex<double>[NLOCAL];
				}
			}
			if(istep>=1)
			{
				for (int ik=0; ik<kv.nks; ik++)
				{
					for (int ib=0; ib<NBANDS; ib++)
					{
						for (int i=0; i<NLOCAL; i++)
						{
							WFC_init[ik][ib][i] = LOWF.WFC_K[ik][ib][i];
						}
					}
				}
			}
			else
			{
				for (int ik=0; ik<kv.nks; ik++)
				{
					for (int ib=0; ib<NBANDS; ib++)
					{
						for (int i=0; i<NLOCAL; i++)
						{
							WFC_init[ik][ib][i] = complex<double>(0.0,0.0);
						}
					}
				}
			}
		}
		
		// calculate exact-exchange
		switch(xcf.iexch_now)						// Peize Lin add 2018-10-30
		{
			case 5:    case 6:   case 9:
				if( !exx_global.info.separate_loop )				
				{
					exx_lcao.cal_exx_elec();
				}
				break;
		}		
		
		if(INPUT.dft_plus_u) 
		{
			dftu.cal_slater_UJ(istep, iter); // Calculate U and J if Yukawa potential is used
		}

		// (1) calculate the bands.
		// mohan add 2021-02-09
		if(GAMMA_ONLY_LOCAL)
		{
			ELEC_cbands_gamma::cal_bands(istep, UHM);
		}
		else
		{
			if(tddft)
			{
				ELEC_evolve::evolve_psi(istep, UHM, this->WFC_init);
			}
			else
			{
				ELEC_cbands_k::cal_bands(istep, UHM);
			}
		}


//		for(int ib=0; ib<NBANDS; ++ib)
//		{
//			cout << ib+1 << " " << wf.ekb[0][ib] << endl;
//		}

		//-----------------------------------------------------------
		// only deal with charge density after both wavefunctions.
		// are calculated.
		//-----------------------------------------------------------
		if(GAMMA_ONLY_LOCAL && NSPIN == 2 && CURRENT_SPIN == 0) continue;


		if(conv_elec) 
		{
			timer::tick("ELEC_scf","scf",'D');
			return;
		}
		
		en.eband  = 0.0;
		en.ef     = 0.0;
		en.ef_up  = 0.0;
		en.ef_dw  = 0.0;

		// demet is included into eband.
		//if(DIAGO_TYPE!="selinv")
		{
			en.demet  = 0.0;
		}

		// (2)
		CHR.save_rho_before_sum_band();
		
		// (3) sum bands to calculate charge density
		Occupy::calculate_weights();

		if (ocp == 1)
		{
			for (int ik=0; ik<kv.nks; ik++)
			{
				for (int ib=0; ib<NBANDS; ib++)
				{
					wf.wg(ik,ib)=ocp_kb[ik*NBANDS+ib];
				}
			}
		}


		for(int ik=0; ik<kv.nks; ++ik)
		{
			en.print_band(ik);
		}

		// if selinv is used, we need this to calculate the charge
		// using density matrix.
		LOC.sum_bands();

		// add exx
		// Peize Lin add 2016-12-03
		en.set_exx();
		
		// Peize Lin add 2020.04.04
		if(Exx_Global::Hybrid_Type::HF==exx_lcao.info.hybrid_type 
			|| Exx_Global::Hybrid_Type::PBE0==exx_lcao.info.hybrid_type 
			|| Exx_Global::Hybrid_Type::HSE==exx_lcao.info.hybrid_type)
		{
			if(restart.info_load.load_H && restart.info_load.load_H_finish && !restart.info_load.restart_exx)
			{
				exx_global.info.set_xcfunc(xcf);							
				exx_lcao.cal_exx_elec();			
				restart.info_load.restart_exx = true;
			}
		}


		// if DFT+U calculation is needed, this function will calculate 
		// the local occupation number matrix and energy correction
		if(INPUT.dft_plus_u)
		{
			if(GAMMA_ONLY_LOCAL) dftu.cal_occup_m_gamma(iter);
			else dftu.cal_occup_m_k(iter);

		 	dftu.cal_energy_correction(istep);
			dftu.output();
		}

		// (4) mohan add 2010-06-24
		// using new charge density.
		en.calculate_harris(2);
		
		// (5) symmetrize the charge density
		Symmetry_rho srho;
		for(int is=0; is<NSPIN; is++)
		{
			srho.begin(is);
		}

		// (6) compute magnetization, only for spin==2
        mag.compute_magnetization();

		// resume codes!
		//-------------------------------------------------------------------------
		// this->LOWF.init_Cij( 0 ); // check the orthogonality of local orbital.
		// CHR.sum_band(); use local orbital in plane wave basis to calculate bands.
		// but must has evc first!
		//-------------------------------------------------------------------------

		// (7) calculate delta energy
		en.deband = en.delta_e();

		// (8) Mix charge density
		CHR.mix_rho(dr2,0,DRHO2,iter,conv_elec);
		
		// Peize Lin add 2020.04.04
		if(restart.info_save.save_charge)
		{
			for(int is=0; is<NSPIN; ++is)
			{
				restart.save_disk("charge", is);
			}
		}

		// (9) Calculate new potential according to new Charge Density.
	
		if(conv_elec || iter==NITER)
		{ 
			if(pot.out_potential<0) //mohan add 2011-10-10
			{
				pot.out_potential = -2;
			}
		}

		if(!conv_elec)
		{
			// option 1
			pot.vr = pot.v_of_rho(CHR.rho, CHR.rho_core);
			en.delta_escf();

			// option 2
			//------------------------------
			// mohan add 2012-06-08
			// use real E_tot functional.
			//------------------------------
			/*
			pot.vr = pot.v_of_rho(CHR.rho_save, CHR.rho);
			en.calculate_etot();
			en.print_etot(conv_elec, istep, iter, dr2, 0.0, ETHR, avg_iter,0);
			pot.vr = pot.v_of_rho(CHR.rho, CHR.rho_core);
			en.delta_escf();
			*/
		}
		else
		{
			pot.vnew = pot.v_of_rho(CHR.rho, CHR.rho_core);
			//(used later for scf correction to the forces )
			pot.vnew -= pot.vr;
			en.descf = 0.0;
		}

		//-----------------------------------
		// output charge density for tmp
		//-----------------------------------
		for(int is=0; is<NSPIN; is++)
		{
			const int precision = 3;
			
			stringstream ssc;
			ssc << global_out_dir << "tmp" << "_SPIN" << is + 1 << "_CHG";
			CHR.write_rho(CHR.rho_save[is], is, iter, ssc.str(), precision );//mohan add 2007-10-17

			stringstream ssd;

			if(GAMMA_ONLY_LOCAL)
			{
				ssd << global_out_dir << "tmp" << "_SPIN" << is + 1 << "_DM";
			}
			else
			{
				ssd << global_out_dir << "tmp" << "_SPIN" << is + 1 << "_DM_R";
			}
			LOC.write_dm( is, iter, ssd.str(), precision );

			//LiuXh modify 20200701
			/*
			stringstream ssp;
			ssp << global_out_dir << "tmp" << "_SPIN" << is + 1 << "_POT";
			pot.write_potential( is, iter, ssp.str(), pot.vr, precision );
			*/
		}

		// (10) add Vloc to Vhxc.
		if(vext == 0)	
		{
			pot.set_vr_eff();
		}
		else		
		{
			pot.set_vrs_tddft(istep);
		}
	
		//time_finish=std::time(NULL);
		double duration = (double)(clock() - clock_start) / CLOCKS_PER_SEC;
		//double duration_time = difftime(time_finish, time_start);
		//cout<<"Time_clock\t"<<"Time_time"<<endl;
		//cout<<duration<<"\t"<<duration_time<<endl;

		// (11) calculate the total energy.
		en.calculate_etot();

		// avg_iter is an useless variable in LCAO, 
		// will fix this interface in future -- mohan 2021-02-10
		int avg_iter=0;
		en.print_etot(conv_elec, istep, iter, dr2, duration, ETHR, avg_iter);
	
		en.etot_old = en.etot;

		if (conv_elec || iter==NITER)
		{
			//--------------------------------------
			// output charge density for converged,
			// 0 means don't need to consider iter,
			//--------------------------------------
			if( chi0_hilbert.epsilon)                                    // pengfei 2016-11-23
			{
				cout <<"eta = "<<chi0_hilbert.eta<<endl;
				cout <<"domega = "<<chi0_hilbert.domega<<endl;
				cout <<"nomega = "<<chi0_hilbert.nomega<<endl;
				cout <<"dim = "<<chi0_hilbert.dim<<endl;
				//cout <<"oband = "<<chi0_hilbert.oband<<endl;
				chi0_hilbert.Chi();
			}

			//quxin add for DFT+U for nscf calculation
			if(INPUT.dft_plus_u)
			{
				if(CHR.out_charge)
				{
					stringstream sst; 
					sst << global_out_dir << "onsite.dm"; 
					dftu.write_occup_m( sst.str() );		
				}
			}

			for(int is=0; is<NSPIN; is++)
			{
				const int precision = 3;

				stringstream ssc;
				ssc << global_out_dir << "SPIN" << is + 1 << "_CHG";
				CHR.write_rho(CHR.rho_save[is], is, 0, ssc.str() );//mohan add 2007-10-17

				stringstream ssd;
				if(GAMMA_ONLY_LOCAL)
				{
					ssd << global_out_dir << "SPIN" << is + 1 << "_DM";
				}
				else
				{
					ssd << global_out_dir << "SPIN" << is + 1 << "_DM_R";
				}
				LOC.write_dm( is, 0, ssd.str(), precision );

				if(pot.out_potential == 1) //LiuXh add 20200701
				{
					stringstream ssp;
					ssp << global_out_dir << "SPIN" << is + 1 << "_POT";
					pot.write_potential( is, 0, ssp.str(), pot.vr_eff, precision );
				}

				//LiuXh modify 20200701
				/*
				//fuxiang add 2017-03-15
				stringstream sse;
				sse << global_out_dir << "SPIN" << is + 1 << "_DIPOLE_ELEC";
				CHR.write_rho_dipole(CHR.rho_save, is, 0, sse.str());
				*/
			}
			
			iter_end(ofs_running);

			if(conv_elec)
			{
 				//xiaohui add "OUT_LEVEL", 2015-09-16
				if(OUT_LEVEL != "m") ofs_running << setprecision(16);
				if(OUT_LEVEL != "m") ofs_running << " EFERMI = " << en.ef * Ry_to_eV << " eV" << endl; 
				if(OUT_LEVEL=="ie")
				{
					ofs_running << " " << global_out_dir << " final etot is " << en.etot * Ry_to_eV << " eV" << endl; 
				}
			}
			else
			{
				ofs_running << " !! convergence has not been achieved @_@" << endl;
				if(OUT_LEVEL=="ie" || OUT_LEVEL=="m") //xiaohui add "m" option, 2015-09-16
				cout << " !! CONVERGENCE HAS NOT BEEN ACHIEVED !!" << endl;
			}

//			DONE(ofs_running,"ELECTRONS CONVERGED!");
			timer::tick("ELEC_scf","scf",'D');
			return;
		}
	}

	// fuxiang add, should be reconstructed in near future -- mohan note 2021-02-09
	if (tddft==1)
	{
		delete[] WFC_init;
	}

	timer::tick("ELEC_scf","scf",'D');
	return;
}


void ELEC_scf::init_mixstep_final_scf(void)
{
    TITLE("ELEC_scf","init_mixstep_final_scf");

    CHR.irstep=0;
    CHR.idstep=0;
    CHR.totstep=0;

    return;
}

