//  88-Line 2D Moving Least Squares Material Point Method (MLS-MPM)
// [Explained Version by David Medina]

// Uncomment this line for image exporting functionality
#define TC_IMAGE_IO

// Note: You DO NOT have to install taichi or taichi_mpm.
// You only need [taichi.h] - see below for instructions.
#include "taichi.h"
using namespace taichi;
using Vec = Vector2;
using Mat = Matrix2;

// Window
const int window_size = 800;

// Grid resolution (cells)
const int quality = 1;
const int n_particle_x = 100 * quality;
const int n_particle_y = 8 * quality;
const int n_particle = n_particle_x * n_particle_y;
const int n_elements = (n_particle_x-1) * (n_particle_y - 1)*2;
const int n = 160 * quality;

const real DX = window_size / n;
const real dt = 1e-5_f;
const real frame_dt = 1e-4_f;
const real dx = 1.0_f / n;
const real inv_dx = 1.0_f / dx;

// Snow material properties
const auto particle_mass = 1.0_f;
const auto vol = 1.0_f;        // Particle Volume
const auto hardening = 10.0_f; // Snow hardening factor
const auto E = 1e4_f;          // Young's Modulus
const auto nu = 0.2_f;         // Poisson ratio
const bool plastic = true;

// Initial Lamé parameters
const real mu_0 = E / (2 * (1 + nu));
const real lambda_0 = E * nu / ((1+nu) * (1 - 2 * nu));

enum Material { Water, Jelly,Snow, Metal};
int color[] = {0xF2522E, 0x023059, 0x025940, 0x5400B4, 0x0D0D0D, 0x0061D3, 0x401D26, 0xCFD600, 0xE76C00, 0xC11321};
const real G = 1e-3_f;
/***********************************(1)*****************************************/
// the definition of a particle
struct Particle {
  // Position and velocity
  Vec x, v;
  // Deformation gradient
  Mat F;
  // Affine momentum from APIC
  Mat C;
  // Determinant of the deformation gradient (i.e. volume)
  real Jp;
  // Color
  int c;
  Material material;

  Particle(Vec x, int c, Vec v=Vec(0), Material m=Snow) :
    x(x),
    v(v),
    F(1),
    C(0),
    Jp(1),
    c(c),
    material(m) {}

  Particle():
    x(Vec(0)),
    v(Vec(0)),
    F(1),
    C(0),
    Jp(1),
    c(0),
    material(Snow){}
};
//std::vector<Particle> particles;

struct Planet {
  Vec center;
  real radius;
  real mass;
  real rho;
  real omega;
  int color;
  Material material;
  Planet(Vec x, real r, real M,real rho, int c):
    center(x),
    radius(r),
    mass(M),
    rho(rho),
    omega(0),
    color(c),
    material(Metal){}

  Planet(Vec x, real r, real M, real rho,real o, int c, Material m) :
      center(x),
      radius(r),
      mass(M),
      rho(rho),
      omega(o),
      color(c),
      material(m){}
  Planet():
    center(Vec(0)),
    radius(0),
    mass(0),
    rho(0),
    omega(0),
    color(0),
    material(Metal) {}

};


////////////////////////////////////////////////////////////////////////////////

// Vector3: [velocity_x, velocity_y, mass]
//Vector3 grid[n + 1][n + 1];


class mpm
{
private:
  // Vector3: [velocity_x, velocity_y, mass]
  Vector3 grid[n+1][n+1];
  std::vector<Vector2i> grid_indices;
public:
  std::vector<Particle> particles;
  Vector3 vertices[n_elements];
  std::vector<Vector3> obstacles;//[center_x, center_y, radius]
  struct Planet center_planet;
  struct Planet satellite;
  std::vector<Vec> orbit;
 public: 
  mpm(){};
  ~mpm(){};

public:
 // Seed particles with position and color
  void add_square(Vec center, int c, Material material = Snow);

  void add_spring(Vec center, int c, Material material = Jelly);

  void add_circle(Vec center, real radius,int num, int c, Vec v, Material material);

  void add_satellite(Vec center, real radius, real rho, int c, Material material);

  void add_obstacle(Vec center, real radius);

  void add_center_planet(Vec center, real radius, real rho, int c);

  void substep();

private:
  int mesh(const int,const int);

  void P2G();
  void grid_op();
  void G2P();

  void Roche_model();

  bool check_outrange(Vec p);
};

