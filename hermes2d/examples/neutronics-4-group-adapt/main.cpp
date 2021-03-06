#define H2D_REPORT_WARN
#define H2D_REPORT_INFO
#define H2D_REPORT_VERBOSE
#define H2D_REPORT_FILE "application.log"

#include "hermes2d.h"

using namespace RefinementSelectors;

// This example uses automatic adaptivity to solve a 4-group neutron diffusion equation in the reactor core.
// The eigenproblem is solved using power interations.
//
// The reactor neutronics in a general coordinate system is given by the following eigenproblem:
//
//  - \nabla \cdot D_g \nabla \phi_g + \Sigma_{Rg}\phi_g - \sum_{g' \neq g} \Sigma_s^{g'\to g} \phi_{g'} =
//  = \frac{\chi_g}{k_{eff}} \sum_{g'} \nu_{g'} \Sigma_{fg'}\phi_{g'}
//
// where 1/k_{eff} is eigenvalue and \phi_g, g = 1,...,4 are eigenvectors (neutron fluxes). The current problem
// is posed in a 3D cylindrical axisymmetric geometry, leading to a 2D problem with r-z as the independent spatial
// coordinates. Identifying r = x, z = y, the gradient in the weak form has the same components as in the
// x-y system, while all integrands are multiplied by 2\pi x (determinant of the transformation matrix).
//
// BC:
//
// homogeneous neumann on symmetry axis
// d D_g\phi_g / d n = - 0.5 \phi_g   elsewhere
//
// The eigenproblem is numerically solved using common technique known as the power method (power iterations):
//
//  1) Make an initial estimate of \phi_g and k_{eff}
//  2) For n = 1, 2,...
//         solve for \phi_g using previous k_prev
//         solve for new k_{eff}
//                                \int_{Active Core} \sum^4_{g = 1} \nu_{g} \Sigma_{fg}\phi_{g}_{new}
//               k_new =  k_prev -------------------------------------------------------------------------
//                                \int_{Active Core} \sum^4_{g = 1} \nu_{g} \Sigma_{fg}\phi_{g}_{prev}
//  3) Stop iterations when
//
//     |   k_new - k_prev  |
//     | ----------------- |  < epsilon
//     |       k_new       |
//
//  Author: Milan Hanus (University of West Bohemia, Pilsen, Czech Republic).



// Number of energy discretization intervals (groups) that also defines the number of solution components, meshes, etc.,
const int N_GROUPS = 4;
#define for_each_group(g) for (int g = 0; g < N_GROUPS; g++)

const bool SOLVE_ON_COARSE_MESH = false; // If true, coarse mesh FE problem is solved in every adaptivity step.
                                         // If false, projection of the fine mesh solution on the coarse mesh is used.
const int INIT_REF_NUM[N_GROUPS] = {     // Initial uniform mesh refinement for the individual solution components.
  1, 1, 1, 1                             
};
const int P_INIT[N_GROUPS] = {           // Initial polynomial orders for the individual solution components. 
  1, 1, 1, 1                              
};      
const double THRESHOLD = 0.3;            // This is a quantitative parameter of the adapt(...) function and
                                         // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 1;                  // Adaptive strategy:
                                         // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                         //   error is processed. If more elements have similar errors, refine
                                         //   all to keep the mesh symmetric.
                                         // STRATEGY = 1 ... refine all elements whose error is larger
                                         //   than THRESHOLD times maximum element error.
                                         // STRATEGY = 2 ... refine all elements whose error is larger
                                         //   than THRESHOLD.
                                         // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO; // Predefined list of element refinement candidates. Possible values are
                                         // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                         // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                         // See User Documentation for details.
const int MESH_REGULARITY = -1;          // Maximum allowed level of hanging nodes:
                                         // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                         // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                         // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                         // Note that regular meshes are not supported, this is due to
                                         // their notoriously bad performance.
const double CONV_EXP = 1.0;             // Default value is 1.0. This parameter influences the selection of
                                         // candidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 0.5;             // Stopping criterion for adaptivity (rel. error tolerance between the
                                         // fine mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;             // Adaptivity process stops when the number of degrees of freedom grows over
                                         // this limit. This is mainly to prevent h-adaptivity to go on forever.
const int MAX_ADAPT_NUM = 30;            // Adaptivity process stops when the number of adaptation steps grows over
                                         // this limit.

// Macros for simpler definition of tuples used in projections.
#define callback_pairs(a)      std::make_pair(callback(a)), std::make_pair(callback(a)), std::make_pair(callback(a)), std::make_pair(callback(a))

