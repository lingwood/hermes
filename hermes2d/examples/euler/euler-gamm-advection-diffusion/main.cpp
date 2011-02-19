#define HERMES_REPORT_INFO
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

//  This example solves the compressible Euler equations using a basic
//  piecewise-constant finite volume method.
//
//  Equations: Compressible Euler equations, perfect gas state equation.
//
//  Domain: GAMM channel, see mesh file GAMM-channel.mesh
//
//  BC: Normal velocity component is zero on solid walls.
//      Subsonic state prescribed on inlet and outlet.
//
//  IC: Constant subsonic state identical to inlet. 
//
//  The following parameters can be changed:

// Calculation of approximation of time derivative (and its output).
// Setting this option to false saves the computation time.
const bool CALC_TIME_DER = false;

const Ord2 P_INIT_FLOW = Ord2(0,0);               // Polynomial degree for the Euler equations (for the flow).
const Ord2 P_INIT_CONCENTRATION = Ord2(1,1);      // Polynomial degree for the concentration.
double CFL = 0.8;                                 // CFL value.
double TAU = 1E-4;                                // Time step.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_AMESOS, SOLVER_AZTECOO, SOLVER_MUMPS,
                                                  // SOLVER_PETSC, SOLVER_SUPERLU, SOLVER_UMFPACK.

const unsigned int INIT_REF_NUM_FLOW = 2;         // Number of initial uniform mesh refinements of the mesh for the flow.
const unsigned int INIT_REF_NUM_CONCENTRATION = 4;// Number of initial uniform mesh refinements of the mesh for the concentration.


// Equation parameters.
double P_EXT = 2.5;         // Exterior pressure (dimensionless).
double RHO_EXT = 1.0;       // Inlet density (dimensionless).   
double V1_EXT = 1.25;       // Inlet x-velocity (dimensionless).
double V2_EXT = 0.0;        // Inlet y-velocity (dimensionless).
double KAPPA = 1.4;         // Kappa.

// Diffusion parameter (diffusivity).
double EPSILON = 0.01;

// Boundary (initial) value of the concentration.
double CONCENTRATION_EXT = 1.0;

// Time is zero at the beginning.
double t = 0;

// Boundary markers.
const int BDY_SOLID_WALL = 1;
const int BDY_INLET_OUTLET = 2;

// Numerical flux.
// For numerical fluxes, please see hermes2d/src/numerical_flux.h
NumericalFlux num_flux(KAPPA);

// Inlet/outlet boundary conditions.
double bc_density(double y)
{
  return RHO_EXT;
}

// Density * velocity in the x coordinate boundary condition.
double bc_density_vel_x(double y)
{
  return RHO_EXT * V1_EXT;
}

// Density * velocity in the y coordinate boundary condition.
double bc_density_vel_y(double y)
{
  return V2_EXT;
}

// Calculation of the pressure on the boundary.
double bc_pressure(double y)
{
  return P_EXT;
}

// Energy boundary condition.
double bc_energy(double y)
{
  double rho = bc_density(y);
  double rho_v_x = bc_density_vel_x(y);
  double rho_v_y = bc_density_vel_y(y);
  double pressure = bc_pressure(y);
  return pressure/(num_flux.kappa - 1.) + (rho_v_x*rho_v_x+rho_v_y*rho_v_y) / 2*rho;
}

// Calculates energy from other quantities.
// FIXME: this should be in the src/ directory, not here.
double calc_energy(double rho, double rho_v_x, double rho_v_y, double pressure)
{
  return pressure/(num_flux.kappa - 1.) + (rho_v_x*rho_v_x+rho_v_y*rho_v_y) / 2*rho;
}

// Calculates pressure from other quantities.
// FIXME: this should be in the src/ directory, not here.
double calc_pressure(double rho, double rho_v_x, double rho_v_y, double energy)
{
  return (num_flux.kappa - 1.) * (energy - (rho_v_x*rho_v_x + rho_v_y*rho_v_y) / (2*rho));
}