int main() {
  GUI gui("Real-time 2D MLS-MPM", window_size, window_size);
  auto &canvas = gui.get_canvas();
  mpm m;
  Vec center(0.5, 0.5);
  m.add_center_planet(center, 25, 10, 0xFFFFFF);
  real satellite_rho = 1;
  real satellite_radius = 10;
  real Roche_radius = 2.5 * m.center_planet.radius * real(pow(m.center_planet.rho / satellite_rho, 1 / 3));

  m.add_satellite(center + Vec(0, (Roche_radius+satellite_radius) / window_size) + Vec(0,0.3), satellite_radius, satellite_rho, 0xBDE4FF, Metal);
  //m.add_circle(center, 20, 100, 0x023059, Vec(0,0), Snow);

  //m.add_circle(center+Vec(0,0.2), 20, 1000, 0x023059, Vec(0, 0), Snow);
  int frame = 0;
  // Main Loop
  for (int step = 0;; step++) {
    // Advance simulation
    m.substep();

    // Visualize frame
    if (step % int(frame_dt / dt) == 0) {
      // Clear background
      canvas.clear(0x0D0D0D);
      // Box
      canvas.rect(Vec(0.01), Vec(0.99)).radius(2).color(0xD0ECF2).close();

      //center collision circle
      for(auto& o : m.obstacles){
          canvas.circle(o.x,o.y).radius(o.z).color(0xD0ECF2);
      }
      // Particles
      for (auto& p : m.particles) {
        canvas.circle(p.x).radius(0.5).color(p.c);
      }
      //canvas.circle(m.particles[0].x).radius(5).color(m.particles[0].c);
      // Planet
      if(m.center_planet.mass > 0){
        canvas.circle(m.center_planet.center).radius(m.center_planet.radius).color(m.center_planet.color);
      }
      canvas.circle(m.satellite.center).radius(3).color(0xED553B);
      for (auto& o : m.orbit) {
        canvas.circle(o).radius(0.25).color(0xDBF227);
      }
      // Update image  
      gui.update();

      // Write to disk (optional)
      canvas.img.write_as_image(fmt::format("Roche_limit_balance/{:05d}.png", frame++));
      if (frame > 6000)
          return 0;
    }
  }
}

// Seed particles with position and color
void mpm::add_square(Vec center, int c, Material material) {
  // Randomly sample 1000 particles in the square
  for (int i = 0; i < 1000; i++) {
    particles.push_back(Particle((Vec::rand() * 2.0f - Vec(1)) * 0.08f + center, c, Vec(0), material));
  }
}

void mpm::add_spring(Vec center, int c, Material material) {
  //particles.reserve(n_particle_x * n_particle_y);
  particles.resize(n_particle_x * n_particle_y);
  int t;
  for (int i = 0; i < n_particle_x; i++){
    for (int j = 0; j < n_particle_y; j++){
      t = mesh(i, j);
      particles[t] = Particle(Vec(i * dx, j * dx) *0.32f + center, c, Vec(0), material);
    } 
  }

}

void mpm::add_circle(Vec center, real radius, int num, int c, Vec v, Material material) {
  real r = radius / window_size;
	for (int i = 0; i < num;) {
        /*real d_theta = 2 * pi / num;
        real theta = d_theta * i;
        Vec pos = Vec(r * cos(theta), r * sin(theta));*/
        auto pos = (Vec::rand() * 2.0_f - Vec(1)) * r;
        if (pos.x * pos.x + pos.y * pos.y < r * r) {
            particles.push_back(Particle(pos + center, c, v, material));
            i++;
        }
	}
}

void mpm::add_satellite(Vec center, real radius, real rho, int c, Material material) {
  if(center_planet.mass == 0){
    std::cout << "No center center_planet" << std::endl;
    exit(0);
  }else{
    real r = radius / window_size;
    int n = int(rho *pow(radius,3));  // * 4.0f / 3.0f * pi
    //int n = 0;
    Vec cp_center = center_planet.center;
    real cp_r = center_planet.radius / window_size;
    Vec d = center - cp_center;
    real dist = sqrt(d.x*d.x + d.y*d.y);
    if(dist < cp_r + r){
      std::cout << "Too close to center center_planet" << std::endl;
      exit(0);
    }
    real omega = sqrt(G * center_planet.mass / (dist)) / dist;
    Vec v = Vec(-d.y,   d.x) / dist * sqrt(G * center_planet.mass / dist);

    //add center_particle
    particles.push_back(Particle(center, c, v, material));

    for (int i = 0; i < n;) {
      /*real d_theta = 2 * pi / num;
      real theta = d_theta * i;
      Vec pos = Vec(r * cos(theta), r * sin(theta));*/
      auto pos = (Vec::rand() * 2.0_f - Vec(1)) * r;
      if (pos.x * pos.x + pos.y * pos.y <= r * r) {
        d = pos + center - cp_center;
        v = omega * Vec(-d.y, d.x);
        //v = Vec(0);
        particles.push_back(Particle(pos + center, c, v, material));
        i++;
      }
    }
    satellite = Planet(center, radius, n*particle_mass, rho,omega, c, material);
  }
  
}


