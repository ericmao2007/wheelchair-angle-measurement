// Run against the LiveVectorFilter class extracted from laptop/index.html.
function testLiveVectorFilter(Filter) {
 const check=(ok,message)=>{if(!ok)throw Error(message);};
 const sd=xs=>{const m=xs.reduce((a,b)=>a+b,0)/xs.length;return Math.sqrt(xs.reduce((s,x)=>s+(x-m)**2,0)/xs.length);};
 let seed=12345;const random=()=>{seed=(1664525*seed+1013904223)>>>0;return seed/4294967296;};
 const tilt=a=>Math.atan2(a[0],a[2])*180/Math.PI;
 const filter=new Filter(),raw=[],smooth=[];
 for(let i=0;i<1000;i++){
  const a=[.5+(random()-.5)*.07,(random()-.5)*.07,9.79+(random()-.5)*.07];
  const b=filter.update(a,i*.1);
  if(i>100){raw.push(tilt(a));smooth.push(tilt(b));}
 }
 const ratio=sd(smooth)/sd(raw);check(ratio<.2,'Stationary noise must fall by at least 80%');
 const target=[4.903325,0,8.492808];let settled=null;
 for(let i=0;i<20;i++){const b=filter.update(target,100+i*.1);if(settled===null&&Math.abs(tilt(b)-30)<.3)settled=i*.1;}
 check(settled!==null&&settled<=.9,'Sustained 30-degree step must settle within 0.9 seconds');
 const spike=new Filter();for(let i=0;i<100;i++)spike.update([0,0,9.80665],i*.1);
 check(Math.abs(tilt(spike.update([7,0,7],10)))<.01,'Reject isolated spike');
 check(Math.abs(tilt(spike.update([0,0,9.80665],10.1)))<.01,'Reject spike tail');
 const flipped=spike.update([0,0,-9.80665],12);check(flipped[2]<0,'Reset after data gap');
 check(spike.update([NaN,0,9.8],12.1)===null,'Reject invalid sample');
 check(spike.update([0,0,9.8],0)[2]>0,'Recover immediately after invalid sample and reboot');
 spike.update([2,0,9],.1,'new calibration');check(spike.value[0]===2,'Reset on calibration change');
 // Gradual real changes must continue to appear; do not freeze small tilts.
 const slow=new Filter();for(let i=0;i<100;i++)slow.update([0,0,9.80665],i*.1);
 let end;for(let i=0;i<200;i++){const r=(i/199)*Math.PI/180;end=slow.update([9.80665*Math.sin(r),0,9.80665*Math.cos(r)],10+i*.1);}
 check(tilt(end)>.85,'Follow slow 1-degree ramp without deadband');
 const six=new Filter();for(let i=0;i<100;i++)six.update([0,0,9.80665],i*.1,'six',[0,0,0]);
 let fast;for(let i=0;i<3;i++)fast=six.update(target,10+i*.1,'six',[0,90,0]);
 check(Math.abs(tilt(fast)-30)<.3,'Gyro motion must bypass the slow stationary window');
 return {noise_sd_ratio:ratio,synthetic_noise_reduction_percent:100*(1-ratio),step_30deg_settle_s:settled,slow_ramp_final_deg:tilt(end)};
}
