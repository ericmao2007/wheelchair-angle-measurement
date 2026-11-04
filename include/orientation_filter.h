#pragma once
#include <cmath>
#include <algorithm>
struct Quaternion {
  float w=1, x=0, y=0, z=0;
  float dot(const Quaternion &b) const { return w*b.w+x*b.x+y*b.y+z*b.z; }
  bool normalize() {
    float n=dot(*this);
    if (!std::isfinite(n) || n<0.25f || n>2.25f) return false;
    float r=1/std::sqrt(n); w*=r; x*=r; y*=r; z*=r; return true;
  }
  Quaternion negated() const { return {-w,-x,-y,-z}; }
};
inline float angleBetween(const Quaternion &a, const Quaternion &b) {
  double dot=double(a.w)*b.w+double(a.x)*b.x+double(a.y)*b.y+double(a.z)*b.z;
  double aa=double(a.w)*a.w+double(a.x)*a.x+double(a.y)*a.y+double(a.z)*a.z;
  double bb=double(b.w)*b.w+double(b.x)*b.x+double(b.y)*b.y+double(b.z)*b.z;
  return float(2*std::acos(std::min(1.0,std::fabs(dot)/std::sqrt(aa*bb))));
}
inline float slopeDegrees(const Quaternion &q) {
  float nx=2*(q.x*q.z+q.w*q.y), ny=2*(q.y*q.z-q.w*q.x);
  float nz=1-2*(q.x*q.x+q.y*q.y);
  return std::atan2(std::sqrt(nx*nx+ny*ny),std::fabs(nz))*180/3.14159265358979323846f;
}
class OrientationFilter {
 public:
  Quaternion value;
  bool rejected=false;
  float residualDegrees=0;
  bool update(Quaternion q, float dt, float gyroRadPerSec) {
    rejected=false;
    if (!q.normalize()) { rejected=true; return false; }
    dt=std::max(0.001f,std::min(dt,0.1f));
    if (!ready) { value=last=q; ready=true; return true; }
    // Isolated gyro-inconsistent jumps are rejected; sustained changes are
    // accepted on the third sample so fusion corrections cannot freeze output.
    float jump=angleBetween(last,q);
    float gate=0.14f + 3*gyroRadPerSec*dt;
    if (jump>gate && ++outliers<3) { rejected=true; return true; }
    outliers=0; last=q;
    if (value.dot(q)<0) q=q.negated();
    // Adaptive quaternion low-pass: 0.7 Hz at rest, up to 12 Hz in motion.
    float speed=angleBetween(value,q)/dt;
    float cutoff=std::min(12.0f,0.7f+1.5f*speed);
    float alpha=1-std::exp(-2*3.14159265358979323846f*cutoff*dt);
    value={value.w+alpha*(q.w-value.w),value.x+alpha*(q.x-value.x),
           value.y+alpha*(q.y-value.y),value.z+alpha*(q.z-value.z)};
    value.normalize();
    float residual=angleBetween(value,q)*180/3.14159265358979323846f;
    residualSquared += (1-std::exp(-dt/2))*(residual*residual-residualSquared);
    residualDegrees=std::sqrt(std::max(0.0f,residualSquared));
    return true;
  }
 private:
  Quaternion last;
  bool ready=false;
  unsigned outliers=0;
  float residualSquared=0;
};
