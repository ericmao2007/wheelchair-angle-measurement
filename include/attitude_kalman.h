#pragma once
#include "orientation_filter.h"

struct Vec3 {
  float x=0,y=0,z=0;
  float norm() const { return std::sqrt(x*x+y*y+z*z); }
  float operator[](int i) const { return i==0?x:(i==1?y:z); }
};
inline Quaternion multiply(const Quaternion &a,const Quaternion &b) {
  return {a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z,
          a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
          a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
          a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w};
}
inline Quaternion rotationVector(Vec3 v) {
  float angle=v.norm(), scale=angle<1e-6f ? 0.5f : std::sin(angle/2)/angle;
  return {std::cos(angle/2),v.x*scale,v.y*scale,v.z*scale};
}
inline Vec3 gravityDirection(const Quaternion &q) {
  // World +Z expressed in the body frame (third row of body-to-world R).
  return {2*(q.x*q.z-q.w*q.y),2*(q.y*q.z+q.w*q.x),1-2*(q.x*q.x+q.y*q.y)};
}

// Six-state multiplicative error-state EKF: body attitude error and gyro bias.
// Quaternion propagation uses raw gyro; tilt updates use normalized raw accel.
// BNO055 fused gravity is the low-noise tilt reference during quiet calibrated
// operation; raw acceleration is used otherwise when motion gates permit.
// These measurements share sensors and are not statistically independent.
class AttitudeKalman {
 public:
  Quaternion value;
  Vec3 bias;
  bool accelerationHeld=false, rejected=false, reacquired=false;
  float residualDegrees=0;
  bool update(Quaternion fused, Vec3 gyro, Vec3 accel, Vec3 linear, float dt,
              bool accelCalibrated, bool headingCalibrated) {
    rejected=false; reacquired=false;
    if (!fused.normalize() || !std::isfinite(gyro.norm()) ||
        !std::isfinite(accel.norm()) || !std::isfinite(linear.norm()) ||
        !std::isfinite(dt) || dt<=0 || accel.norm()<1) return false;
    if (!ready || dt>0.2f) {
      // Unknown sampling gaps cannot be reconstructed by one current gyro sample.
      reset(fused); previousAccel=accel; previousGravity=gravityDirection(value);
    }
    dt=std::max(0.001f,std::min(dt,0.2f));
    Vec3 omega{gyro.x-bias.x,gyro.y-bias.y,gyro.z-bias.z};
    value=multiply(value,rotationVector({omega.x*dt,omega.y*dt,omega.z*dt}));
    value.normalize();
    float F[6][6]={};
    for (int i=0;i<6;i++) F[i][i]=1;
    float skew[3][3]={{0,-omega.z,omega.y},{omega.z,0,-omega.x},{-omega.y,omega.x,0}};
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) F[i][j]-=skew[i][j]*dt;
    for(int i=0;i<3;i++) F[i][i+3]=-dt;
    float temp[6][6]={}, next[6][6]={};
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) for(int k=0;k<6;k++) temp[i][j]+=F[i][k]*P[k][j];
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) for(int k=0;k<6;k++) next[i][j]+=temp[i][k]*F[j][k];
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) P[i][j]=next[i][j];
    // Tuning values are engineering defaults, not BNO055 accuracy guarantees.
    for(int i=0;i<3;i++) { P[i][i]+=0.000025f*dt; P[i+3][i+3]+=0.00000001f*dt; }
    Vec3 g=gravityDirection(value);
    float norm=accel.norm();
    Vec3 change{accel.x-previousAccel.x,accel.y-previousAccel.y,accel.z-previousAccel.z};
    // Remove predicted gravity change from jerk so real rotation is not treated
    // as linear acceleration solely because body-frame gravity rotates.
    Vec3 jerk{change.x/dt-9.80665f*(g.x-previousGravity.x)/dt,
              change.y/dt-9.80665f*(g.y-previousGravity.y)/dt,
              change.z/dt-9.80665f*(g.z-previousGravity.z)/dt};
    previousAccel=accel; previousGravity=g;
    float jerkAlpha=1-std::exp(-dt/0.12f);
    smoothedJerk.x+=jerkAlpha*(jerk.x-smoothedJerk.x);
    smoothedJerk.y+=jerkAlpha*(jerk.y-smoothedJerk.y);
    smoothedJerk.z+=jerkAlpha*(jerk.z-smoothedJerk.z);
    float deviation=std::fabs(norm-9.80665f);
    bool dynamic=deviation>0.5f || linear.norm()>0.5f || smoothedJerk.norm()>6.0f || jerk.norm()>30.0f;
    if(dynamic) recovery=0.25f; else recovery=std::max(0.0f,recovery-dt);
    accelerationHeld=dynamic || recovery>0;
    bool tiltQuiet=!accelerationHeld && gyro.norm()<0.035f
        && linear.norm()<0.2f && deviation<0.3f && smoothedJerk.norm()<2;
    bool biasQuiet=accelCalibrated && tiltQuiet;
    biasStill=biasQuiet?biasStill+dt:0;
    learnBias=biasStill>2;
    if(!accelCalibrated) bias={};
    if(!accelerationHeld) {
      // Hardware A/B measurements showed fused tilt substantially quieter than
      // instantaneous raw accel. Use that reference only outside acceleration
      // disturbances; select one tilt observation, never double-count both.
      bool fusedTiltReference=tiltQuiet;
      float R=(fusedTiltReference?0.0000025f*(accelCalibrated?1:4):0.0001f*(accelCalibrated?1:25))
          *(1+deviation*deviation*100+linear.norm()*linear.norm()*100);
      Vec3 observed=fusedTiltReference?gravityDirection(fused):Vec3{accel.x/norm,accel.y/norm,accel.z/norm};
      float H[3][6]={{0,-g.z,g.y,0,0,0},{g.z,0,-g.x,0,0,0},{-g.y,g.x,0,0,0,0}};
      float residual[]={observed.x-g.x,observed.y-g.y,observed.z-g.z};
      bool accepted=correct(H,residual,R,3,16.3f); // 99.9% chi-square gate for 3 residuals.
      // The linearized gravity update has no useful tangent correction at
      // exactly 180 degrees. Allow nonlinear recovery after quiet data even
      // before full calibration, provided fused gravity agrees with raw accel.
      // This recovers an estimate, not the sensor calibration or stored tare.
      Vec3 rawGravity{accel.x/norm,accel.y/norm,accel.z/norm};
      Vec3 fusedGravity=gravityDirection(fused);
      float agreement=rawGravity.x*fusedGravity.x+rawGravity.y*fusedGravity.y+rawGravity.z*fusedGravity.z;
      bool quiet=tiltQuiet && agreement>0.98f;
      quietRejected=(!accepted && quiet)?quietRejected+dt:0;
      if(quietRejected>=1.0f) {
        Vec3 axis{observed.y*g.z-observed.z*g.y,observed.z*g.x-observed.x*g.z,observed.x*g.y-observed.y*g.x};
        float length=axis.norm(),dot=observed.x*g.x+observed.y*g.y+observed.z*g.z;
        if(length>1e-6f) {
          float scale=std::atan2(length,dot)/length;
          Quaternion recovered=multiply(value,rotationVector({axis.x*scale,axis.y*scale,axis.z*scale}));
          recovered.normalize(); reset(recovered); rejected=false; reacquired=true;
          previousGravity=gravityDirection(value);
        } else if(dot<0) {
          // Antipodal directions do not specify a unique rotation axis. The
          // agreeing fused quaternion is a valid orientation to reacquire.
          reset(fused); rejected=false; reacquired=true;
          previousGravity=gravityDirection(value);
        }
        quietRejected=0;
      }
    }
    if(accelerationHeld) quietRejected=0;
    // A calibrated fusion heading provides a slow magnetic reference; no raw
    // fused tilt is reintroduced. Skip it during acceleration disturbances.
    if(headingCalibrated && !accelerationHeld) {
      g=gravityDirection(value);
      Quaternion error=multiply({value.w,-value.x,-value.y,-value.z},fused);
      if(error.w<0) error=error.negated();
      float twist=2*std::atan2(error.x*g.x+error.y*g.y+error.z*g.z,error.w);
      float H[3][6]={{g.x,g.y,g.z,0,0,0},{},{}};
      float residual[]={twist,0,0};
      correct(H,residual,0.0012f,1,10.8f);
    }
    float residual=angleBetween(value,fused)*180/3.14159265358979323846f;
    residualSquared+=(1-std::exp(-dt/2))*(residual*residual-residualSquared);
    residualDegrees=std::sqrt(std::max(0.0f,residualSquared));
    return true;
  }
 private:
  bool ready=false;
  float P[6][6]={}, recovery=0, residualSquared=0, quietRejected=0;
  Vec3 previousAccel,previousGravity,smoothedJerk;
  float biasStill=0;
  bool learnBias=false;
  void reset(Quaternion q) {
    value=q; bias={}; recovery=0; ready=true; smoothedJerk={}; biasStill=0; learnBias=false;
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) P[i][j]=i==j?(i<3?0.01f:0.0001f):0;
  }
  bool correct(const float H[3][6],const float residual[3],float R,int count,float gate) {
    float PH[6][3]={},S[3][3]={},aug[3][6]={};
    for(int i=0;i<6;i++) for(int j=0;j<count;j++) for(int k=0;k<6;k++) PH[i][j]+=P[i][k]*H[j][k];
    for(int i=0;i<count;i++) for(int j=0;j<count;j++) {
      for(int k=0;k<6;k++) S[i][j]+=H[i][k]*PH[k][j];
      if(i==j) S[i][j]+=R;
      aug[i][j]=S[i][j];
    }
    for(int i=0;i<count;i++) aug[i][i+count]=1;
    for(int col=0;col<count;col++) {
      int pivot=col;
      for(int row=col+1;row<count;row++) if(std::fabs(aug[row][col])>std::fabs(aug[pivot][col])) pivot=row;
      if(std::fabs(aug[pivot][col])<1e-12f) { rejected=true; return false; }
      for(int j=0;j<2*count;j++) std::swap(aug[col][j],aug[pivot][j]);
      float divisor=aug[col][col];
      for(int j=0;j<2*count;j++) aug[col][j]/=divisor;
      for(int row=0;row<count;row++) if(row!=col) {
        float scale=aug[row][col];
        for(int j=0;j<2*count;j++) aug[row][j]-=scale*aug[col][j];
      }
    }
    float mahalanobis=0;
    for(int i=0;i<count;i++) for(int j=0;j<count;j++) mahalanobis+=residual[i]*aug[i][j+count]*residual[j];
    if(!std::isfinite(mahalanobis) || mahalanobis>gate) { rejected=true; return false; }
    float K[6][3]={},delta[6]={},A[6][6]={},temp[6][6]={},next[6][6]={};
    for(int i=0;i<6;i++) for(int j=0;j<count;j++) {
      for(int k=0;k<count;k++) K[i][j]+=PH[i][k]*aug[k][j+count];
      delta[i]+=K[i][j]*residual[j];
    }
    // Bias is only observable with trusted, still measurements. Freeze bias
    // gain during motion or incomplete accel calibration to avoid learning
    // transient acceleration as an angular-rate offset.
    if(!learnBias) {
      for(int i=3;i<6;i++) { delta[i]=0; for(int j=0;j<count;j++) K[i][j]=0; }
    }
    value=multiply(value,rotationVector({delta[0],delta[1],delta[2]})); value.normalize();
    bias.x=std::max(-0.0174533f,std::min(0.0174533f,bias.x+delta[3]));
    bias.y=std::max(-0.0174533f,std::min(0.0174533f,bias.y+delta[4]));
    bias.z=std::max(-0.0174533f,std::min(0.0174533f,bias.z+delta[5]));
    // Joseph covariance update avoids subtractive loss of positive definiteness.
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) {
      A[i][j]=i==j?1:0;
      for(int k=0;k<count;k++) A[i][j]-=K[i][k]*H[k][j];
    }
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) for(int k=0;k<6;k++) temp[i][j]+=A[i][k]*P[k][j];
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) {
      for(int k=0;k<6;k++) next[i][j]+=temp[i][k]*A[j][k];
      for(int k=0;k<count;k++) next[i][j]+=R*K[i][k]*K[j][k];
    }
    // First-order multiplicative error-state reset after injecting rotation.
    float G[6][6]={},resetSkew[3][3]={{0,-delta[2],delta[1]},{delta[2],0,-delta[0]},{-delta[1],delta[0],0}};
    for(int i=0;i<6;i++) G[i][i]=1;
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) G[i][j]-=0.5f*resetSkew[i][j];
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) {
      temp[i][j]=0; for(int k=0;k<6;k++) temp[i][j]+=G[i][k]*next[k][j];
    }
    float resetP[6][6]={};
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) for(int k=0;k<6;k++) resetP[i][j]+=temp[i][k]*G[j][k];
    for(int i=0;i<6;i++) for(int j=0;j<6;j++) P[i][j]=0.5f*(resetP[i][j]+resetP[j][i]);
    return true;
  }
};