// Calculates speed of sound.
// FIXME: this should be in the src/ directory, not here.
double calc_sound_speed(double rho, double rho_v_x, double rho_v_y, double energy)
{
  return std::sqrt(num_flux.kappa * calc_pressure(rho, rho_v_x, rho_v_y, energy) / rho);
}

// Constant initial state (matching the supersonic inlet state).
double ic_density(double x, double y, scalar& dx, scalar& dy)
{
  return RHO_EXT;
}
double ic_density_vel_x(double x, double y, scalar& dx, scalar& dy)
{
  return RHO_EXT * V1_EXT;
}
double ic_density_vel_y(double x, double y, scalar& dx, scalar& dy)
{
  return RHO_EXT * V2_EXT;
}
double ic_energy(double x, double y, scalar& dx, scalar& dy)
{
  return calc_energy(RHO_EXT, RHO_EXT*V1_EXT, RHO_EXT*V2_EXT, P_EXT);
}

double ic_concentration(double x, double y, scalar& dx, scalar& dy)
{
  return CONCENTRATION_EXT;
}

// Weak forms.
#include "forms.cpp"

// Filters.
#include "filters.cpp"

// Filter for entropy which uses the constants defined above.
static void calc_entropy_estimate_func(int n, Hermes::vector<scalar*> scalars, scalar* result)
{
  for (int i = 0; i < n; i++)
    result[i] = std::log((calc_pressure(scalars.at(0)[i], scalars.at(1)[i], scalars.at(2)[i], scalars.at(3)[i]) / P_EXT)
    / pow((scalars.at(0)[i] / RHO_EXT), KAPPA));
};

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh basemesh;
  H2DReader mloader;
  mloader.load("GAMM-channel.mesh", &basemesh);

  // Initialize the meshes.
  Mesh mesh_flow, mesh_concentration;
  mesh_flow.copy(&basemesh);
  mesh_concentration.copy(&basemesh);

  for(unsigned int i = 0; i < INIT_REF_NUM_CONCENTRATION; i++)
    mesh_concentration.refine_all_elements();

  for(unsigned int i = 0; i < INIT_REF_NUM_FLOW; i++)
    mesh_flow.refine_all_elements();

  // Enter boundary markers.  
  BCTypes bc_types_euler;
  bc_types_euler.add_bc_neumann(Hermes::vector<int>(BDY_SOLID_WALL, BDY_INLET_OUTLET));

  BCTypes bc_types_concentration;
  bc_types_concentration.add_bc_neumann(Hermes::vector<int>(BDY_INLET_OUTLET));
  bc_types_concentration.add_bc_dirichlet(Hermes::vector<int>(BDY_SOLID_WALL));

  BCValues bc_values_concentration;
  bc_values_concentration.add_const(Hermes::vector<int>(BDY_SOLID_WALL), CONCENTRATION_EXT);

  // Create L2 spaces with default shapesets.
  // Spaces for the flow.
  L2Space space_rho(&mesh_flow, &bc_types_euler, P_INIT_FLOW);
  L2Space space_rho_v_x(&mesh_flow, &bc_types_euler, P_INIT_FLOW);
  L2Space space_rho_v_y(&mesh_flow, &bc_types_euler, P_INIT_FLOW);
  L2Space space_e(&mesh_flow, &bc_types_euler, P_INIT_FLOW);
  // Space for concentration.
  H1Space space_c(&mesh_concentration, &bc_types_concentration, &bc_values_concentration, P_INIT_CONCENTRATION);

  // Initialize solutions, set initial conditions.
  Solution sln_rho, sln_rho_v_x, sln_rho_v_y, sln_e, sln_c, prev_rho, prev_rho_v_x, prev_rho_v_y, prev_e, prev_c;
  sln_rho.set_exact(&mesh_flow, ic_density);
  sln_rho_v_x.set_exact(&mesh_flow, ic_density_vel_x);
  sln_rho_v_y.set_exact(&mesh_flow, ic_density_vel_y);
  sln_e.set_exact(&mesh_flow, ic_energy);
  sln_c.set_exact(&mesh_concentration, ic_concentration);
  prev_rho.set_exact(&mesh_flow, ic_density);
  prev_rho_v_x.set_exact(&mesh_flow, ic_density_vel_x);
  prev_rho_v_y.set_exact(&mesh_flow, ic_density_vel_y);
  prev_e.set_exact(&mesh_flow, ic_energy);
  prev_c.set_exact(&mesh_concentration, ic_concentration);

  // Initialize weak formulation.
  WeakForm wf(5);

  // Bilinear forms coming from time discretization by explicit Euler's method.
  wf.add_matrix_form(0, 0, callback(bilinear_form_time));
  wf.add_matrix_form(1, 1, callback(bilinear_form_time));
  wf.add_matrix_form(2, 2, callback(bilinear_form_time));
  wf.add_matrix_form(3, 3, callback(bilinear_form_time));
  wf.add_matrix_form(4, 4, callback(bilinear_form_time));

  // Volumetric linear forms.
  // Linear forms coming from the linearization by taking the Eulerian fluxes' Jacobian matrices 
  // from the previous time step.
  // Unnecessary for FVM.
  if(P_INIT_FLOW.order_h > 0 || P_INIT_FLOW.order_v > 0) {
    // First flux.
    wf.add_vector_form(0, callback(linear_form_0_1), HERMES_ANY, Hermes::vector<MeshFunction*>(&prev_rho_v_x));
    
    wf.add_vector_form(1, callback(linear_form_1_0_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_1_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_2_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_3_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(2, callback(linear_form_2_0_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(2, callback(linear_form_2_1_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(2, callback(linear_form_2_2_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(2, callback(linear_form_2_3_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_0_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_1_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_2_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_3_first_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));

    // Second flux.
    wf.add_vector_form(0, callback(linear_form_0_2), HERMES_ANY, Hermes::vector<MeshFunction*>(&prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_0_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_1_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_2_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(1, callback(linear_form_1_3_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(2, callback(linear_form_2_0_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(2, callback(linear_form_2_1_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(2, callback(linear_form_2_2_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y));
    wf.add_vector_form(2, callback(linear_form_2_3_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_0_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_1_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_2_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
    wf.add_vector_form(3, callback(linear_form_3_3_second_flux), HERMES_ANY, 
                       Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  }

  // Volumetric linear forms coming from the time discretization.
  wf.add_vector_form(0, linear_form, linear_form_order, HERMES_ANY, &prev_rho);
  wf.add_vector_form(1, linear_form, linear_form_order, HERMES_ANY, &prev_rho_v_x);
  wf.add_vector_form(2, linear_form, linear_form_order, HERMES_ANY, &prev_rho_v_y);
  wf.add_vector_form(3, linear_form, linear_form_order, HERMES_ANY, &prev_e);
  wf.add_vector_form(4, linear_form, linear_form_order, HERMES_ANY, &prev_c);

  // Surface linear forms - inner edges coming from the DG formulation.
  wf.add_vector_form_surf(0, linear_form_interface_0, linear_form_order, H2D_DG_INNER_EDGE, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(1, linear_form_interface_1, linear_form_order, H2D_DG_INNER_EDGE, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(2, linear_form_interface_2, linear_form_order, H2D_DG_INNER_EDGE, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(3, linear_form_interface_3, linear_form_order, H2D_DG_INNER_EDGE, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));


  // Surface linear forms - inlet / outlet edges.
  wf.add_vector_form_surf(0, bdy_flux_inlet_outlet_comp_0, linear_form_order, BDY_INLET_OUTLET, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(1, bdy_flux_inlet_outlet_comp_1, linear_form_order, BDY_INLET_OUTLET, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(2, bdy_flux_inlet_outlet_comp_2, linear_form_order, BDY_INLET_OUTLET, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(3, bdy_flux_inlet_outlet_comp_3, linear_form_order, BDY_INLET_OUTLET, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  
  // Surface linear forms - Solid wall edges.
  wf.add_vector_form_surf(0, bdy_flux_solid_wall_comp_0, linear_form_order, BDY_SOLID_WALL, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(1, bdy_flux_solid_wall_comp_1, linear_form_order, BDY_SOLID_WALL, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(2, bdy_flux_solid_wall_comp_2, linear_form_order, BDY_SOLID_WALL, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));
  wf.add_vector_form_surf(3, bdy_flux_solid_wall_comp_3, linear_form_order, BDY_SOLID_WALL, 
                          Hermes::vector<MeshFunction*>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e));

  // Forms for concentration.
  wf.add_vector_form(4, callback(linear_form_concentration_grad_grad), HERMES_ANY, &prev_c);
  
  wf.add_vector_form(4, callback(linear_form_concentration_convective), HERMES_ANY, 
                          Hermes::vector<MeshFunction*>(&prev_c, &prev_rho, &prev_rho_v_x, &prev_rho_v_y));

  wf.add_vector_form_surf(4, callback(linear_form_concentration_inlet_outlet), BDY_INLET_OUTLET, 
                          Hermes::vector<MeshFunction*>(&prev_c, &prev_rho, &prev_rho_v_x, &prev_rho_v_y));

  wf.add_vector_form_surf(4, callback(linear_form_concentration_inner_edges), H2D_DG_INNER_EDGE, 
                          Hermes::vector<MeshFunction*>(&prev_c, &prev_rho, &prev_rho_v_x, &prev_rho_v_y));

  // Initialize the FE problem.
  bool is_linear = true;
  
  DiscreteProblem dp(&wf, Hermes::vector<Space*>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e, &space_c), is_linear);
  
  // Filters for visualization of pressure and the two components of velocity.
  /*
  SimpleFilter pressure(calc_pressure_func, Hermes::vector<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
  SimpleFilter u(calc_u_func, Hermes::vector<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
  SimpleFilter w(calc_w_func, Hermes::vector<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
  SimpleFilter Mach_number(calc_Mach_func, Hermes::vector<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));
  SimpleFilter entropy_estimate(calc_entropy_estimate_func, Hermes::vector<MeshFunction*>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e));

  ScalarView pressure_view("Pressure", new WinGeom(0, 0, 600, 300));
  ScalarView Mach_number_view("Mach number", new WinGeom(700, 0, 600, 300));
  ScalarView entropy_production_view("Entropy estimate", new WinGeom(0, 400, 600, 300));
  VectorView vview("Velocity", new WinGeom(700, 400, 600, 300));
  */

  ScalarView s1("w0", new WinGeom(0, 0, 600, 300));
  ScalarView s2("w1", new WinGeom(700, 0, 600, 300));
  ScalarView s3("w2", new WinGeom(0, 400, 600, 300));
  ScalarView s4("w3", new WinGeom(700, 400, 600, 300));
  ScalarView s5("Concentration", new WinGeom(350, 200, 600, 300));
  
  // Iteration number.
  int iteration = 0;
  
  // Set up the solver, matrix, and rhs according to the solver selection.
  SparseMatrix* matrix = create_matrix(matrix_solver);
  Vector* rhs = create_vector(matrix_solver);
  Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);

  // Output of the approximate time derivative.
  std::ofstream time_der_out("time_der");

  for(t = 0.0; t < 10; t += TAU) {
    info("---- Time step %d, time %3.5f.", iteration, t);

    iteration++;

    bool rhs_only = (iteration == 1 ? false : true);
    // Assemble stiffness matrix and rhs or just rhs.
    if (rhs_only == false) info("Assembling the stiffness matrix and right-hand side vector.");
    else info("Assembling the right-hand side vector (only).");
    dp.assemble(matrix, rhs, rhs_only);

        
    // Solve the matrix problem.
    info("Solving the matrix problem.");
    if(solver->solve())
      Solution::vector_to_solutions(solver->get_solution(), Hermes::vector<Space *>(&space_rho, &space_rho_v_x, 
      &space_rho_v_y, &space_e, &space_c), Hermes::vector<Solution *>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e, &sln_c));
    else
    error ("Matrix solver failed.\n");

    // Approximate the time derivative of the solution.
    if(CALC_TIME_DER) {
      Adapt *adapt_for_time_der_calc = new Adapt(Hermes::vector<Space *>(&space_rho, &space_rho_v_x, &space_rho_v_y, &space_e));
      bool solutions_for_adapt = false;
      double difference = iteration == 1 ? 0 : 
        adapt_for_time_der_calc->calc_err_est(Hermes::vector<Solution *>(&prev_rho, &prev_rho_v_x, &prev_rho_v_y, &prev_e), 
					      Hermes::vector<Solution *>(&sln_rho, &sln_rho_v_x, &sln_rho_v_y, &sln_e), 
                                              (Hermes::vector<double>*) NULL, solutions_for_adapt, 
                                              HERMES_TOTAL_ERROR_ABS | HERMES_ELEMENT_ERROR_ABS) / TAU;
      delete adapt_for_time_der_calc;

      // Info about the approximate time derivative.
      if(iteration > 1) {
        info("Approximate the norm time derivative : %g.", difference);
        time_der_out << iteration << '\t' << difference << std::endl;
      }
    }

    // Determine the time step according to the CFL condition.
    // Only mean values on an element of each solution component are taken into account.
    /*
    double *solution_vector = solver->get_solution();
    double min_condition = 0;
    Element *e;
    for (int _id = 0, _max = mesh.get_max_element_id(); _id < _max; _id++) \
          if (((e) = mesh.get_element_fast(_id))->used) \
            if ((e)->active) {
              AsmList al;
              space_rho.get_element_assembly_list(e, &al);
              double rho = solution_vector[al.dof[0]];
              space_rho_v_x.get_element_assembly_list(e, &al);
              double v1 = solution_vector[al.dof[0]] / rho;
              space_rho_v_y.get_element_assembly_list(e, &al);
              double v2 = solution_vector[al.dof[0]] / rho;
              space_e.get_element_assembly_list(e, &al);
              double energy = solution_vector[al.dof[0]];
      
              double condition = e->get_area() / (std::sqrt(v1*v1 + v2*v2) + calc_sound_speed(rho, rho*v1, rho*v2, energy));
      
              if(condition < min_condition || min_condition == 0.)
                min_condition = condition;
            }
    if(TAU > min_condition)
      TAU = min_condition;
    if(TAU < min_condition * 0.9)
      TAU = min_condition;
      */

    // Copy the solutions into the previous time level ones.
    prev_rho.copy(&sln_rho);
    prev_rho_v_x.copy(&sln_rho_v_x);
    prev_rho_v_y.copy(&sln_rho_v_y);
    prev_e.copy(&sln_e);
    prev_c.copy(&sln_c);

    // Visualization.
    /*
    pressure.reinit();
    u.reinit();
    w.reinit();
    Mach_number.reinit();
    entropy_estimate.reinit();
    pressure_view.show(&pressure);
    entropy_production_view.show(&entropy_estimate);
    Mach_number_view.show(&Mach_number);
    vview.show(&u, &w);
    */
    s1.show(&prev_rho);
    s2.show(&prev_rho_v_x);
    s3.show(&prev_rho_v_y);
    s4.show(&prev_e);
    s5.show(&prev_c);
  }
  
  time_der_out.close();
  return 0;
}