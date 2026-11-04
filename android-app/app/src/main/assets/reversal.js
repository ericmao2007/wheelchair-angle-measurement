/* Two opposite headings on the same contact face: p1=s+b, p2=-s+b.
 * Estimates a constant Pitch zero bias; does not calibrate scale or prove yaw. */
(function(root){
'use strict';
const DEG=180/Math.PI;
function reading(a){
 if(!Array.isArray(a)||a.length!==3||!a.every(Number.isFinite)||Math.hypot(...a)<1)return null;
 const [x,y,z]=a;
 return {pitch:Math.atan2(x,Math.hypot(y,z))*DEG,roll:-Math.atan2(y,z)*DEG,up:z>0};
}
class ReversalZero {
 constructor(){this.clear();}
 get busy(){return ['first','turn','second','review'].includes(this.stage);}
 clear(){this.applied=false;this.offset=0;this.result=null;this.cancel();}
 cancel(){this.stage='idle';this.first=null;this.second=null;this.samples=[];this.last=null;this.progress=0;this.reason='';}
 start(){this.cancel();this.stage='first';}
 opposite(){if(this.stage!=='turn')return false;this.stage='second';this.samples=[];this.last=null;this.progress=0;return true;}
 interrupt(reason){this.samples=[];this.last=null;this.progress=0;this.reason=reason;}
 update(vector,gyro,time,valid=true){
  if(this.stage!=='first'&&this.stage!=='second')return;
  const r=reading(vector);
  if(!valid||!r||!Number.isFinite(time)){this.interrupt('Waiting for valid, calibrated sensor data');return;}
  if(!r.up||Math.abs(r.pitch)>=80||Math.abs(r.roll)>=80){this.interrupt('Keep the same face down, away from vertical');return;}
  if(!Array.isArray(gyro)||gyro.length!==3||!gyro.every(Number.isFinite)||Math.hypot(...gyro)>1){this.interrupt('Hold still: rotation detected');return;}
  if(this.last!==null&&time<=this.last)return; // Duplicate BLE frames cannot advance sampling.
  if(this.last!==null&&time-this.last>250)this.interrupt('Data gap: collecting a new stable window');
  this.last=time;this.samples.push({time,pitch:r.pitch,roll:r.roll});
  while(this.samples.length>1&&this.samples[1].time<=time-5000)this.samples.shift();
  const mean=k=>this.samples.reduce((s,v)=>s+v[k],0)/this.samples.length;
  const pitch=mean('pitch'),roll=mean('roll');
  if(this.samples.some(s=>Math.abs(s.pitch-pitch)>.2||Math.abs(s.roll-roll)>.2)){
   this.samples=[{time,pitch:r.pitch,roll:r.roll}];this.progress=0;this.reason='Movement detected: restarting 5-second window';return;
  }
  this.reason='';this.progress=Math.min(1,(time-this.samples[0].time)/5000);
  if(this.progress<1||this.samples.length<30)return;
  const captured={pitch,roll,count:this.samples.length};
  if(this.stage==='first'){this.first=captured;this.stage='turn';}
  else{this.second=captured;this.stage='review';}
  this.samples=[];this.last=null;this.progress=0;
 }
 get candidate(){return this.first&&this.second?{offset:(this.first.pitch+this.second.pitch)/2,slope:Math.abs(this.first.pitch-this.second.pitch)/2}:null;}
 apply(){
  if(this.stage!=='review'||!this.candidate)return false;
  const c=this.candidate;this.offset=c.offset;this.applied=true;this.result={...c,first:{...this.first},second:{...this.second}};this.cancel();return true;
 }
 correct(vector){
  if(!this.applied)return vector;
  const r=reading(vector);if(!r)return null;
  const p=(r.pitch-this.offset)/DEG,roll=r.roll/DEG,n=Math.hypot(...vector);
  // Reconstruct one corrected vector for angle, Grade, 3D, relative zero and GPS.
  return [n*Math.sin(p),-n*Math.cos(p)*Math.sin(roll),n*Math.cos(p)*Math.cos(roll)];
 }
}
const api={ReversalZero,reading};
if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.ReversalCalibration=api;
})(globalThis);
