#include <lgr_run.hpp>
#include <lgr_simulation.hpp>
#include <lgr_hydro.hpp>
#include <Omega_h_profile.hpp>
#include <lgr_flood.hpp>
#include <lgr_traction.hpp>

namespace lgr {

static void apply_force_conditions(Simulation& sim) {
  OMEGA_H_TIME_FUNCTION;
  apply_tractions(sim);
  apply_conditions(sim, sim.force);
}

static void apply_acceleration_conditions(Simulation& sim) {
  OMEGA_H_TIME_FUNCTION;
  apply_conditions(sim, sim.acceleration);
}

template <class Elem>
static void initialize_state(Simulation& sim) {
  OMEGA_H_TIME_FUNCTION;
  if (sim.time == 0.0) {
    apply_conditions(sim);
  } else {
    std::vector<FieldIndex> field_indices;
    for (auto& field_ptr : sim.fields.storage) {
      if ((field_ptr->remap_type != RemapType::NONE) &&
          (field_ptr->remap_type != RemapType::SHAPE)) {
        field_indices.push_back(sim.fields.find(field_ptr->long_name));
      }
    }
    sim.fields.copy_from_omega_h(sim.disc, field_indices);
  }
  initialize_configuration<Elem>(sim);
  sim.models.after_configuration();
  lump_masses<Elem>(sim);
}

template <class Elem>
static void close_state(Simulation& sim) {
  OMEGA_H_TIME_FUNCTION;
  sim.models.before_field_update();
  sim.models.at_field_update();
  sim.models.after_field_update();
  sim.models.before_material_model();
  sim.models.at_material_model();
  sim.models.after_material_model();
  compute_point_time_steps<Elem>(sim);
  compute_stress_divergence<Elem>(sim);
  apply_force_conditions(sim);
  compute_nodal_acceleration<Elem>(sim);
  apply_acceleration_conditions(sim);
  sim.models.before_secondaries();
  sim.models.at_secondaries();
  sim.models.after_secondaries();
  update_cpu_time(sim);
  sim.responses.evaluate();
}

template <class Elem>
static void run_simulation(Simulation& sim) {
  OMEGA_H_TIME_FUNCTION;
  initialize_state<Elem>(sim);
  close_state<Elem>(sim);
  while (sim.time < sim.end_time && sim.step < sim.end_step) {
    if (sim.adapter.adapt()) {
      sim.flooder.flood();
      lump_masses<Elem>(sim);
      sim.prev_time = sim.time;
      sim.prev_dt = sim.dt;
      sim.dt = 0.0;
      ++sim.step;
      close_state<Elem>(sim);
    }
    update_time(sim);
    update_position<Elem>(sim);
    update_configuration<Elem>(sim);
    sim.models.after_configuration();
    ++sim.step;
    close_state<Elem>(sim);
    correct_velocity<Elem>(sim);
    sim.models.after_correction();
  }
}

void run(Omega_h::CommPtr comm, Omega_h::InputMap& pl,
    Factories&& factories_in) {
  OMEGA_H_TIME_FUNCTION;
  Factories factories(std::move(factories_in));
  auto elem = pl.get<std::string>("element type");
  if (factories.empty()) factories = Factories(elem);
  Simulation sim(comm, std::move(factories));
#define LGR_EXPL_INST(Elem) \
  if (elem == Elem::name()) { \
    sim.set_elem<Elem>(); \
    sim.setup(pl); \
    run_simulation<Elem>(sim); \
    return; \
  }
  LGR_EXPL_INST_ELEMS
#undef LGR_EXPL_INST
  Omega_h_fail("Unknown element type \"%s\"\n", elem.c_str());
}

}
