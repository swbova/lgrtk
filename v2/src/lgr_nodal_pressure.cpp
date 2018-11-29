#include <lgr_internal_energy.hpp>
#include <lgr_model.hpp>
#include <lgr_simulation.hpp>
#include <lgr_for.hpp>

namespace lgr {

template <class Elem>
struct NodalPressure : public Model<Elem> {
  using Model<Elem>::sim;
  FieldIndex nodal_pressure;
  FieldIndex nodal_pressure_rate;
  FieldIndex effective_bulk_modulus;
  FieldIndex velocity_stabilization;
  NodalPressure(Simulation& sim_in)
    :Model<Elem>(sim_in,this->sim.disc.covering_class_names())
  {  
    auto& everywhere = this->sim.disc.covering_class_names();
    nodal_pressure =
    this->sim.fields.define("p", "nodal pressure", 1, NODES, false, everywhere);
    nodal_pressure_rate =
    this->sim.fields.define("p_dot", "nodal pressure rate", 1, NODES, false, everywhere);
    effective_bulk_modulus =
    this->sim.fields.define("kappa", "effective bulk modulus", 1, ELEMS, true, everywhere);
    velocity_stabilization =
    this->sim.fields.define("tau_v", "velocity stabilizaiton", 1, ELEMS, true, everywhere);
  }

