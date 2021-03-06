#include "hermes2d.h"

// This test makes sure that example 04-bc-dirichlet works correctly.
// CAUTION: This test will fail when any changes to the shapeset
// are made, but it is easy to fix (see below).

double CONST_F = -4.0;                            // constant right-hand side.
int P_INIT = 2;                                   // Initial polynomial degree in all elements.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// boundary condition type (essential = Dirichlet).
BCType bc_types(int marker)
{
  return BC_ESSENTIAL;
}

// function values for essential(Dirichlet) boundary markers.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return (-CONST_F/4.0)*(x*x + y*y);
}

// return the value \int \nabla u . \nabla v dx .
template<typename Real, typename Scalar>
Scalar bilinear_form(int n, double *wt, Func<Scalar> *u_ext[], 
Func<Real> *u, Func<Real> *v, Geom<Real> *e, ExtData<Scalar> *ext)
{
  return int_grad_u_grad_v<Real, Scalar>(n, wt, u, v);
}

// return the value \int v dx .
template<typename Real, typename Scalar>
Scalar linear_form(int n, double *wt, Func<Scalar> *u_ext[], 
Func<Real> *v, Geom<Real> *e, ExtData<Scalar> *ext)
{
  return CONST_F*int_v<Real, Scalar>(n, wt, v);
}

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("domain.mesh", &mesh);
  mesh.refine_all_elements();

  // Create an H1 space.
  H1Space* space = new H1Space(&mesh, bc_types, essential_bc_values, P_INIT);

  // Initialize the weak formulation.
  WeakForm wf;
  wf.add_matrix_form(callback(bilinear_form));
  wf.add_vector_form(callback(linear_form));

  // Testing n_dof and correctness of solution vector
  // for p_init = 1, 2, ..., 10
  int success = 1;
  Solution sln;
  for (int p_init = 1; p_init <= 10; p_init++) {

    printf("********* p_init = %d *********\n", p_init);
    space->set_uniform_order(p_init);

    // Initialize the linear problem.
    LinearProblem lp(&wf, space);

    // Select matrix solver.
    Matrix* mat; Vector* rhs; CommonSolver* solver;
    init_matrix_solver(matrix_solver, get_num_dofs(space), mat, rhs, solver);

    // Assemble stiffness matrix and rhs.
    bool rhsonly = false;
    lp.assemble(mat, rhs, rhsonly);

    // Solve the matrix problem.
    if (!solver->solve(mat, rhs)) error ("Matrix solver failed.\n");

    int ndof = get_num_dofs(space);
    printf("ndof = %d\n", ndof);
    double sum = 0;
    for (int i=0; i < ndof; i++) sum += rhs->get(i);
    printf("coefficient sum = %g\n", sum);

    // Actual test. The values of 'sum' depend on the
    // current shapeset. If you change the shapeset,
    // you need to correct these numbers.
    if (p_init == 1 && fabs(sum - 1.7251) > 1e-3) success = 0;
    if (p_init == 2 && fabs(sum - 3.79195) > 1e-3) success = 0;
    if (p_init == 3 && fabs(sum - 3.80206) > 1e-3) success = 0;
    if (p_init == 4 && fabs(sum - 3.80156) > 1e-3) success = 0;
    if (p_init == 5 && fabs(sum - 3.80155) > 1e-3) success = 0;
    if (p_init == 6 && fabs(sum - 3.80154) > 1e-3) success = 0;
    if (p_init == 7 && fabs(sum - 3.80154) > 1e-3) success = 0;
    if (p_init == 8 && fabs(sum - 3.80153) > 1e-3) success = 0;
    if (p_init == 9 && fabs(sum - 3.80152) > 1e-3) success = 0;
    if (p_init == 10 && fabs(sum - 3.80152) > 1e-3) success = 0;
  }

#define ERROR_SUCCESS                               0
#define ERROR_FAILURE                               -1
  if (success == 1) {
    printf("Success!\n");
    return ERROR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERROR_FAILURE;
  }
}
