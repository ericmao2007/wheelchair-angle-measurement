const assert=require('node:assert/strict');
const {ReversalZero,reading}=require('../app/src/main/assets/reversal.js');
const vector=(pitch,roll=0)=>{const p=pitch*Math.PI/180,r=roll*Math.PI/180,g=9.80665;return [g*Math.sin(p),-g*Math.cos(p)*Math.sin(r),g*Math.cos(p)*Math.cos(r)];};
const capture=(r,pitch,start=0,roll=0)=>{for(let i=0;i<=100;i++)r.update(vector(pitch,roll),[0,0,0],start+i*50);};
const early=new ReversalZero();early.start();for(let i=0;i<=60;i++)early.update(vector(8),[0,0,0],i*50);assert.equal(early.stage,'first','3 seconds no longer enough');
const r=new ReversalZero();r.start();capture(r,-8);assert.equal(r.stage,'turn');
r.opposite();capture(r,12,7000);assert.equal(r.stage,'review');assert(Math.abs(r.candidate.offset-2)<1e-10);assert(Math.abs(r.candidate.slope-10)<1e-10);
assert(r.apply());assert(Math.abs(reading(r.correct(vector(-8))).pitch+10)<1e-10);assert(Math.abs(reading(r.correct(vector(12))).pitch-10)<1e-10);
assert(Math.abs(reading(r.correct(vector(12,3))).roll-3)<1e-10,'Keep transverse roll');
r.start();capture(r,50,10000);r.cancel();assert(Math.abs(r.offset-2)<1e-10);assert(r.applied,'Cancel preserves old applied correction');
r.clear();assert.equal(r.offset,0);assert(!r.applied);
// Same signs are allowed on shallow slopes with a larger offset; true flat also works.
r.start();capture(r,3);r.opposite();capture(r,1,7000);assert(Math.abs(r.candidate.offset-2)<1e-10);assert(Math.abs(r.candidate.slope-1)<1e-10);
r.clear();r.start();capture(r,2);r.opposite();capture(r,2,7000);assert.equal(r.candidate.slope,0);
// A held/duplicated frame, motion, invalid data, gaps and upright checks cannot fabricate stability.
r.clear();r.start();for(let i=0;i<100;i++)r.update(vector(8),[0,0,0],0);assert.equal(r.stage,'first');
r.update(vector(8),[0,4,0],100);assert.equal(r.progress,0);
r.update(vector(8),[0,0,0],200,false);assert.equal(r.progress,0);
r.update([NaN,0,9],[0,0,0],300);assert.equal(r.samples.length,0);
r.update([0,0,-9.8],[0,0,0],400);assert.equal(r.samples.length,0);
r.update(vector(89),[0,0,0],500);assert.equal(r.samples.length,0);
for(let i=0;i<30;i++)r.update(vector(8),[0,0,0],1000+i*50);
r.update(vector(8),[0,0,0],3000);assert.equal(r.samples.length,1,'Gap restarts stability');
for(let i=1;i<=30;i++)r.update(vector(i%2?8:9),[0,0,0],3000+i*50);assert.equal(r.stage,'first','Jitter must not pass');
capture(r,8,5000);assert.equal(r.stage,'turn','Recover after motion');
console.log('Reversal: bias/slope math, both signs, flat, corrected vector/Roll, cancel, duplicates, gyro, jitter, gaps and invalid input passed.');
