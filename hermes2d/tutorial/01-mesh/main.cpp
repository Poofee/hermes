#include "hermes2d.h"

// This example shows how to load a mesh, perform various types
// of initial refinements, and use keyboard and mouse controls.

static char text[] = "\
Click into the image window and:\n\
  press 'm' to show element numbers,\n\
  press 'b' to toggle boundary markers,\n\
  enlarge your window and press 'c' to center the mesh,\n\
  zoom into the mesh using the right mouse button\n\
  and move the mesh around using the left mouse button\n\
    -- in this way you can read the numbers of all elements,\n\
  press 'c' to center the mesh again,\n\
  press 'm' to hide element numbers,\n\
  press 's' to save a screenshot in bmp format\n\
    -- the bmp file can be converted to any standard\n\
       image format using the command \"convert\",\n\
  press 'q' to quit.\n\
  Also see help - click into the image window and press F1.\n";


int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  mloader.load("domain.mesh", &mesh);

  // Conversion between triangular and quadrilateral meshes (optional). 
  // Need to be done before any other type of mesh refinement.
  //mesh.convert_quads_to_triangles();
  //mesh.convert_triangles_to_quads();

  // Refine mesh uniformly (optional).
  mesh.refine_all_elements();          

  // Refine towards a mesh vertex (optional).
  mesh.refine_towards_vertex(3, 4);    // Four refinements towards vertex no. 3.

  // Refine towards boundary (optional).
  mesh.refine_towards_boundary(2, 4);  // Four refinements towards boundary with marker 2.

  // Refine individual elements (optional).
  mesh.refine_element(86, 0);          // 0... isotropic refinement.
  mesh.refine_element(112, 0);         // 0... isotropic refinement.
  mesh.refine_element(84, 2);          // 2... anisotropic refinement.
  mesh.refine_element(114, 1);         // 1... anisotropic refinement.

  // Display the mesh.
  // (0, 0) is the upper left corner position, 
  // 350 x 350 is the window size.
  MeshView mview("Hello world!", new WinGeom(0, 0, 350, 350));
  mview.show(&mesh);

  // Practice some keyboard and mouse controls.
  printf("%s", text);

  // Wait for the view to be closed.
  View::wait();
  return 0;
}