  void compute_pressure_rate(){
    OMEGA_H_TIME_FUNCTION;
  auto const nodes_to_pressure = sim.get(this->nodal_pressure);
  auto const nodes_to_pressure_rate = sim.set(this->nodal_pressure_rate);
  auto const points_to_grads = sim.get(sim.gradient);
  auto const points_to_weights = sim.get(sim.weight);
  auto const nodes_to_a = sim.get(sim.acceleration);
  auto const nodes_to_v = sim.get(sim.velocity);
  auto const points_to_rho = sim.get(sim.density);
  auto const points_to_kappa = sim.get(this->effective_bulk_modulus);
  auto const points_to_tau_v = sim.get(this->velocity_stabilization);
  auto const nodes_to_elems = sim.nodes_to_elems();
  auto functor = OMEGA_H_LAMBDA(int const node) {
    auto node_p_dot = 0.0;
    auto const begin = nodes_to_elems.a2ab[node];
    auto const end = nodes_to_elems.a2ab[node + 1];
    for (auto node_elem = begin; node_elem < end; ++node_elem) {
      auto const elem = nodes_to_elems.ab2b[node_elem];
      auto const code = nodes_to_elems.codes[node_elem];
      auto const elem_node = Omega_h::code_which_down(code);
      auto const elem_nodes = getnodes<Elem>(elems_to_nodes, elem);
      auto const v = getvecs<Elem>(nodes_to_v, elem_nodes);
      auto const nodes_a = getvecs<Elem>(nodes_to_a, elem_nodes);
      auto const p = getscals<Elem>(nodes_to_pressure, elem_nodes);
      auto const phis = Elem::basis_values();
      for (int elem_pt = 0; elem_pt < Elem::points; ++elem_pt) {
        auto const point = elem * Elem::points + elem_pt;
        auto const weight = points_to_weights[point];
	auto const grads = getgrads<Elem>(points_to_grad, point);
	auto const grad_v = grad<Elem>(grads, v);
	auto const div_v = trace(grad_v);
	auto const kappa = points_to_kappa[point];
	auto const phi = phis[elem_pt][elem_node];
	node_p_dot += phi*kappa*div_v*weight;
        auto const grad_phi = grads[elem_node];
	auto const rho = points_to_rho[point];
	auto const point_a = nodes_a*phis[elem_pt];
	auto const grad_p = grad<Elem>(grads, p);
	auto const tau_v = points_to_tau_v[point];
	auto const v_prime = -(tau_v/rho)*(rho*point_a-grad_p);
	node_p_dot += (grad_phi*(kappa*v_prime))*weight;
      }
    }
    nodes_to_pressure_rate[node] = node_p_dot;
  };
  parallel_for(this->sim.disc.counts(NODES), std::move(functor));
  }
  std::uint64_t exec_stages() override final {
    return BEFORE_MATERIAL_MODEL | BEFORE_SECONDARIES |
      AFTER_CORRECTION;
  }
  char const* name() override final { return "nodal pressure"; }
  void before_material_model() override final {
    if (sim.dt == 0.0 && (!sim.fields[nodal_pressure_rate].storage.exists())) {
      zero_nodal_pressure_rate();
    }
    compute_nodal_pressure_predictor();
  }
  void before_secondaries() override final {
    backtrack_to_midpoint_nodal_pressure();
    zero_nodal_pressure_rate();
  }
  void after_correction() override final {
    correct_nodal_pressure();
  }
  // based on the previous pressure and pressure rate, compute a predicted
  // energy using forward Euler. this predicted pressure is what material models use
  void compute_nodal_pressure_predictor() {
    OMEGA_H_TIME_FUNCTION;
    auto const nodes_to_p = this->sim.getset(this->nodal_pressure);
    auto const nodes_to_p_dot = this->sim.get(this->nodal_pressure_rate);
    auto const dt = this->sim.dt;
    auto functor = OMEGA_H_LAMBDA(int const node) {
      auto const p_dot_n = nodes_to_p_dot[node];
      auto const p_n = nodes_to_p[node];
      auto const p_np1_tilde = p_n + dt * p_dot_n;
      nodes_to_p[node] = p_np1_tilde;
    };
    parallel_for(this->sim.disc.count(NODES), std::move(functor));
  }
  void backtrack_to_midpoint_nodal_pressure() {
    OMEGA_H_TIME_FUNCTION;
    auto const nodes_to_p = this->sim.getset(this->nodal_pressure);
    auto const nodes_to_p_dot = this->sim.get(this->nodal_pressure_rate);
    auto const dt = this->sim.dt;
    auto functor = OMEGA_H_LAMBDA(int const node) {
      auto const p_dot_n = nodes_to_p_dot[node];
      auto const p_np1_tilde = nodes_to_p[node];
      auto const p_np12 = p_np1_tilde - (0.5 * dt) * p_dot_n;
      nodes_to_p[node] = p_np12;
    };
    parallel_for(this->sim.disc.count(NODES), std::move(functor));
  }
  // zero the rate before other models contribute to it
  void zero_nodal_pressure_rate() {
    OMEGA_H_TIME_FUNCTION;
    auto const nodes_to_p_dot = this->sim.set(this->nodal_pressure_rate);
    Omega_h::fill(nodes_to_p_dot, 0.0);
  }
 
  // using the previous midpoint nodal pressure and the current pressure rate,
  // compute the current pressure
  void correct_nodal_pressure() {
    OMEGA_H_TIME_FUNCTION;
    auto const nodes_to_p = this->sim.getset(this->nodal_pressure);
    auto const nodes_to_p_dot = this->sim.get(this->nodal_pressure_rate);
    auto const dt = this->sim.dt;
    auto functor = OMEGA_H_LAMBDA(int const node) {
      auto const p_dot_np1 = nodes_to_p_dot[node];
      auto const p_np12 = nodes_to_p[node];
      auto const p_np1 = p_np12 + (0.5 * dt) * p_dot_np1;
      nodes_to_p[node] = p_np1;
    };
    parallel_for(this->sim.disc.count(NODES), std::move(functor));
  }
};

template <class Elem>
ModelBase* nodal_pressure_factory(Simulation& sim, std::string const&, Omega_h::InputMap&) {
  return new NodalPressure<Elem>(sim);
}

#define LGR_EXPL_INST(Elem) \
template ModelBase* nodal_pressure_factory<Elem>(Simulation&, std::string const&, Omega_h::InputMap&);
LGR_EXPL_INST_ELEMS
#undef LGR_EXPL_INST

}