// Element markers.
const int marker_reflector = 1;
const int marker_core = 2;

// Boundary markers.
const int bc_vacuum = 1;
const int bc_sym = 2;

// Boundary condition types.
BCType bc_types(int marker)
{
  return BC_NATURAL;
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return 0;
}

// Power iteration control.

double k_eff = 1.0;         // Initial eigenvalue approximation.
double TOL_PIT_CM = 5e-5;   // Tolerance for eigenvalue convergence when solving on coarse mesh.
double TOL_PIT_RM = 1e-6;   // Tolerance for eigenvalue convergence when solving on reference mesh.

// Physical data of the problem for the given number of energy groups (N_GROUPS).
#include "physical_parameters.cpp"
// Weak forms.
#include "forms.cpp"
// Norms in the axisymmetric coordinate system.
#include "norms.cpp"

/// Fission source function.
inline void source_fn(int n, Tuple<scalar*> values, scalar* out)
{
  for (int i = 0; i < n; i++) {
    out[i] = 0.0;
    for_each_group(g)
      out[i] += nu[1][g] * Sf[1][g] * values.at(g)[i];
  }
}

// Integral over the active core.
double integrate(MeshFunction* sln, int marker)
{
  Quad2D* quad = &g_quad_2d_std;
  sln->set_quad_2d(quad);

  double integral = 0.0;
  Element* e;
  Mesh* mesh = sln->get_mesh();

  for_all_active_elements(e, mesh)
  {
    if (e->marker == marker)
    {
      update_limit_table(e->get_mode());
      sln->set_active_element(e);
      RefMap* ru = sln->get_refmap();
      int o = sln->get_fn_order() + ru->get_inv_ref_order();
      limit_order(o);
      sln->set_quad_order(o, H2D_FN_VAL);
      scalar *uval = sln->get_fn_values();
      double* x = ru->get_phys_x(o);
      double result = 0.0;
      h1_integrate_expression(x[i] * uval[i]);
      integral += result;
    }
  }

  return 2.0 * M_PI * integral;
}

/// Calculate number of negative solution values.
int get_num_of_neg(MeshFunction *sln)
{
	Quad2D* quad = &g_quad_2d_std;
  sln->set_quad_2d(quad);
  Element* e;
  Mesh* mesh = sln->get_mesh();

  int n = 0;

  for_all_active_elements(e, mesh)
  {
    update_limit_table(e->get_mode());
    sln->set_active_element(e);
    RefMap* ru = sln->get_refmap();
    int o = sln->get_fn_order() + ru->get_inv_ref_order();
    limit_order(o);
    sln->set_quad_order(o, H2D_FN_VAL);
    scalar *uval = sln->get_fn_values();
    int np = quad->get_num_points(o);

		for (int i = 0; i < np; i++)
			if (uval[i] < -1e-12)
				n++;
  }

  return n;
} 

