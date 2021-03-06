#define H2D_REPORT_WARN
#define H2D_REPORT_INFO
#define H2D_REPORT_VERBOSE
#define H2D_REPORT_FILE "application.log"

#include "hermes2d.h"
#include <string>

using namespace RefinementSelectors;

// This test makes sure that the example "newton-np-timedep-adapt-system" works correctly.

#define SIDE_MARKER 1
#define TOP_MARKER 2
#define BOT_MARKER 3

// Parameters to tweak the amount of output to the console.
#define NOSCREENSHOT

/*** Fundamental coefficients ***/
const double D = 10e-11; 	            // [m^2/s] Diffusion coefficient.
const double R = 8.31; 		            // [J/mol*K] Gas constant.
const double T = 293; 		            // [K] Aboslute temperature.
const double F = 96485.3415;	            // [s * A / mol] Faraday constant.
const double eps = 2.5e-2; 	            // [F/m] Electric permeability.
const double mu = D / (R * T);              // Mobility of ions.
const double z = 1;		            // Charge number.
const double K = z * mu * F;                // Constant for equation.
const double L =  F / eps;	            // Constant for equation.
const double VOLTAGE = 1;	            // [V] Applied voltage.
const scalar C0 = 1200;	                    // [mol/m^3] Anion and counterion concentration.

/* For Neumann boundary */
const double height = 180e-6;	            // [m] thickness of the domain.
const double E_FIELD = VOLTAGE / height;    // Boundary condtion for positive voltage electrode


/* Simulation parameters */
const int NSTEP = 50;                 // Number of time steps.
const double TAU = 0.1;               // Size of the time step.
const int P_INIT = 3;       	      // Initial polynomial degree of all mesh elements.
const int REF_INIT = 1;     	      // Number of initial refinements.
const bool MULTIMESH = false;	      // Multimesh?
const int TIME_DISCR = 2;             // 1 for implicit Euler, 2 for Crank-Nicolson.
const int VOLT_BOUNDARY = 1;          // 1 for Dirichlet, 2 for Neumann.

/* Nonadaptive solution parameters */
const double NEWTON_TOL = 1e-6;       // Stopping criterion for nonadaptive solution.

/* Adaptive solution parameters */
const bool SOLVE_ON_COARSE_MESH = false;  // true... Newton is done on coarse mesh in every adaptivity step.
                                      // false...Newton is done on coarse mesh only once, then projection 
                                      // of the fine mesh solution to coarse mesh is used.
const double NEWTON_TOL_COARSE = 0.01;// Stopping criterion for Newton on coarse mesh.
const double NEWTON_TOL_FINE = 0.05;  // Stopping criterion for Newton on fine mesh.
const int NEWTON_MAX_ITER = 100;      // Maximum allowed number of Newton iterations.

const int UNREF_FREQ = 5;             // every UNREF_FREQth time step the mesh is unrefined.
const double THRESHOLD = 0.3;         // This is a quantitative parameter of the adapt(...) function and
                                      // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;               // Adaptive strategy:
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
const int MESH_REGULARITY = -1;  // Maximum allowed level of hanging nodes:
                                 // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                 // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                 // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                 // Note that regular meshes are not supported, this is due to
                                 // their notoriously bad performance.
const double CONV_EXP = 1.0;     // Default value is 1.0. This parameter influences the selection of
                                 // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const int NDOF_STOP = 5000;		        // To prevent adaptivity from going on forever.
const double ERR_STOP = 0.1;          // Stopping criterion for adaptivity (rel. error tolerance between the
                                      // fine mesh and coarse mesh solution in percent).
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// Program parameters
const std::string USE_ADAPTIVE("adapt");

// Weak forms
#include "forms.cpp"


/*** Boundary types and conditions ***/

// Poisson takes Dirichlet and Neumann boundaries
BCType phi_bc_types(int marker) {
  return (marker == SIDE_MARKER || (marker == TOP_MARKER && VOLT_BOUNDARY == 2)) 
    ? BC_NATURAL : BC_ESSENTIAL;
}

// Nernst-Planck takes Neumann boundaries
BCType C_bc_types(int marker) {
  return BC_NATURAL;
}

// Diricleht Boundary conditions for Poisson equation.
scalar C_essential_bc_values(int ess_bdy_marker, double x, double y) {
  return 0;
}

// Diricleht Boundary conditions for Poisson equation.
scalar phi_essential_bc_values(int ess_bdy_marker, double x, double y) {
  return ess_bdy_marker == TOP_MARKER ? VOLTAGE : 0.0;
}

template<class Real, class Scalar>
Scalar linear_form_surf_top(int n, double *wt, Func<Scalar> *u_ext[], Func<Real> *v, Geom<Real> *e, ExtData<Scalar> *ext) {
  return -E_FIELD * int_v<Real, Scalar>(n, wt, v);
}