void mpm::add_obstacle(Vec center, real radius) {
  obstacles.push_back(Vector3(center, radius));
}

void mpm::add_center_planet(Vec center, real radius, real rho, int c) {
  // Randomly sample 1000 particles in the square
  real mass = rho * pow(radius, 3);// * 4.0f / 3.0f * pi;
  center_planet = Planet(center, radius, mass*particle_mass,rho, c);
}

void mpm::substep() {
    if(satellite.material==Metal)
        Roche_model();
    else {
        std::memset(grid, 0, sizeof(grid));
        P2G();
        grid_op();
        G2P();
    }
}

int mpm::mesh(const int i,const int j) {
  return i * n_particle_y + j;
}

void mpm::P2G() {
  // P2G
  for (auto& p : particles) {
    Vector2i base_coord = (p.x * inv_dx - Vec(0.5f)).cast<int>();
    Vec fx = p.x * inv_dx - base_coord.cast<real>();
    Vec w[3] = {
        Vec(0.5) * sqr(Vec(1.5) - fx),
        Vec(0.75) - sqr(fx - Vec(1.0)),
        Vec(0.5) * sqr(fx - Vec(0.5))};
    auto e = std::exp(hardening * (1.0_f - p.Jp));
    if (p.material == Jelly)  // 果冻
      e = 0.3;
    auto mu = mu_0 * e, lambda = lambda_0 * e;
    if (p.material == Water)  // 流体
      mu = 0;
    if(p.material == Metal){  // 金属
      mu = 0;
      lambda = 0;
    }
    real J = determinant(p.F);
    Mat r, s;
    polar_decomp(p.F, r, s);
    real Dinv = 4 * inv_dx * inv_dx;
    auto PF = (2 * mu * (p.F - r) * transposed(p.F) + lambda * (J - 1) * J);
    auto stress = -(dt * vol) * (Dinv * PF);
    auto affine = stress + particle_mass * p.C;

    // P2G
    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        auto dpos = (Vec(i, j) - fx) * dx;
        // Translational momentum
        Vector3 mass_x_velocity(p.v * particle_mass, particle_mass);
        grid[base_coord.x + i][base_coord.y + j] += (w[i].x * w[j].y * (mass_x_velocity + Vector3(affine * dpos, 0)));
      }
    }
  }

  grid_indices.clear();
  grid_indices.reserve((n+1) * (n+1));
  for (int i = 0; i <= n; i++) {
    for (int j = 0; j <= n; j++) {
      auto& g = grid[i][j];
      if (g[2] > 0) {
        grid_indices.push_back(Vector2i(i, j));
      }
    }
  }
}

