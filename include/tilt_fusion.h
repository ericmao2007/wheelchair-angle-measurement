#pragma once
#include "inclinometer.h"

namespace inclino {
// Six-axis complementary tilt observer, evaluated at the sensor sample rate.
// Gravity constrains tilt; rotation about gravity (absolute yaw) is unobservable.
class TiltFusion {
 public:
  Vector gravity{0,0,G};
  void reset(){ready=false;last=-1;bias={};}
  bool update(Vector accel,Vector gyroDegrees,float now,bool valid){
    const float dt=last<0?0:now-last;last=now;
    if(!valid||!finite(accel)||!finite(gyroDegrees)||norm(accel)<1){reset();return false;}
    const float magnitude=norm(accel);
    Vector observed=unit(accel),omega{};
    for(int i=0;i<3;i++)omega[i]=gyroDegrees[i]/DEG;
    if(!ready||dt<=0||dt>.12f){direction=observed;reference=observed;ready=true;bias={};gravity=scaled(direction,G);return true;}
    // During motion avoid delayed acceleration pulling against gyro prediction.
    const float tau=norm(gyroDegrees)>2?.04f:.5f;
    const float alpha=1-std::exp(-dt/tau);
    for(int i=0;i<3;i++)reference[i]+=alpha*(observed[i]-reference[i]);
    const Vector target=unit(reference);
    // Rotate gravity into the new body frame using Rodrigues (negative gyro).
    Vector rate{};for(int i=0;i<3;i++)rate[i]=omega[i]-bias[i];
    direction=rotate(direction,rate,dt);
    // Downweight accel correction when its magnitude disagrees with gravity.
    const float deviation=std::fabs(magnitude-G);
    const float trust=std::max(0.f,1-deviation/.6f);
    Vector error=cross(target,direction);
    float length=norm(error),dot=0;for(int i=0;i<3;i++)dot+=target[i]*direction[i];
    const float angle=std::atan2(length,dot);
    if(length>1e-7f)error=scaled(error,angle/length);
    else if(dot<0){Vector axis=std::fabs(direction[0])<.8f?Vector{1,0,0}:Vector{0,1,0};error=scaled(unit(cross(axis,direction)),3.14159265359f);}
    // Integral feedback estimates observable gyro bias, never absolute heading.
    if(trust>.8f&&angle<5/DEG&&norm(gyroDegrees)<3){
      for(int i=0;i<3;i++)bias[i]=std::max(-.05f,std::min(.05f,bias[i]-.08f*error[i]*dt));
    }
    direction=unit(rotate(direction,scaled(error,1.5f*trust),dt));
    gravity=scaled(direction,G);return finite(gravity);
  }
 private:
  bool ready=false;float last=-1;Vector direction{0,0,1},reference{0,0,1},bias{};
  static Vector scaled(Vector a,float s){for(float &v:a)v*=s;return a;}
  static Vector unit(Vector a){const float n=norm(a);return n>1e-7f?scaled(a,1/n):Vector{0,0,1};}
  static Vector cross(Vector a,Vector b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
  static Vector rotate(Vector a,Vector rate,float dt){
    const float speed=norm(rate);if(speed<1e-8f)return a;
    Vector axis=scaled(rate,1/speed),c=cross(axis,a);float dot=0;for(int i=0;i<3;i++)dot+=axis[i]*a[i];
    const float angle=-speed*dt,cs=std::cos(angle),sn=std::sin(angle);
    for(int i=0;i<3;i++)a[i]=a[i]*cs+c[i]*sn+axis[i]*dot*(1-cs);
    return a;
  }
};
}
