#include "orientation_filter.h"
#include <cassert>
#include <iostream>
Quaternion tilt(float degrees) {
  float a=degrees*3.14159265358979323846f/360;
  return {std::cos(a),std::sin(a),0,0};
}
int main() {
  assert(std::fabs(slopeDegrees(tilt(0)))<.001);
  assert(std::fabs(slopeDegrees(tilt(30))-30)<.001);
  assert(std::fabs(slopeDegrees(tilt(90))-90)<.001);
  assert(std::fabs(slopeDegrees(tilt(180)))<.001);
  OrientationFilter f;
  f.update(tilt(10),.02,0);
  assert(f.update(tilt(10).negated(),.02,0));
  assert(angleBetween(f.value,tilt(10))<.001);
  f.update(tilt(80),.02,0);
  assert(f.rejected && std::fabs(slopeDegrees(f.value)-10)<.001);
  f.update(tilt(10),.02,0); assert(!f.rejected);
  // A sustained correction must converge despite low reported gyro speed.
  for (int i=0;i<100;i++) f.update(tilt(45),.02,0);
  assert(std::fabs(slopeDegrees(f.value)-45)<.1);
  assert(!f.update({0,0,0,0},.02,0));
  assert(!f.update({NAN,0,0,0},.02,0));
  // Reduction of deterministic stationary jitter after settling.
  OrientationFilter noise; float rawEnergy=0, filteredEnergy=0;
  for(int i=0;i<600;i++) {
    float jitter=.3f*std::sin(i*1.7f);
    noise.update(tilt(20+jitter),.02,0);
    if(i>100) { rawEnergy+=jitter*jitter; float e=slopeDegrees(noise.value)-20; filteredEnergy+=e*e; }
  }
  assert(filteredEnergy<rawEnergy*.3);
  // Fast real movement is admitted when supported by the gyro.
  OrientationFilter motion; motion.update(tilt(0),.02,0);
  motion.update(tilt(15),.02,14); assert(!motion.rejected);
  std::cout << "Filter tests passed; stationary variance ratio " << filteredEnergy/rawEnergy << '\n';
}