void mpm::grid_op() {
  for (auto& ind_i : grid_indices) {
      auto& g = grid[ind_i.x][ind_i.y];
      // No need for epsilon here
        // Normalize by mass
      g /= g[2];

      // Gravity
      // g += dt * Vector3(0, -200, 0);//gravity

      // center planet      
      //in the example, there is only one center_planet
      Vec gravity = Vec(0);

      for (auto& ind_j : grid_indices) {
        auto& g2 = grid[ind_j.x][ind_j.y];
        if (ind_i == ind_j)
          continue;
        Vec d = (Vec(ind_i.x, ind_i.y) - Vec(ind_j.x, ind_j.y)) * dx;
        real dist = sqrt(d.x * d.x + d.y * d.y);
        gravity += -G * g2[2] / (dist * dist * dist) * d;
      }
      g += dt * Vector3(gravity.x, gravity.y, 0);

      if(center_planet.mass != 0){
        Vec center = center_planet.center;
        real r = center_planet.radius * dx / DX;
        Vec d = Vec(ind_i.x, ind_i.y) * dx - center;
        real dist = sqrt(d.x * d.x + d.y * d.y);
        if (dist <= r) {
          g = Vector3(0, 0, g[2]);
        } 
         else {
           gravity = -G * center_planet.mass / (dist * dist * dist) * d;
           g += dt * Vector3(gravity.x, gravity.y, 0);
         }
      }
      

      // center collision circle
      for (size_t k = 0; k < obstacles.size(); k++) {
        Vec center = Vec(obstacles[k][0], obstacles[k][1]);
        real radius = obstacles[k][2] * dx / DX;
        Vec d = Vec(ind_i.x, ind_i.y) * dx - center;
        real dist = sqrt(d.x * d.x + d.y * d.y);
        if (dist <= radius) {
          d = d / dist;
          real n_normal = g[0] * d.x + g[1] * d.y;  // dot product
          n_normal = n_normal < 0 ? 2 * n_normal : 0;
          g -= Vector3(n_normal * d.x, n_normal * d.y, 0);
        }
      }

      // boundary thickness
      real boundary = 0.01;
      // Node coordinates
      real x = (real)ind_i.x / n;
      real y = (real)ind_i.y / n;

      // Sticky boundary

      if (x < boundary && g[0]<0) {
        g[0] = -g[0];
      }
      if (x > 1 - boundary && g[0]>0) {
        g[0] = -g[0];
      }
      // Separate boundary
      if (y < boundary && g[1]<0) {
        g[1] = -g[1];
      }
      if (y > 1 - boundary && g[1]>0) {
        g[1] = -g[1];
      }
  }
}

void mpm::G2P() {
  for (auto& p : particles) {
    // element-wise floor
    Vector2i base_coord = (p.x * inv_dx - Vec(0.5f)).cast<int>();
    Vec fx = p.x * inv_dx - base_coord.cast<real>();
    Vec w[3] = {
        Vec(0.5) * sqr(Vec(1.5) - fx),
        Vec(0.75) - sqr(fx - Vec(1.0)),
        Vec(0.5) * sqr(fx - Vec(0.5))};

    p.C = Mat(0);
    p.v = Vec(0);

    for (int i = 0; i < 3; i++) {
      for (int j = 0; j < 3; j++) {
        auto dpos = (Vec(i, j) - fx);
        auto grid_v = Vec(grid[base_coord.x + i][base_coord.y + j]);
        auto weight = w[i].x * w[j].y;
        // Velocity
        p.v += weight * grid_v;
        // APIC C
        p.C += 4 * inv_dx * Mat::outer_product(weight * grid_v, dpos);
      }
    }

    // Advection
    p.x += dt * p.v;
  
    // MLS-MPM F-update
    auto F = (Mat(1) + dt * p.C) * p.F;

    /***********************************(3)*****************************************/
    if (p.material == Water) {  // Water
      p.F = Mat(1) * sqrt(determinant(F));
    } else if (p.material == Jelly) {  // Jelly
      p.F = F;
    } else if (p.material == Snow) {  // Snow
      Mat svd_u, sig, svd_v;
      svd(F, svd_u, sig, svd_v);
      for (int i = 0; i < 2 * int(plastic); i++)  // Snow Plasticity
        sig[i][i] = clamp(sig[i][i], 1.0_f - 2.5e-2_f, 1.0_f + 7.5e-3_f);
      real oldJ = determinant(F);
      F = svd_u * sig * transposed(svd_v);
      real Jp_new = clamp(p.Jp * oldJ / determinant(F), 0.6_f, 20.0_f);
      p.Jp = Jp_new;
      p.F = F;
    }
    ////////////////////////////////////////////////////////////////////////////////
  }
  satellite.center = particles[0].x;
}


