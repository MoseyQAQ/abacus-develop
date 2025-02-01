#include "plumed_interface.h"
#include "module_cell/update_cell.h"

Plumed_interface::Plumed_interface(const Parameter& param_in, UnitCell& unit_in) : mdp(param_in.mdp), ucell(unit_in)
{
    my_rank = param_in.globalv.myrank;
    plumed_input = param_in.mdp.plumed_input;
    plumed_output = param_in.mdp.plumed_output;

    if (my_rank == 0) {

        // enable plumed
        enable_plumed = true;
        p = new PLMD::Plumed();

        // check version
        api_version = p->getVersion();
        if ((api_version < 5) || (api_version > 10)) {
            std::cout << "Plumed API version is not supported!" << std::endl;
            delete p;
            exit(1);
        }

        // initialize plumed
        int natoms = unit_in.nat;
        const double timeUnits = mdp.md_dt / 1000; // fs to ps
        const double lengthUnits = 1 / ModuleBase::ANGSTROM_AU / 10; // au to nm
        const double energyUnits = 96.48530749925792 * ModuleBase::Hartree_to_eV; // Hartree to kj/mol
        p->cmd("setMDEngine", "ABACUS");
        p->cmd("setMDTimeUnits", &timeUnits);
        p->cmd("setMDLengthUnits", &lengthUnits);
        p->cmd("setMDEnergyUnits", &energyUnits);
        p->cmd("setPlumedDat", plumed_input.c_str());
        p->cmd("setLogFile", plumed_output.c_str());
        p->cmd("setNatoms", &natoms);
        p->cmd("setTimestep", &timeUnits);
        p->cmd("init");

    }
}