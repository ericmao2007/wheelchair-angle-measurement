#pragma once
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace inclino {
using Vector=std::array<float,3>;
constexpr float G=9.80665f, DEG=57.2957795131f;
// All static detector/filter tuning lives here. Angles are degrees, gyro deg/s.
struct Config {
  float lowpassSeconds=.35f; // 0 disables the vector low-pass
  float stableSeconds=5.0f, settleSeconds=1.0f, maxGap=.12f;
  float normTolerance=.8f, noiseLimit=.08f, driftLimit=.08f;
  float gyroInstant=5, gyroMean=.3f, gyroRms=1.5f, accelStep=.25f;
};
// Signed permutation: device X/Y/Z = sensor +X/+Y/+Z by default.
constexpr int AXES[3]={1,2,3};
constexpr int axisIndex(int i){return AXES[i]<0?-AXES[i]:AXES[i];}
static_assert(axisIndex(0)>=1&&axisIndex(0)<=3&&axisIndex(1)>=1&&axisIndex(1)<=3&&axisIndex(2)>=1&&axisIndex(2)<=3
              &&axisIndex(0)!=axisIndex(1)&&axisIndex(0)!=axisIndex(2)&&axisIndex(1)!=axisIndex(2),"AXES must be a signed permutation");
inline Vector mounted(Vector a) {
  Vector b{}; for(int i=0;i<3;i++) b[i]=(AXES[i]<0?-1:1)*a[std::abs(AXES[i])-1]; return b;
}
inline float norm(Vector a) {return std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);}
inline Vector sub(Vector a,Vector b){for(int i=0;i<3;i++)a[i]-=b[i];return a;}
inline bool finite(Vector a){return std::isfinite(a[0])&&std::isfinite(a[1])&&std::isfinite(a[2]);}
inline float separation(Vector a,Vector b){
  if(norm(a)*norm(b)<1)return 180;
  // atan2(cross,dot) retains resolution near zero, unlike float acos(dot).
  double x=double(a[1])*b[2]-double(a[2])*b[1],y=double(a[2])*b[0]-double(a[0])*b[2],z=double(a[0])*b[1]-double(a[1])*b[0];
  double d=0;for(int i=0;i<3;i++)d+=double(a[i])*b[i];
  return std::atan2(std::sqrt(x*x+y*y+z*z),d)*DEG;
}
struct Angles { float roll=NAN,pitch=NAN,slope=NAN,gradeX=NAN,gradeY=NAN,grade=NAN; };
inline Angles angles(Vector a) {
  Angles r;if(!finite(a)||norm(a)<1)return r;
  r.roll=std::atan2(a[1],a[2])*DEG; // [-180,180], upside-down near +/-180
  r.pitch=std::atan2(-a[0],std::hypot(a[1],a[2]))*DEG; // [-90,90]
  r.slope=std::atan2(std::hypot(a[0],a[1]),std::fabs(a[2]))*DEG; // plane [0,90]
  float hx=std::hypot(a[1],a[2]),hy=std::hypot(a[0],a[2]);
  if(hx>1e-5f)r.gradeX=100*a[0]/hx;
  if(hy>1e-5f)r.gradeY=100*a[1]/hy;
  if(std::fabs(a[2])>1e-5f)r.grade=100*std::hypot(a[0],a[1])/std::fabs(a[2]);
  return r;
}
struct Calibration {
  uint32_t magic=0x53495831;
  Vector bias{0,0,0},scale{1,1,1};
  bool valid() const {
    if(magic!=0x53495831||!finite(bias)||!finite(scale))return false;
    for(int i=0;i<3;i++)if(std::fabs(bias[i])>1.5f||scale[i]<.8f||scale[i]>1.2f)return false;
    return true;
  }
  Vector apply(Vector a) const {for(int i=0;i<3;i++)a[i]=(a[i]-bias[i])*scale[i];return a;}
};
struct SixFace {
  std::array<Vector,6> faces{}; uint8_t mask=0;
  bool capture(int face,Vector mean) {
    if(face<0||face>5||!finite(mean))return false;
    int axis=face/2;float sign=face%2?-1:1;
    if(sign*mean[axis]<.85f*G||std::fabs(norm(mean)-G)>1)return false;
    for(int i=0;i<3;i++)if(i!=axis&&std::fabs(mean[i])>.2f*G)return false;
    faces[face]=mean;mask|=1<<face;return true;
  }
  bool solve(Calibration &out) const {
    if(mask!=63)return false;
    Calibration c;
    for(int i=0;i<3;i++) {
      float plus=faces[i*2][i],minus=faces[i*2+1][i];
      if(plus-minus<17||plus-minus>22)return false;
      c.bias[i]=(plus+minus)/2;c.scale[i]=2*G/(plus-minus);
    }
    if(!c.valid())return false;
    out=c;return true;
  }
};
enum State:uint8_t { Fault=0, Motion=1, Settling=2, Collecting=3, Unstable=4, Ready=5 };
class Meter {
 public:
  Config cfg; Vector corrected{},filtered{},mean{},rawMean{};
  float magnitude=NAN,noise=NAN,drift=NAN,progress=0;
  State state=Fault; Angles result;
  // Fixed capacity supports 2-5 s at nominal 50 Hz; no heap allocation.
  explicit Meter(Config config=Config()):cfg(config){}
  void reset(){count=0;head=0;warm=false;progress=0;noise=NAN;drift=NAN;state=Collecting;result=Angles();}
  void update(Vector raw,Vector gyro,bool hasGyro,const Calibration &cal,float now,bool ok) {
    float dt=last>=0?now-last:0;last=now;
    if(!ok||!finite(raw)||(hasGyro&&!finite(gyro))){reset();state=Fault;until=now+cfg.settleSeconds;return;}
    corrected=mounted(cal.apply(raw));magnitude=norm(corrected);
    bool gap=dt<=0||dt>cfg.maxGap;
    if(gap){reset();until=now+cfg.settleSeconds;}
    bool moving=std::fabs(magnitude-G)>cfg.normTolerance || (hasGyro&&norm(gyro)>cfg.gyroInstant)
       || (previousValid&&!gap&&norm(sub(corrected,previous))>cfg.accelStep);
    previous=corrected;previousValid=true;
    if(moving){reset();filtered=corrected;until=now+cfg.settleSeconds;state=Motion;return;}
    if(now<until){reset();filtered=corrected;state=Settling;return;}
    if(!warm){filtered=corrected;warm=true;}
    else {float alpha=cfg.lowpassSeconds<=0?1:1-std::exp(-dt/cfg.lowpassSeconds);for(int i=0;i<3;i++)filtered[i]+=alpha*(corrected[i]-filtered[i]);}
    if(count==CAP){ // preserve the requested time window at reduced sample rates too
      head=(head+1)%CAP;count--;
    }
    int end=(head+count)%CAP; samples[end]={now,corrected,filtered,raw,gyro};count++;
    while(count>1&&now-samples[head].t>cfg.stableSeconds+.04f){head=(head+1)%CAP;count--;}
    float elapsed=now-samples[head].t;progress=std::min(1.f,elapsed/cfg.stableSeconds);state=Collecting;
    result=Angles();noise=NAN;drift=NAN;
    if(elapsed<cfg.stableSeconds||count<30)return;
    Vector avg{},gm{},first{},tail{};mean={};rawMean={};float gs=0;int nf=0,nt=0;
    for(int k=0;k<count;k++){auto &s=samples[(head+k)%CAP];
      for(int i=0;i<3;i++){avg[i]+=s.a[i]/count;mean[i]+=s.f[i]/count;rawMean[i]+=s.raw[i]/count;gm[i]+=s.g[i]/count;}
      gs+=norm(s.g)*norm(s.g)/count;
      if(s.t<samples[head].t+cfg.stableSeconds/3){for(int i=0;i<3;i++)first[i]+=s.a[i];nf++;}
      if(s.t>now-cfg.stableSeconds/3){for(int i=0;i<3;i++)tail[i]+=s.a[i];nt++;}
    }
    float variance=0;for(int k=0;k<count;k++){float d=norm(sub(samples[(head+k)%CAP].a,avg));variance+=d*d/count;}
    noise=std::sqrt(variance);for(int i=0;i<3;i++){first[i]/=nf;tail[i]/=nt;}drift=separation(first,tail);
    if(noise>cfg.noiseLimit||drift>cfg.driftLimit||(hasGyro&&(norm(gm)>cfg.gyroMean||std::sqrt(gs)>cfg.gyroRms))){
      // A failed window cannot become valid by retaining part of the disturbance.
      count=0;head=0;warm=false;until=now+cfg.settleSeconds;progress=0;state=Unstable;return;
    }
    state=Ready;result=angles(mean);
  }
 private:
  struct Sample{float t;Vector a,f,raw,g;};
  static constexpr int CAP=260;
  std::array<Sample,CAP> samples{}; int count=0,head=0;
  bool warm=false,previousValid=false;Vector previous{};float last=-1,until=0;
};
}
