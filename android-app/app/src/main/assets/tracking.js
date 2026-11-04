/* Stability uses every unsmoothed orientation frame, not the 20 Hz display. */
(function(root){
'use strict';
const distance=(a,b)=>{
 const rad=Math.PI/180,dlat=(b.latitude-a.latitude)*rad,dlon=(b.longitude-a.longitude)*rad;
 const h=Math.sin(dlat/2)**2+Math.cos(a.latitude*rad)*Math.cos(b.latitude*rad)*Math.sin(dlon/2)**2;
 return 6371000*2*Math.asin(Math.sqrt(Math.min(1,h)));
};
class StableCapture {
 constructor(){this.reset();}
 reset(){this.samples=[];this.lastTime=null;this.key=null;}
 update(reading,time,key){
  if(!reading||![time,reading.pitch,reading.roll].every(Number.isFinite)){this.reset();return null;}
  if(this.lastTime!==null&&(time<=this.lastTime||time-this.lastTime>750)||key!==this.key)this.reset();
  this.key=key;this.lastTime=time;this.samples.push({time,pitch:reading.pitch,roll:reading.roll});
  // Keep the sample straddling the 5-second boundary, so coverage is never short.
  while(this.samples.length>1&&this.samples[1].time<=time-5000)this.samples.shift();
  if(time-this.samples[0].time<5000||this.samples.length<5)return null;
  const mean=axis=>this.samples.reduce((s,v)=>s+v[axis],0)/this.samples.length;
  const pitch=mean('pitch'),roll=mean('roll');
  if(this.samples.some(s=>Math.abs(s.pitch-pitch)>.2+1e-9||Math.abs(s.roll-roll)>.2+1e-9))return null;
  return {pitch,roll};
 }
}
function freshFix(fix,now){
 return !!fix&&[fix.latitude,fix.longitude,fix.accuracy,fix.receivedAt,fix.ageMs].every(Number.isFinite)
  &&Math.abs(fix.latitude)<=90&&Math.abs(fix.longitude)<=180&&fix.accuracy>=0&&fix.accuracy<=20
  &&fix.ageMs>=0&&now-fix.receivedAt>=0&&fix.ageMs+now-fix.receivedAt<=10000;
}
function differentPoint(last,point){
 return !last||last.referenceId!==point.referenceId||distance(last,point)>=Math.max(10,last.accuracy,point.accuracy)
  ||Math.abs(last.pitch-point.pitch)>=.5||Math.abs(last.roll-point.roll)>=.5;
}
const api={StableCapture,freshFix,differentPoint,distance};
if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.GradientTracking=api;
})(globalThis);
