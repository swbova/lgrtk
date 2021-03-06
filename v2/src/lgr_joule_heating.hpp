#ifndef LGR_JOULE_HEATING_HPP 
#define LGR_JOULE_HEATING_HPP 

#include <lgr_element_types.hpp>
#include <lgr_model.hpp>
#include <string>

namespace lgr {

template <class Elem>
ModelBase* joule_heating_factory(
    Simulation& sim, std::string const&,
    Omega_h::InputMap&);

#define LGR_EXPL_INST(Elem) \
extern template ModelBase* \
joule_heating_factory<Elem>( \
    Simulation&, std::string const&, Omega_h::InputMap&);
LGR_EXPL_INST_ELEMS
#undef LGR_EXPL_INST

}

#endif