/// \brief Power iteration. 
///
/// Starts from an initial guess stored in the argument 'solutions' and updates it by the final result after the iteration
/// has converged, also updating the global eigenvalue 'k_eff'.
///
/// \param[in]  spaces        Pointers to spaces on which the solutions are defined (one space for each energy group).
/// \param[in]  wf            Pointer to the weak form of the problem.
/// \param[in,out] slptr_solution   A set of Solution* pointers to solution components (neutron fluxes in each group). 
///                                 Initial guess for the iteration on input, converged result on output.
/// \param[in,out] mfptr_solution   The same as above, only the type of the pointers is MeshFunction*.
///                                 This is needed for the fission source filter, which accepts this type instead of Solution*.
/// \param[in]  tol           Relative difference between two successive eigenvalue approximations that stops the iteration.
/// \param[in]  matrix_solver Solver for the resulting matrix problem (one of the available types enumerated in common.h).
/// \return  number of iterations needed for convergence within the specified tolerance.
///
int power_iteration(Tuple<Space *>& spaces, WeakForm *wf,
                    Tuple<Solution *>& slptr_solution, Tuple<MeshFunction *>& mfptr_solution,
                    double tol, MatrixSolverType matrix_solver = SOLVER_UMFPACK)
{
  // Sanity checks.
  if (slptr_solution.size() != N_GROUPS) 
    error("Wrong number of power iteration solutions for the given number of energy groups.");
  if (spaces.size() != N_GROUPS) 
    error("Spaces and solutions supplied to power_iteration do not match."); 
  if (slptr_solution.size() != mfptr_solution.size()) 
    error("Number of Solutions and corresponding MeshFunctions supplied to power_iteration does not match."); 
  
  // Initialize the linear problem.
  LinearProblem lp(wf, spaces);
  int ndof = get_num_dofs(spaces);
  
  // Select matrix solver.
  Matrix* mat; Vector* rhs; CommonSolver* solver;
  init_matrix_solver(matrix_solver, ndof, mat, rhs, solver);
  
  // The following variables will store pointers to solutions obtained at each iteration and will be needed for 
  // updating the eigenvalue. We will also need to use them in the fission source filter, so their MeshFunction* 
  // version is created as well.
  Tuple<Solution*> slptr_new_solution;
  Tuple<MeshFunction*> mfptr_new_solution;
  for_each_group(g) { 
    slptr_new_solution.push_back(new Solution);
    mfptr_new_solution.push_back(slptr_new_solution.back());
  }
  
  bool eigen_done = false; int it = 0;
  do {
    // Assemble the system matrix and rhs for the first time, then just update the rhs using the updated eigenpair.
    lp.assemble(mat, rhs, it == 0 ? false : true);
        
    // Solve the matrix problem to get a new approximation of the eigenvector.
    if (!solver->solve(mat, rhs)) error ("Matrix solver failed.\n");
    
    // Convert coefficients vector into a set of Solution pointers.
    for_each_group(g) slptr_new_solution[g]->set_coeff_vector(spaces[g], rhs); 
    
    // Update fission sources.
    SimpleFilter new_source(source_fn, mfptr_new_solution);
    SimpleFilter old_source(source_fn, mfptr_solution);

    // Compute the eigenvalue for current iteration.
    double k_new = k_eff * (integrate(&new_source, marker_core) / integrate(&old_source, marker_core));

    info("      dominant eigenvalue (est): %g, rel error: %g", k_new, fabs((k_eff - k_new) / k_new));

    // Stopping criterion.
    if (fabs((k_eff - k_new) / k_new) < tol) eigen_done = true;

    // Update the final eigenvalue.
    k_eff = k_new;

    it++;
    
    // Store the new eigenvector approximation in the result.
    for_each_group(g) { slptr_solution[g]->copy(slptr_new_solution[g]); }
  }
  while (!eigen_done);
  
  // Free memory.
  for_each_group(g) delete slptr_new_solution[g];
  mat->free_data();
  rhs->free_data();
  //solver->free_data();  // FIXME: to be implemented. Default destructor is used now.
  delete solver;

  return it;
}


// Macros for simpler reporting (four group case).
#define report_num_dofs(spaces) spaces[0]->get_num_dofs(), spaces[1]->get_num_dofs(),\
                                spaces[2]->get_num_dofs(), spaces[3]->get_num_dofs(), get_num_dofs(spaces)
#define report_errors(errors) errors[0],errors[1],errors[2],errors[3]

