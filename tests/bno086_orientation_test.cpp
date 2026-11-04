#include "bno086_orientation.h"
#include <cassert>
#include <iostream>
using namespace inclino;
int main(){
 Vector g;assert(gameGravity(1,0,0,0,g));assert(separation(g,{0,0,G})<.001f);
 const float a=30/DEG,s=std::sin(a/2),c=std::cos(a/2);
 assert(gameGravity(c,s,0,0,g));assert(std::fabs(angles(g).roll-30)<.001f);
 assert(gameGravity(c,0,s,0,g));assert(std::fabs(angles(g).pitch-30)<.001f);
 Vector original=g;assert(gameGravity(-c,0,-s,0,g));assert(separation(g,original)<.001f);
 // Compose world yaw with a 30-degree body X tilt; gravity is yaw independent.
 const float yaw=1.1f,cy=std::cos(yaw/2),sy=std::sin(yaw/2);
 assert(gameGravity(cy*c,cy*s,sy*s,sy*c,g));assert(std::fabs(angles(g).roll-30)<.001f);
 assert(gameGravity(0,1,0,0,g));assert(g[2]<0);
 assert(!gameGravity(0,0,0,0,g));assert(!gameGravity(NAN,0,0,0,g));
 std::cout<<"BNO086 quaternion: tilt signs, yaw independence, q/-q, inversion, invalid input passed\n";
}
