#pragma once
#include "inclinometer.h"
namespace inclino {
// Body-to-world SH-2 quaternion: project world +Z into the sensor frame.
inline bool gameGravity(float w,float x,float y,float z,Vector &out){
 const float n=w*w+x*x+y*y+z*z;
 if(!std::isfinite(n)||n<.5f||n>1.5f)return false;
 out={2*(x*z-w*y)*G/n,2*(y*z+w*x)*G/n,(w*w-x*x-y*y+z*z)*G/n};
 return finite(out);
}
}