void mpm::Roche_model() {
    std::vector<Vec> force;
    force.resize(particles.size());
    Vec gravity = Vec(0);
    Vec radius;
    real dist;
    real rho;

    for (int i = 0; i < force.size(); i++) {
        gravity = Vec(0);
        radius = particles[i].x - satellite.center;
        dist = sqrt(radius.x * radius.x + radius.y * radius.y);
        rho = satellite.rho * particle_mass;
        if(dist > satellite.radius / window_size){
          for (int j = 0; j < particles.size(); j++) {
            if (i == j)
                continue;
            Vec r = particles[i].x - particles[j].x;
            real d = sqrt(r.x * r.x + r.y * r.y);
            if (d <= 0.0001)
                continue;
            gravity += -G * particle_mass / real(pow(d, 3)) * r;
          }
        }
        else
        {
            gravity += -G * rho * radius;
        }

        
        radius = particles[i].x - center_planet.center;
        dist = sqrt(radius.x * radius.x + radius.y * radius.y);
        if (dist > center_planet.radius / window_size) {
            gravity += -radius * G * center_planet.mass / real((pow(dist, 3)));
        }
        force[i] = gravity;
    }
    real boundary = 0.01;
    
    for (int i = 0; i < particles.size(); i++) {
        Particle &p = particles[i];
        if(p.x.x < boundary){
            p.v.x = max(p.v.x, 0.0_f);        
        }else if(p.x.x > 1 - boundary){
            p.v.x = min(p.v.x, 0.0_f);
        }else if(p.x.y < boundary){
            p.v.y = max(p.v.y, 0.0_f);
        }else if(p.x.y > 1 - boundary){
            p.v.y = min(p.v.y, 0.0_f);
        }else {
            Vec radius = p.x - center_planet.center;
            real dist = sqrt(radius.x * radius.x + radius.y * radius.y);
            if (dist < center_planet.radius / window_size) {
                p.v = Vec(0);
            }else{
                p.v += dt * force[i];
                p.x += dt * p.v;
            }
        }
        
    }
    //for (auto iter = particles.begin(); iter != particles.end(); iter++) {
    //    if(check_outrange((*iter).x))
    //        particles.erase(iter);
    //}
    radius = satellite.center - center_planet.center;
    //satellite.center += satellite.omega * dt * (Vec(-radius.y, radius.x));
    satellite.center = particles[0].x;
    orbit.push_back(satellite.center);
}
bool mpm::check_outrange(Vec p) {
    if (p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1)
        return true;
    return false;   
}
/* -----------------------------------------------------------------------------
** Reference: A Moving Least Squares Material Point Method with Displacement
              Discontinuity and Two-Way Rigid Body Coupling (SIGGRAPH 2018)

  By Yuanming Hu (who also wrote this 88-line version), Yu Fang, Ziheng Ge,
           Ziyin Qu, Yixin Zhu, Andre Pradhana, Chenfanfu Jiang


** Build Instructions:

Step 1: Download and unzip mls-mpm88.zip (Link: http://bit.ly/mls-mpm88)
        Now you should have "mls-mpm88.cpp" and "taichi.h".

Step 2: Compile and run

* Linux:
    g++ mls-mpm88-explained.cpp -std=c++14 -g -lX11 -lpthread -O3 -o mls-mpm
    ./mls-mpm


* Windows (MinGW):
    g++ mls-mpm88-explained.cpp -std=c++14 -lgdi32 -lpthread -O3 -o mls-mpm
    .\mls-mpm.exe


* Windows (Visual Studio 2017+):
  - Create an "Empty Project"
  - Use taichi.h as the only header, and mls-mpm88-explained.cpp as the only source
  - Change configuration to "Release" and "x64"
  - Press F5 to compile and run


* OS X:
    g++ mls-mpm88-explained.cpp -std=c++14 -framework Cocoa -lpthread -O3 -o mls-mpm
    ./mls-mpm


** FAQ:
Q1: What does "1e-4_f" mean?
A1: The same as 1e-4f, a float precision real number.

Q2: What is "real"?
A2: real = float in this file.

Q3: What are the hex numbers like 0xED553B?
A3: They are RGB color values.
    The color scheme is borrowed from
    https://color.adobe.com/Copy-of-Copy-of-Core-color-theme-11449181/

Q4: How can I get higher-quality?
A4: Change n to 320; Change dt to 1e-5; Change E to 2e4;
    Change particle per cube from 500 to 8000 (Ln 72).
    After the change the whole animation takes ~3 minutes on my computer.

Q5: How to record the animation?
A5: Uncomment Ln 2 and 85 and create a folder named "tmp".
    The frames will be saved to "tmp/XXXXX.png".

    To get a video, you can use ffmpeg. If you already have taichi installed,
    you can simply go to the "tmp" folder and execute

      ti video 60

    where 60 stands for 60 FPS. A file named "video.mp4" is what you want.

Q6: How is taichi.h generated?
A6: Please check out my #include <taichi> talk:
    http://taichi.graphics/wp-content/uploads/2018/11/include_taichi.pdf
    and the generation script:
    https://github.com/yuanming-hu/taichi/blob/master/misc/amalgamate.py
    You can regenerate it using `ti amal`, if you have taichi installed.

Questions go to yuanming _at_ mit.edu
                            or https://github.com/yuanming-hu/taichi_mpm/issues.

                                                      Last Update: March 6, 2019
                                                      Version 1.5

----------------------------------------------------------------------------- */