scalar voltage_ic(double x, double y, double &dx, double &dy) {
  // y^2 function for the domain.
  //return (y+100e-6) * (y+100e-6) / (40000e-12);
  return 0.0;
}

scalar concentration_ic(double x, double y, double &dx, double &dy) {
  return C0;
}

int main (int argc, char* argv[]) {
  
  // Load the mesh file.
  Mesh Cmesh, phimesh, basemesh;
  H2DReader mloader;
  mloader.load("small.mesh", &basemesh);
  
  basemesh.refine_towards_boundary(TOP_MARKER, REF_INIT);
  basemesh.refine_towards_boundary(BOT_MARKER, REF_INIT - 1);
  Cmesh.copy(&basemesh);
  phimesh.copy(&basemesh);

  // Spaces for concentration and the voltage.
  H1Space C(&Cmesh, C_bc_types, C_essential_bc_values, P_INIT);
  H1Space phi(MULTIMESH ? &phimesh : &Cmesh, phi_bc_types, phi_essential_bc_values, P_INIT);

  Solution C_sln, C_ref_sln,C_prev_time,
           phi_sln, phi_ref_sln, phi_prev_time;

  // The weak form for 2 equations.
  WeakForm wf(2);
  // Add the bilinear and linear forms.
  wf.add_matrix_form(0, 0, callback(J_euler_DFcDYc), H2D_UNSYM, H2D_ANY, &phi_prev_time);
  wf.add_matrix_form(0, 1, callback(J_euler_DFcDYphi), H2D_UNSYM, H2D_ANY, &C_prev_time);
  wf.add_matrix_form(1, 0, callback(J_euler_DFphiDYc), H2D_UNSYM);
  wf.add_matrix_form(1, 1, callback(J_euler_DFphiDYphi), H2D_UNSYM);
  wf.add_vector_form(0, callback(Fc_euler), H2D_ANY, Tuple<MeshFunction*>(&C_prev_time, &phi_prev_time));
  wf.add_vector_form(1, callback(Fphi_euler), H2D_ANY, Tuple<MeshFunction*>(&C_prev_time, &phi_prev_time));

  // Neumann voltage boundary.
  wf.add_vector_form_surf(1, callback(linear_form_surf_top), TOP_MARKER);

  // Initialize adaptivity parameters.
  AdaptivityParamType apt(ERR_STOP, NDOF_STOP, THRESHOLD, STRATEGY, MESH_REGULARITY);

  // Create a selector which will select optimal candidate.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Assign initial condition to mesh.
  phi_prev_time.set_exact(MULTIMESH ? &phimesh : &Cmesh, voltage_ic);
  C_prev_time.set_exact(&Cmesh, concentration_ic);

//  C_prev_time.set_const(&Cmesh, C0);
//  phi_prev_time.set_const(MULTIMESH ? &phimesh : &Cmesh, 0);
  Vector *coeff_vec = new AVector();

  // Time stepping loop.
  int ts = 1; //for saving screenshot
  for (int n = 1; n <= NSTEP; n++) 
  {
    info("---- Time step %d:", ts);
    // Periodic global derefinements.
    if (n % UNREF_FREQ == 0) 
    {
      info("Global mesh derefinement.");
      Cmesh.copy(&basemesh);
      if (MULTIMESH) 
      {
        phimesh.copy(&basemesh);
      }
      C.set_uniform_order(P_INIT);
      phi.set_uniform_order(P_INIT);
      int ndofs;
      ndofs = C.assign_dofs();
      phi.assign_dofs(ndofs);
    }

    // Update the coefficient vector and u_prev_time.
    project_global(Tuple<Space*>(&C, &phi), Tuple<int>(H2D_H1_NORM, H2D_H1_NORM), 
                   Tuple<MeshFunction*>(&C_prev_time, &phi_prev_time), 
                   Tuple<Solution*>(&C_prev_time, &phi_prev_time), coeff_vec);

    // Adaptivity loop (in space):
    bool verbose = true;     // Print info during adaptivity.
    info("Projecting coarse mesh solution to obtain initial vector on new fine mesh.");
    // The NULL pointers mean that we are not interested in visualization during the Newton's loop.
    solve_newton_adapt(Tuple<Space*>(&C, &phi), &wf, coeff_vec, matrix_solver,                                                        Tuple<int>(H2D_H1_NORM, H2D_H1_NORM), 
                       Tuple<Solution*>(&C_sln, &phi_sln), 
                       Tuple<Solution*>(&C_ref_sln, &phi_ref_sln),
                       NULL, NULL, &selector, &apt,
                       NEWTON_TOL_COARSE, NEWTON_TOL_FINE, NEWTON_MAX_ITER, verbose);
  }
  delete coeff_vec;

}