int main(int argc, char* argv[])
{
  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Use multimesh, i.e. create one mesh for each energy group.
  
  Tuple<Mesh *> meshes;
  for_each_group(g) meshes.push_back(new Mesh());
  
  // Load the mesh for the 1st group.
  H2DReader mloader;
  mloader.load("reactor.mesh", meshes[0]);
 
  for (int g = 1; g < N_GROUPS; g++) {
    // Obtain meshes for the 2nd to 4th group by cloning the mesh loaded for the 1st group.
    meshes[g]->copy(meshes[0]);
    // Initial uniform refinements.
    for (int i = 0; i < INIT_REF_NUM[g]; i++) meshes[g]->refine_all_elements();
  }
  for (int i = 0; i < INIT_REF_NUM[0]; i++) meshes[0]->refine_all_elements();
  
  // Create pointers to solutions on coarse and fine meshes and from the latest power iteration, respectively.
  Tuple<Solution*> slptr_coarse_slns, slptr_fine_slns, slptr_pow_iter_slns;
  // We will need to pass the power iteration solutions to methods like project_global,
  // which expect MeshFunction* pointers instead of just Solution*:
  Tuple<MeshFunction*> mfptr_pow_iter_slns;
  // Initialize all the new solution variables.
  for_each_group(g) 
  {
    slptr_coarse_slns.push_back(new Solution);
    slptr_fine_slns.push_back(new Solution);
    slptr_pow_iter_slns.push_back(new Solution);   
    slptr_pow_iter_slns[g]->set_const(meshes[g], 1.0);  // Starting point for the first power iteration.
    mfptr_pow_iter_slns.push_back(slptr_pow_iter_slns[g]);
  }
  // Define a macro for easier manipulation with Solution*/MeshFunction* pairs.
  #define mkptr(a) slptr_##a, mfptr_##a
  
  // Create the approximation spaces with the default shapeset.
  Tuple<Space *> spaces;
  for_each_group(g) spaces.push_back(new H1Space(meshes[g], bc_types, essential_bc_values, P_INIT[g]));

  // Initialize the weak formulation.
  WeakForm wf(N_GROUPS);
  wf.add_matrix_form(0, 0, callback(biform_0_0), H2D_SYM);
  wf.add_matrix_form(1, 1, callback(biform_1_1), H2D_SYM);
  wf.add_matrix_form(1, 0, callback(biform_1_0));
  wf.add_matrix_form(2, 2, callback(biform_2_2), H2D_SYM);
  wf.add_matrix_form(2, 1, callback(biform_2_1));
  wf.add_matrix_form(3, 3, callback(biform_3_3), H2D_SYM);
  wf.add_matrix_form(3, 2, callback(biform_3_2));
  wf.add_vector_form(0, callback(liform_0), marker_core, mfptr_pow_iter_slns);
  wf.add_vector_form(1, callback(liform_1), marker_core, mfptr_pow_iter_slns);
  wf.add_vector_form(2, callback(liform_2), marker_core, mfptr_pow_iter_slns);
  wf.add_vector_form(3, callback(liform_3), marker_core, mfptr_pow_iter_slns);
  wf.add_matrix_form_surf(0, 0, callback(biform_surf_0_0), bc_vacuum);
  wf.add_matrix_form_surf(1, 1, callback(biform_surf_1_1), bc_vacuum);
  wf.add_matrix_form_surf(2, 2, callback(biform_surf_2_2), bc_vacuum);
  wf.add_matrix_form_surf(3, 3, callback(biform_surf_3_3), bc_vacuum);

  // Initialize and solve coarse mesh problem.
  info("Coarse mesh power iteration, %d + %d + %d + %d = %d ndof:", report_num_dofs(spaces));
  power_iteration(spaces, &wf, mkptr(pow_iter_slns), TOL_PIT_CM);
  // If SOLVE_ON_COARSE_MESH == true, we will store the results as the first coarse mesh solution;
  // otherwise, we will obtain this solution later by projecting the reference solution on the coarse mesh.
  if (SOLVE_ON_COARSE_MESH) 
    for_each_group(g) 
      slptr_coarse_slns[g]->copy(slptr_pow_iter_slns[g]);

  // Initialize views.
  /* for 1280x800 display */
  ScalarView view1("Neutron flux 1", 0, 0, 320, 400);
  ScalarView view2("Neutron flux 2", 330, 0, 320, 400);
  ScalarView view3("Neutron flux 3", 660, 0, 320, 400);
  ScalarView view4("Neutron flux 4", 990, 0, 320, 400);
  OrderView oview1("Mesh for group 1", 0, 450, 320, 500);
  OrderView oview2("Mesh for group 2", 330, 450, 320, 500);
  OrderView oview3("Mesh for group 3", 660, 450, 320, 500);
  OrderView oview4("Mesh for group 4", 990, 450, 320, 500);

  /* for adjacent 1280x800 and 1680x1050 displays
  ScalarView view1("Neutron flux 1", 0, 0, 640, 480);
  ScalarView view2("Neutron flux 2", 650, 0, 640, 480);
  ScalarView view3("Neutron flux 3", 1300, 0, 640, 480);
  ScalarView view4("Neutron flux 4", 1950, 0, 640, 480);
  OrderView oview1("Mesh for group 1", 1300, 500, 340, 500);
  OrderView oview2("Mesh for group 2", 1650, 500, 340, 500);
  OrderView oview3("Mesh for group 3", 2000, 500, 340, 500);
  OrderView oview4("Mesh for group 4", 2350, 500, 340, 500);
  */

  Tuple<ScalarView *> sviews(&view1, &view2, &view3, &view4);
  Tuple<OrderView *> oviews(&oview1, &oview2, &oview3, &oview4); 
  for_each_group(g) 
  { 
    sviews[g]->show_mesh(false);
    sviews[g]->set_3d_mode(true);
  }
  
  // DOF and CPU convergence graphs
  GnuplotGraph graph_dof("Error convergence", "NDOF", "log(error [%])");
  graph_dof.add_row("H1 error est.", "r", "-", "o");
  graph_dof.add_row("L2 error est.", "g", "-", "s");
  graph_dof.add_row("Keff error est.", "b", "-", "d");
  graph_dof.set_log_y();
  graph_dof.show_legend();
  graph_dof.show_grid();

  GnuplotGraph graph_dof_evol("Evolution of NDOF", "Adaptation step", "NDOF");
  graph_dof_evol.add_row("group 1", "r", "-", "o");
  graph_dof_evol.add_row("group 2", "g", "-", "x");
  graph_dof_evol.add_row("group 3", "b", "-", "+");
  graph_dof_evol.add_row("group 4", "m", "-", "*");
  graph_dof_evol.set_log_y();
  graph_dof_evol.set_legend_pos("bottom right");
  graph_dof_evol.show_grid();

  GnuplotGraph graph_cpu("Error convergence", "CPU time [s]", "log(error [%])");
  graph_cpu.add_row("H1 error est.", "r", "-", "o");
  graph_cpu.add_row("L2 error est.", "g", "-", "s");
  graph_cpu.add_row("Keff error est.", "b", "-", "d");
  graph_cpu.set_log_y();
  graph_cpu.show_legend();
  graph_cpu.show_grid();

  // Initialize the refinement selectors.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);
  Tuple<RefinementSelectors::Selector*> selectors;
  for_each_group(g) selectors.push_back(&selector);

  // Adaptivity loop:
  int as = 1; bool done = false;
  do {

    info("---- Adaptivity step %d:", as);

    // Construct globally refined meshes and setup reference spaces on them.
    Tuple<Space *> ref_spaces;
    Tuple<Mesh *> ref_meshes;
    for_each_group(g) 
    { 
      ref_meshes.push_back(new Mesh());
      Mesh *ref_mesh = ref_meshes.back();      
      ref_mesh->copy(spaces[g]->get_mesh());
      ref_mesh->refine_all_elements();
      
      ref_spaces.push_back(spaces[g]->dup(ref_mesh));
      int order_increase = 1;
      ref_spaces[g]->copy_orders(spaces[g], order_increase);
    }

    // For the first time, project coarse mesh solutions on fine meshes to obtain 
    // a starting point for the fine mesh power iteration.
    if (as == 1) {
      info("Projecting initial coarse mesh solutions on fine meshes.");
      project_global(spaces, 
                     matrix_forms_tuple_t(callback_pairs(projection_biform)), 
                     vector_forms_tuple_t(callback_pairs(projection_liform)),
                     mfptr_pow_iter_slns, slptr_pow_iter_slns);
    }

    // Solve the fine mesh problem.
    info("Fine mesh power iteration, %d + %d + %d + %d = %d ndof:", report_num_dofs(ref_spaces));
    power_iteration(ref_spaces, &wf, mkptr(pow_iter_slns), TOL_PIT_RM);
    
    // Store the results.
    for_each_group(g) slptr_fine_slns[g]->copy(slptr_pow_iter_slns[g]);

    // Either solve on coarse mesh or project the fine mesh solution on the coarse mesh.
    if (SOLVE_ON_COARSE_MESH) {
      if (as > 1) {
        info("Coarse mesh power iteration, %d + %d + %d + %d = %d ndof:", report_num_dofs(spaces));
        power_iteration(spaces, &wf, mkptr(pow_iter_slns), TOL_PIT_CM);
        // Store the results.
        for_each_group(g) slptr_coarse_slns[g]->copy(slptr_pow_iter_slns[g]);
      }
    }
    else {
      info("Projecting fine mesh solutions on coarse meshes.");
      project_global(spaces, 
                     matrix_forms_tuple_t(callback_pairs(projection_biform)), 
                     vector_forms_tuple_t(callback_pairs(projection_liform)),
                     mfptr_pow_iter_slns, slptr_coarse_slns);
    }

    // Time measurement.
    cpu_time.tick();

    // View the coarse mesh solution and meshes.
    for_each_group(g) { 
      sviews[g]->show(slptr_coarse_slns[g]); 
      oviews[g]->show(spaces[g]);
    }

    // Skip visualization time.
    cpu_time.tick(HERMES_SKIP);

    // Report the number of negative eigenfunction values.
    info("Num. of negative values: %d, %d, %d, %d",
         get_num_of_neg(slptr_coarse_slns[0]), get_num_of_neg(slptr_coarse_slns[1]),
         get_num_of_neg(slptr_coarse_slns[2]), get_num_of_neg(slptr_coarse_slns[3]));

    // Calculate element errors and total error estimate.
    Adapt hp(spaces);
    hp.set_error_form(0, 0, callback(biform_0_0));
    hp.set_error_form(1, 1, callback(biform_1_1));
    hp.set_error_form(1, 0, callback(biform_1_0));
    hp.set_error_form(2, 2, callback(biform_2_2));
    hp.set_error_form(2, 1, callback(biform_2_1));
    hp.set_error_form(3, 3, callback(biform_3_3));
    hp.set_error_form(3, 2, callback(biform_3_2));

    // Calculate element errors and error estimate for adaptivity.
    info("Calculating error.");
    hp.set_solutions(slptr_coarse_slns, slptr_fine_slns);

    double energy_err_est = hp.calc_elem_errors(H2D_TOTAL_ERROR_REL | H2D_ELEMENT_ERROR_REL) * 100;
    double h1_err_est = error_total(error_fn_h1_axisym, norm_fn_h1_axisym, slptr_coarse_slns, slptr_fine_slns);
    double l2_err_est = error_total(error_fn_l2_axisym, norm_fn_l2_axisym, slptr_coarse_slns, slptr_fine_slns);

    // Time measurement.
    cpu_time.tick();
    double cta = cpu_time.accumulated();

    // Calculate H1 and L2 error estimates.
    std::vector<double> h1_errors, l2_errors;
    for_each_group(g) 
    {
      l2_errors.push_back( 100*l2_error_axisym(slptr_coarse_slns[g], slptr_fine_slns[g]) );
      h1_errors.push_back( 100*h1_error_axisym(slptr_coarse_slns[g], slptr_fine_slns[g]) );
    }
    
    // Report results.
    info("ndof_coarse: %d + %d + %d + %d = %d", report_num_dofs(spaces));

    // Millipercent eigenvalue error w.r.t. the reference value (see physical_parameters.cpp). 
  	double keff_err = 1e5*fabs(k_eff - REF_K_EFF)/REF_K_EFF;

  	info("per-group err_est_coarse (H1): %g%%, %g%%, %g%%, %g%%", report_errors(h1_errors));
  	info("per-group err_est_coarse (L2): %g%%, %g%%, %g%%, %g%%", report_errors(l2_errors));
  	info("total err_est_coarse (energy): %g%%", energy_err_est);
  	info("total err_est_coarse (H1): %g%%", h1_err_est);
  	info("total err_est_coarse (L2): %g%%", l2_err_est);
  	info("k_eff err: %g milli-percent", keff_err);

    // Add entry to DOF convergence graph.
    int ndof_coarse = get_num_dofs(spaces);
    graph_dof.add_values(0, ndof_coarse, h1_err_est);
    graph_dof.add_values(1, ndof_coarse, l2_err_est);
    graph_dof.add_values(2, ndof_coarse, keff_err);

    // Add entry to CPU convergence graph.
    graph_cpu.add_values(0, cta, h1_err_est);
    graph_cpu.add_values(1, cta, l2_err_est);
    graph_cpu.add_values(2, cta, keff_err);

    for_each_group(g)
      graph_dof_evol.add_values(g, as, spaces[g]->get_num_dofs());

    cpu_time.tick(HERMES_SKIP);

    // If err_est too large, adapt the mesh.
    if (energy_err_est < ERR_STOP) break;
    else {
      info("Adapting the coarse mesh.");
      done = hp.adapt(selectors, THRESHOLD, STRATEGY, MESH_REGULARITY);
      if (get_num_dofs(spaces) >= NDOF_STOP) done = true;
    }

    // Free reference meshes and spaces.
    for_each_group(g) 
    {
      delete ref_spaces[g];
      delete ref_meshes[g];
    }

    as++;
    if (as >= MAX_ADAPT_NUM) done = true;
  }
  while(done == false);
  verbose("Total running time: %g s", cpu_time.accumulated());
  
  for_each_group(g) 
  {
    delete spaces[g]; delete meshes[g];
    delete slptr_coarse_slns[g], delete slptr_fine_slns[g]; delete slptr_pow_iter_slns[g];
  }

  graph_dof.save("conv_dof.gp");
  graph_cpu.save("conv_cpu.gp");
  graph_dof_evol.save("dof_evol.gp");

  // Wait for all views to be closed.
  View::wait();
  return 0;
}
