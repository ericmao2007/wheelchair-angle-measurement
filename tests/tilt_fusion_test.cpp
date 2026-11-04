#include "tilt_fusion.h"
#include <cassert>
#include <iostream>
using namespace inclino;
Vector pitchAt(float p){float a=p/DEG;return {-G*std::sin(a),0,G*std::cos(a)};}
Vector rollAt(float r){float a=r/DEG;return {0,G*std::sin(a),G*std::cos(a)};}
int main(){
 TiltFusion f;float t=0;
 assert(f.update(pitchAt(17),{},t,true));assert(std::fabs(angles(f.gravity).pitch-17)<.001f);
 // Body +Y rotation produces positive pitch; +X produces positive roll.
 f.reset();f.update(pitchAt(0),{},0,true);
 for(int i=1;i<=250;i++){t=i*.02f;f.update(pitchAt(t*12),{0,12,0},t,true);}
 assert(std::fabs(angles(f.gravity).pitch-60)<1);
 f.reset();f.update(rollAt(0),{},0,true);
 for(int i=1;i<=250;i++){t=i*.02f;f.update(rollAt(t*12),{12,0,0},t,true);}
 assert(std::fabs(angles(f.gravity).roll-60)<1);
 // Rotation around gravity does not fabricate tilt or absolute heading.
 f.reset();f.update(pitchAt(0),{},0,true);
 for(int i=1;i<1000;i++)f.update(pitchAt(0),{0,0,25},i*.02f,true);
 assert(angles(f.gravity).slope<.001f);
 // Constant transverse gyro bias is corrected rather than integrating forever.
 f.reset();f.update(pitchAt(8),{},0,true);
 for(int i=1;i<=6000;i++)f.update(pitchAt(8),{0,.5f,0},i*.02f,true);
 const float biasError=std::fabs(angles(f.gravity).pitch-8);
 assert(biasError<.02f);
 // Wrong-magnitude acceleration burst is downweighted while gyro tracks tilt.
 f.reset();f.update(pitchAt(0),{},0,true);
 for(int i=1;i<25;i++)f.update({2,0,G}, {},i*.02f,true);
 for(int i=25;i<600;i++)f.update(pitchAt(0),{},i*.02f,true);
 assert(std::fabs(angles(f.gravity).pitch)<.2f);
 // Recovery from reversed gravity, sample loss, invalid frames and reboot.
 f.reset();f.update(pitchAt(0),{},0,true);
 for(int i=1;i<1500;i++)f.update({0,0,-G},{},i*.02f,true);
 assert(separation(f.gravity,{0,0,-G})<.5f);
 assert(!f.update({}, {},30,false));assert(f.update(pitchAt(21),{},31,true));
 assert(std::fabs(angles(f.gravity).pitch-21)<.001f);
 assert(f.update(pitchAt(-12),{},1,true));assert(std::fabs(angles(f.gravity).pitch+12)<.001f);
 std::cout<<"Six-axis tilt observer passed; static 0.5 deg/s gyro-bias residual="<<biasError<<" deg\n";
}
