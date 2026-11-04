#include "inclinometer.h"
#include <cassert>
#include <iostream>
using namespace inclino;
Vector at(float pitch,float roll=0){float p=pitch/DEG,r=roll/DEG;return {-G*std::sin(p),G*std::sin(r)*std::cos(p),G*std::cos(r)*std::cos(p)};}
void run(Meter &m,Vector a,float &t,int n=360,Vector g={},bool hasGyro=false){for(int i=0;i<n;i++){t+=.02f;m.update(a,g,hasGyro,Calibration(),t,true);}}
int main(){
 assert(std::fabs(separation(at(0),at(.001f))-.001f)<.00001f);
 Calibration identity;Meter m;float t=0;
 Meter early;float te=0;run(early,at(10),te,240);assert(early.state!=Ready);
 run(m,at(10),t);assert(m.state==Ready);assert(std::fabs(m.result.pitch-10)<.001);assert(std::fabs(m.result.slope-10)<.001);
 assert(std::fabs(m.result.gradeX+100*std::tan(10/DEG))<.001);
 // Repeated flip/return cycles remeasure gravity rather than integrate errors.
 for(int cycle=0;cycle<5;cycle++){
  run(m,at(0,180),t,20,{20,0,0},true);assert(m.state!=Ready);
  run(m,at(10),t);assert(m.state==Ready);assert(std::fabs(m.result.slope-10)<.001);
 }
 auto inverted=angles(at(0,180));assert(inverted.slope<.001);assert(std::fabs(std::fabs(inverted.roll)-180)<.001);
 assert(!std::isfinite(angles(at(90)).grade));
 // Slow direction drift is rejected even with a norm of exactly 1g.
 Meter drift;float td=0;for(int i=0;i<500;i++){td+=.02f;drift.update(at(td*.3f),{},false,identity,td,true);}assert(drift.state!=Ready);
 // A sustained yaw gyro signal is rejected even if the gravity direction does not change.
 Meter rotating;float tr=0;run(rotating,at(0),tr,500,{0,0,1},true);assert(rotating.state!=Ready);
 // Vector averaging is safe across the +/-180-degree roll branch.
 Meter branch;float tb=0;for(int i=0;i<400;i++){tb+=.02f;branch.update(at(0,i%2?179.99f:-179.99f),{},false,identity,tb,true);}assert(branch.state==Ready);assert(std::fabs(std::fabs(branch.result.roll)-180)<.05);
 // Shock invalidates, settles, and recovers the same result without locking it.
 m.update({G,0,G},{},false,identity,t+.02f,true);t+=.02f;assert(m.state!=Ready);
 run(m,at(10.2f),t);assert(m.state==Ready);assert(std::fabs(m.result.pitch-10.2f)<.002);
 // Missing/invalid samples discard the window.
 m.update({}, {},false,identity,t+.02f,false);t+=.02f;assert(m.state==Fault);run(m,at(10),t);assert(m.state==Ready);
 m.update(at(10),{},false,identity,t+1,true);assert(m.state!=Ready);
 // Configurable low-pass can be disabled; calibrated sensor bias/scale recovered.
 Config cfg;cfg.lowpassSeconds=0;Meter bypass(cfg);float tp=0;run(bypass,at(20),tp);assert(bypass.state==Ready);
 SixFace six;Calibration target;target.bias={.12f,-.08f,.2f};target.scale={1.02f,.98f,1.01f};
 for(int face=0;face<6;face++){Vector raw=target.bias;int axis=face/2;raw[axis]+=(face%2?-G:G)/target.scale[axis];assert(six.capture(face,raw));}
 Calibration solved;assert(six.solve(solved));for(int i=0;i<3;i++){assert(std::fabs(solved.bias[i]-target.bias[i])<1e-5);assert(std::fabs(solved.scale[i]-target.scale[i])<1e-5);}
 SixFace incomplete;assert(!incomplete.solve(solved));assert(!incomplete.capture(0,{0,0,G}));assert(!incomplete.capture(0,{G,4,0}));
 std::cout<<"Static gravity: signs/grades, flips, shock recovery, slow-motion rejection, branch cut, gaps, six-face calibration passed\n";
}
