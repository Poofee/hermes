#include "hermes2d.h"

using namespace RefinementSelectors;

/** \addtogroup t_bench_layer_bdy Benchmarks/Layer-Boundary
 *  \{
 *  \brief This test makes sure that the benchmark "layer-boundary" works correctly.
 *
 *  \section s_params Parameters
 *   - INIT_REF_NUM=1
 *   - INIT_REF_NUM_BDY=3
 *   - P_INIT=1
 *   - THRESHOLD=0.3
 *   - STRATEGY=0
 *   - CAND_LIST=H2D_HP_ANISO
 *   - MESH_REGULARITY=-1
 *   - CONV_EXP=0.5
 *   - ERR_STOP=1.0
 *   - NDOF_STOP=100000
 *   - matrix_solver = SOLVER_UMFPACK
 *   - SLOPE = 60
 *
 *  \section s_res Results
 *   - DOFs: 529
 *   - Adaptivity steps: 9 
 */

const int P_INIT = 1;                             // Initial polynomial degree of all mesh elements.
const int INIT_REF_NUM = 1;                       // Number of initial mesh refinements (the original mesh is just one element).
const int INIT_REF_NUM_BDY = 3;                   // Number of initial mesh refinements towards the boundary.
const double THRESHOLD = 0.3;                     // This is a quantitative parameter of the adapt(...) function and
                                                  // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;                           // Adaptive strategy:
                                                  // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                                  //   error is processed. If more elements have similar errors, refine
                                                  //   all to keep the mesh symmetric.
                                                  // STRATEGY = 1 ... refine all elements whose error is larger
                                                  //   than THRESHOLD times maximum element error.
                                                  // STRATEGY = 2 ... refine all elements whose error is larger
                                                  //   than THRESHOLD.
                                                  // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO;          // Predefined list of element refinement candidates. Possible values are
                                                  // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                                  // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                                  // See User Documentation for details.
const int MESH_REGULARITY = -1;                   // Maximum allowed level of hanging nodes:
                                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                                  // Note that regular meshes are not supported, this is due to
                                                  // their notoriously bad performance.
const double CONV_EXP = 0.5;                      // Default value is 1.0. This parameter influences the selection of
                                                  // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 1.0;                      // Stopping criterion for adaptivity (rel. error tolerance between the
                                                  // reference mesh and coarse mesh solution in percent).
const int NDOF_STOP = 100000;                     // Adaptivity process stops when the number of degrees of freedom grows
                                                  // over this limit. This is to prevent h-adaptivity to go on forever.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// Problem parameters.
const double K = 1e2;

// Exact solution
#include "exact_solution.cpp"

// Boundary condition types.
BCType bc_types(int marker)
{
  return BC_ESSENTIAL;
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return 0;
}

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Time measurement
  TimePeriod cpu_time;
  cpu_time.tick();

  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("square.mesh", &mesh);

  // Perform initial mesh refinement.
  for (int i=0; i < INIT_REF_NUM; i++) mesh.refine_all_elements();
  mesh.refine_towards_boundary(1, INIT_REF_NUM_BDY);

  // Create an H1 space with default shapeset.
  H1Space space(&mesh, bc_types, essential_bc_values, P_INIT);

  // Initialize the weak formulation.
  WeakForm wf;
  wf.add_matrix_form(callback(bilinear_form), H2D_SYM);
  wf.add_vector_form(linear_form, linear_form_ord);

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Initialize adaptivity parameters.
  AdaptivityParamType apt(ERR_STOP, NDOF_STOP, THRESHOLD, STRATEGY, 
                          MESH_REGULARITY);

  // Adaptivity loop.
  Solution *sln = new Solution();
  Solution *ref_sln = new Solution();
  ExactSolution exact(&mesh, sol_exact);
  bool verbose = true;     // Print info during adaptivity.
  solve_linear_adapt(&space, &wf, NULL, matrix_solver, H2D_H1_NORM, sln, ref_sln,
                     Tuple<WinGeom *>(), Tuple<WinGeom *>(), &selector, &apt, verbose, &exact);

  int ndof = get_num_dofs(&space);

#define ERROR_SUCCESS                               0
#define ERROR_FAILURE                               -1
  int n_dof_allowed = 550;
  printf("n_dof_actual = %d\n", ndof);
  printf("n_dof_allowed = %d\n", n_dof_allowed);
  if (ndof <= n_dof_allowed) {
    printf("Success!\n");
    return ERROR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERROR_FAILURE;
  }
}

