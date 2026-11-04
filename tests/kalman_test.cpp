#include "attitude_kalman.h"
#include <cassert>
#include <iostream>
constexpr float RAD=3.14159265358979323846f/180;
Vec3 accelFor(Quaternion q) { auto g=gravityDirection(q); return {g.x*9.80665f,g.y*9.80665f,g.z*9.80665f}; }
int main() {
  Quaternion identity;
  // Gyro bias causes drift without correction. Gravity updates learn observable bias.
  AttitudeKalman rest;
  for(int i=0;i<3000;i++) assert(rest.update(identity,{.3f*RAD,-.2f*RAD,0},{0,0,9.80665f},{},.02,true,true));
  assert(slopeDegrees(rest.value)<.1);
  assert(std::fabs(rest.bias.x-.3f*RAD)<.001f);
  assert(std::fabs(rest.bias.y+.2f*RAD)<.001f);
  // Fully calibrated heading constrains observable Z-axis gyro bias too.
  AttitudeKalman yaw;
  for(int i=0;i<3000;i++) yaw.update(identity,{0,0,.2f*RAD},{0,0,9.80665f},{},.02,true,true);
  assert(angleBetween(identity,yaw.value)<.01);
  assert(std::fabs(yaw.bias.z-.2f*RAD)<.001f);
  // Raw gravity corrects an initial error when calibrated fused tilt is unavailable.
  AttitudeKalman settle;
  Quaternion offset=rotationVector({.05f,0,0});
  for(int i=0;i<500;i++) settle.update(offset,{},{0,0,9.80665f},{.21f,0,0},.02,false,false);
  assert(slopeDegrees(settle.value)<.1);
  // Stationary gravity jitter is reduced, even when heading updates are unavailable.
  AttitudeKalman noise; float rawEnergy=0,filteredEnergy=0;
  for(int i=0;i<1000;i++) {
    float jitter=.1f*RAD*std::sin(i*1.7f);
    auto noisy=rotationVector({jitter,0,0});
    noise.update(identity,{},accelFor(noisy),{},.02,true,false);
    if(i>100) { rawEnergy+=jitter*jitter; float e=slopeDegrees(noise.value)*RAD; filteredEnergy+=e*e; }
  }
  assert(filteredEnergy<rawEnergy*.3f);
  // Recorded hardware showed ~0.1 m/s² sample noise triggering constant holds.
  // This must not turn stationary operation into repeated gyro-only integration.
  AttitudeKalman sampleNoise; int held=0;
  Quaternion flat=multiply(rotationVector({3.14159265f,0,0}),rotationVector({0,.04f,0}));
  auto base=accelFor(flat); float minimum=100,maximum=0;
  for(int i=0;i<1000;i++) {
    Vec3 a{base.x+.02f*std::sin(i*.7f),base.y+.1f*std::sin(i*1.7f),base.z+.02f*std::sin(i*.3f)};
    sampleNoise.update(flat,{.001f,-.001f,.001f},a,{.02f,.1f*std::sin(i*1.7f),.1f},.02,false,true);
    if(i>100) {
      held+=sampleNoise.accelerationHeld;
      float s=slopeDegrees(sampleNoise.value); minimum=std::min(minimum,s); maximum=std::max(maximum,s);
    }
  }
  assert(held<20);
  assert(maximum-minimum<.3f);
  assert(maximum-minimum<.05f);
  assert(sampleNoise.bias.norm()==0); // No bias learning from uncalibrated accel.
  // Calibrated quiet operation follows a stable fused tilt, despite noisy raw accel.
  AttitudeKalman fusedQuiet; float fmin=100,fmax=0;
  for(int i=0;i<1000;i++) {
    Vec3 a{base.x+.02f*std::sin(i*.7f),base.y+.1f*std::sin(i*1.7f),base.z+.02f*std::sin(i*.3f)};
    fusedQuiet.update(flat,{.001f,-.001f,.001f},a,{.02f,.1f*std::sin(i*1.7f),.1f},.02,true,true);
    if(i>200){float s=slopeDegrees(fusedQuiet.value);fmin=std::min(fmin,s);fmax=std::max(fmax,s);}
  }
  assert(fmax-fmin<.05f);
  // Horizontal acceleration would produce ~11.5 degrees of apparent tilt.
  // Hold accel corrections and propagate zero gyro instead.
  AttitudeKalman impulse; impulse.update(identity,{},{0,0,9.80665f},{},.02,true,true);
  Quaternion contaminated=rotationVector({0,-.2f,0});
  for(int i=0;i<100;i++) {
    assert(impulse.update(contaminated,{},{2,0,9.80665f},{2,0,0},.02,true,true));
    assert(impulse.accelerationHeld);
  }
  assert(slopeDegrees(impulse.value)<.01);
  // Real gyro-supported rotation must track while acceleration is rejected.
  AttitudeKalman moving; moving.update(identity,{},{0,0,9.80665f},{},.02,true,false);
  Quaternion truth;
  for(int i=0;i<100;i++) {
    truth=multiply(truth,rotationVector({.2f*.02f,0,0}));
    Vec3 a=accelFor(truth); a.x+=2;
    moving.update(truth,{.2f,0,0},a,{2,0,0},.02,true,false);
  }
  assert(angleBetween(truth,moving.value)<.001);
  // Recover after an impact; scalar heading correction rejects large mag jumps.
  for(int i=0;i<200;i++) impulse.update(identity,{},{0,0,9.80665f},{},.02,true,true);
  assert(!impulse.accelerationHeld && slopeDegrees(impulse.value)<.1);
  Quaternion heading=rotationVector({0,0,1.5});
  impulse.update(heading,{},{0,0,9.80665f},{},.02,true,true);
  assert(impulse.rejected);
  assert(angleBetween(identity,impulse.value)<.01);
  // Invalid sensor data and unknown long sampling gaps.
  assert(!impulse.update(identity,{NAN,0,0},{0,0,9.81},{},.02,true,true));
  Quaternion newAttitude=rotationVector({.3f,0,0});
  impulse.update(newAttitude,{},accelFor(newAttitude),{},1,true,true);
  assert(angleBetween(newAttitude,impulse.value)<.001);
  // A missed rotation must not leave the innovation gate permanently locked.
  AttitudeKalman reconnect; reconnect.update(identity,{},{0,0,9.80665f},{},.02,true,false);
  Quaternion tilted=rotationVector({.9f,0,0}); bool recovered=false;
  for(int i=0;i<150;i++) {
    reconnect.update(tilted,{},accelFor(tilted),{},.02,true,false);
    recovered=recovered || reconnect.reacquired;
  }
  assert(recovered && angleBetween(tilted,reconnect.value)<.01);
  // Exact antipodal gravity and repeated 180-degree return cycles, with both
  // partial and full accel calibration. Simulate missing gyro samples and
  // require the nonlinear recovery to return to the actual fused attitude.
  for(bool calibrated : {false,true}) {
    for(Vec3 axis : {Vec3{1,0,0},Vec3{0,1,0},Vec3{.7071068f,.7071068f,0}}) {
      AttitudeKalman flip; flip.update(identity,{},{0,0,9.80665f},{},.02,calibrated,false);
      Quaternion inverted=rotationVector({axis.x*3.14159265f,axis.y*3.14159265f,0});
      for(int cycle=0;cycle<5;cycle++) {
        for(int i=0;i<120;i++) flip.update(inverted,{},accelFor(inverted),{},.02,calibrated,false);
        assert(angleBetween(flip.value,inverted)<.002);
        for(int i=0;i<120;i++) flip.update(identity,{},accelFor(identity),{},.02,calibrated,false);
        assert(angleBetween(flip.value,identity)<.002);
        assert(slopeDegrees(flip.value)<.05f);
      }
    }
  }
  // A fast gyro-supported flip followed by its inverse preserves the level pose.
  AttitudeKalman fast; fast.update(identity,{},{0,0,9.80665f},{},.02,true,false);
  Quaternion pose;
  for(int sign : {1,-1}) for(int i=0;i<25;i++) {
    float omega=sign*3.14159265f/.5f;
    pose=multiply(pose,rotationVector({omega*.02f,0,0})); pose.normalize();
    auto a=accelFor(pose);a.y+=1;
    fast.update(pose,{omega,0,0},a,{0,1,0},.02,true,false);
  }
  assert(angleBetween(fast.value,identity)<.002);
  // Equivalent negative quaternion must not upset heading correction.
  impulse.update(newAttitude.negated(),{},accelFor(newAttitude),{},.02,true,true);
  assert(angleBetween(newAttitude,impulse.value)<.001);
  std::cout << "Kalman bias, acceleration rejection, gyro motion, heading gate, and recovery passed.\n";
}
