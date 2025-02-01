#ifndef PLUMED_INTERFACE_H
#define PLUMED_INTERFACE_H

#include "plumed/wrapper/Plumed.h"
#include "module_parameter/parameter.h"

// forward declaration
namespace PLMD {
class Plumed;
}

class Plumed_interface
{
  public:
    Plumed_interface(const Parameter& param_in, UnitCell& unit_in);
    ~Plumed_interface();
    void run();

  protected:
    bool enable_plumed;
    PLMD::Plumed *p;
    int my_rank;
    int api_version;
    std::string plumed_input;
    std::string plumed_output;
};

#endif // PLUMED_INTERFACE_H