// Run: node android-app/tests/mobile-ui.test.cjs
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.resolve(__dirname,'..'),fixture=JSON.parse(fs.readFileSync(path.join(__dirname,'fixture.json'),'utf8'));
const html=fs.readFileSync(path.join(root,'app/src/main/assets/index.html'),'utf8');
const assert=(ok,msg)=>{if(!ok)throw Error(msg);};
const labels=[],ctx=new Proxy({measureText(t){return {width:t.length*7};},setTransform(){},clearRect(){labels.length=0;},fillText(text,x,y){labels.push({text,x,y});}},{get:(o,k)=>k in o?o[k]:()=>{}});
const nodes=new Map();
function node(id){if(!nodes.has(id))nodes.set(id,{id,textContent:'',hidden:false,disabled:false,style:{},children:[],clientWidth:320,clientHeight:240,width:0,height:0,
 appendChild(child){this.children.push(child);nodes.set(child.id,child);},setAttribute(k,v){this[k]=v;},getContext(){return ctx;},addEventListener(){},setPointerCapture(){},getBoundingClientRect(){return {left:0,top:0};}});return nodes.get(id);}
const commands=[],intervals=[];let clock=0,saved='[]';
const sandbox={scrollTo(){},performance:{now:()=>clock},addEventListener(){},Image:class {},console,Date,Math,Uint8Array,DataView,Array,Number,JSON,Error,
 document:{getElementById:node,createElement:()=>({})},atob:s=>Buffer.from(s,'base64').toString('binary'),
 setInterval(fn,ms){intervals.push({fn,ms});},
 LiveTiltAndroid:{loadRecords(){return saved;},saveRecords(s){saved=s;return true;},location(v){commands.push('GPS:'+v);},ready(){},connect(){commands.push('CONNECT');},send(s){commands.push(s);}},devicePixelRatio:1};
sandbox.window=sandbox;sandbox.globalThis=sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(root,'app/src/main/assets/protocol.js'),'utf8'),sandbox);
const decoded=sandbox.LiveTiltProtocol.fromBase64(fixture.base64);
for(const key of ['sequence','uptime_ms','temperature'])assert(decoded[key]===fixture[key],key+' decoded');
for(const key of ['corrected','fusion_accel','gyro','bias','scale'])assert(JSON.stringify(decoded[key])===JSON.stringify(fixture[key]),key+' decoded');
assert(decoded.has_gyro&&decoded.has_fusion&&decoded.stable&&decoded.saved&&!decoded.calibrating,'Flags decoded');
assert(decoded.filtered_accel.every(x=>x===null),'Fused vector not mislabeled as accel lowpass');
let rejected=false;try{sandbox.LiveTiltProtocol.decode(new Uint8Array(12));}catch(_){rejected=true;}assert(rejected,'Reject wrong frame size');
for(const file of ['tracking.js','reversal.js','map.js'])vm.runInContext(fs.readFileSync(path.join(root,'app/src/main/assets',file),'utf8'),sandbox);
const source=html.match(/<script>\s*([\s\S]*?)<\/script>/)[1];
vm.runInContext(source,sandbox);
assert(vm.runInContext('fmt(.149)',sandbox)==='0.1'&&vm.runInContext('fmt(.151)',sandbox)==='0.2','One decimal rounding');
assert(vm.runInContext('signed(-.01)',sandbox)==='0.0','No negative zero');
assert(intervals.length===1&&intervals[0].ms===50,'Display refresh is 20 Hz');
node('connect').onclick();assert(commands.pop()==='CONNECT','Native Connect button');
sandbox.onNativeState({state:'ready',message:'',metadata:'STATIC2:ACCGYRO:FUSION6'});
sandbox.onNativeFrame(fixture.base64);intervals[0].fn();
assert(node('connection').textContent==='Live · 6-axis','Six-axis status');
assert(node('slope').textContent.startsWith('+'),'Pitch follows positive sensor X');
assert(node('grade').textContent==='+'+(100*6.25/Math.hypot(7.25,8.25)).toFixed(1)+'%','Grade follows X pitch');
const xTilt=sandbox.displayReading([4.903325,0,8.492808]);
const yTilt=sandbox.displayReading([0,4.903325,8.492808]);
assert(Math.abs(xTilt.pitch-30)<.001&&Math.abs(xTilt.roll)<.001,'X tilt changes pitch only');
assert(Math.abs(yTilt.roll+30)<.001&&Math.abs(yTilt.pitch)<.001,'Y tilt changes roll only');
assert(!node('tare').disabled,'Live zero enabled');
node('tare').onclick();assert(node('slope').textContent==='0.0°'&&node('grade').textContent==='0.0%','Zero main angle and grade together');
assert(!node('untare').hidden,'Reset zero available only after zeroing');
const axisX=labels.find(p=>p.text==='X'),axisY=labels.find(p=>p.text==='Y');
assert(axisX.x<axisY.x,'Diagram X/Y screen positions swapped');
node('untare').onclick();assert(node('slope').textContent!=='0.0°','Restore absolute values');
node('calstart').onclick();assert(commands.pop()==='CALSTART','Calibration command uses native BLE');
sandbox.onNativeState({state:'retrying',message:'Sensor disconnected. Reconnecting…',metadata:''});
assert(node('slope').textContent==='—°'&&node('grade').textContent==='—%','Disconnect clears stale measurements');
assert(node('tare').disabled&&node('connect').textContent==='Cancel','Retry cancellation available');
assert(!html.includes('new WebSocket'),'No computer bridge required');
assert(!html.includes('ws://'),'No WebSocket URL');
// Full frame -> stability -> precise location -> persistence -> menu integration.
sandbox.onNativeState({state:'ready',message:'',metadata:''});
node('gpsToggle').onclick();assert(commands.pop()==='GPS:true','GPS opt-in native bridge');
sandbox.onNativeLocation({latitude:36,longitude:-78.9,accuracy:5,ageMs:0,timestamp:Date.now()});
for(clock=0;clock<=5100;clock+=100)sandbox.onNativeFrame(fixture.base64);
assert(JSON.parse(saved).length===1,'Stable live frames save one point');
for(;clock<=8000;clock+=100)sandbox.onNativeFrame(fixture.base64);
assert(JSON.parse(saved).length===1,'Same location and angle are deduplicated');
node('mapTab').onclick();assert(node('measurePage').hidden&&!node('mapPage').hidden,'Second menu page opens map');
node('mapLatest').onclick();assert(node('pointDetail').textContent.includes('36.000000')&&node('pointDetail').textContent.includes('Grade'),'Point details include GPS and grade');
node('measureTab').onclick();node('tare').onclick();
sandbox.onNativeLocation({latitude:36,longitude:-78.9,accuracy:5,ageMs:0,timestamp:Date.now()});
for(;clock<=13300;clock+=100)sandbox.onNativeFrame(fixture.base64);
assert(JSON.parse(saved).length===2&&JSON.parse(saved)[1].relative,'Zero change starts a separate reference');
sandbox.onNativeLocationState('Paused in background');
for(;clock<=17500;clock+=100)sandbox.onNativeFrame(fixture.base64);
assert(JSON.parse(saved).length===2,'No stale/background fix is recorded');
node('gpsToggle').onclick();assert(commands.pop()==='GPS:false','Pause stops native GPS');
sandbox.LiveTiltAndroid.saveRecords=()=>false;
node('gpsToggle').onclick();
sandbox.onNativeLocation({latitude:36.001,longitude:-78.9,accuracy:5,ageMs:0,timestamp:Date.now()});
for(const end=clock+5300;clock<=end;clock+=100)sandbox.onNativeFrame(fixture.base64);
intervals[0].fn();
assert(JSON.parse(saved).length===2&&node('captureStatus').textContent.includes('Could not save'),'Storage failure does not report a successful save');
console.log('GPS integration: stable capture, deduplication, native save, map navigation/detail, zero reference, missing GPS, pause passed.');
// New chip-fusion mode: decode accuracy, expose real SH-2 calibration commands,
// and use the fused vector even when diagnostic accel points elsewhere.
const chipBytes=Buffer.from(fixture.base64,'base64');
chipBytes[129]|=128;chipBytes[132]=6;chipBytes[133]=63;
const chipFrame=chipBytes.toString('base64');
sandbox.onNativeFrame(chipFrame);intervals[0].fn();
assert(!node('imuControls').hidden&&node('calstart').hidden,'Chip calibration replaces diagnostic six-face controls');
assert(node('imuStatus').textContent.includes('High')&&node('imuStatus').textContent.includes('Saved profile'),'Real calibration accuracy displayed');
node('imuStart').onclick();assert(commands.pop()==='IMUCAL','Start chip calibration command');
chipBytes[129]|=64;sandbox.onNativeFrame(chipBytes.toString('base64'));intervals[0].fn();
assert(!node('imuSave').disabled,'High accuracy and stationary permit calibration save');
node('imuSave').onclick();assert(commands.pop()==='SAVEDCD','Save chip DCD command');
chipBytes[133]=0;sandbox.onNativeFrame(chipBytes.toString('base64'));intervals[0].fn();
assert(node('imuSave').disabled,'Unreliable accuracy cannot save chip calibration');
// Reversal integration samples the unsmoothed vector, while display filtering stays active.
let firmwareTime=200000,frameSequence=1000;
function feedPitch(pitch,gyro=0,flags=1|8|16|32|128){
 const bytes=Buffer.from(fixture.base64,'base64');bytes.writeUInt32LE(++frameSequence,0);bytes.writeUInt32LE(firmwareTime+=50,4);
 bytes[129]=flags;bytes[130]=5;bytes[132]=6;bytes[133]=63;bytes[135]=0;
 const p=pitch*Math.PI/180;
 [9.80665*Math.sin(p),0,9.80665*Math.cos(p)].forEach((x,i)=>bytes.writeFloatLE(x,32+4*i));
 [0,gyro,0].forEach((x,i)=>bytes.writeFloatLE(x,44+4*i));
 clock+=50;sandbox.onNativeFrame(bytes.toString('base64'));
}
feedPitch(-8);intervals[0].fn();
node('untare').onclick();
const commandsBefore=commands.length;
node('reverseStart').onclick();
assert(node('tare').disabled,'Do not mix Set zero with pair sampling');
for(let i=0;i<=101;i++)feedPitch(-8);
intervals[0].fn();assert(!node('reverseSecond').hidden,'First stable capture unlocks second');
node('reverseSecond').onclick();feedPitch(12,25); // movement must reset the fresh second window
for(let i=0;i<20;i++)feedPitch(12);
intervals[0].fn();assert(node('reverseApply').hidden,'No apply before five stable seconds');
for(let i=0;i<85;i++)feedPitch(12);
intervals[0].fn();assert(!node('reverseApply').hidden,'Both directions unlock review');
assert(node('reverseStatus').textContent.includes('+2.0°')&&node('reverseStatus').textContent.includes('10.0°'),'Show midpoint bias and half-difference slope');
node('reverseApply').onclick();
assert(node('slope').textContent==='+10.0°','Display subtracts 2-degree bias');
assert(node('grade').textContent==='+17.6%','Grade uses corrected pitch');
assert(Math.abs(sandbox.displayReading().vector[0]-9.80665*Math.sin(10*Math.PI/180))<.0001,'3D vector uses the same correction');
assert(node('processingStatus').textContent.includes('1.5 s average')&&node('processingStatus').textContent.includes('profile saved'),'Calibration and anti-jitter status retained');
assert(commands.length===commandsBefore,'Reversal never writes IMU calibration');
node('tare').onclick();assert(node('slope').textContent==='0.0°','Relative zero operates on corrected vector');
node('untare').onclick();assert(node('slope').textContent==='+10.0°','Reset relative zero preserves reversal bias');
vm.runInContext('storageError=""',sandbox);sandbox.LiveTiltAndroid.saveRecords=s=>{saved=s;return true;};
sandbox.onNativeLocation({latitude:36.002,longitude:-78.9,accuracy:5,ageMs:0,timestamp:Date.now()});
for(let i=0;i<105;i++)feedPitch(12);
const correctedPoint=JSON.parse(saved).at(-1);
assert(Math.abs(correctedPoint.pitch-10)<.001&&correctedPoint.zeroMethod==='reversal'&&correctedPoint.calibrated,'GPS saves corrected angles and IMU saved flag');
assert(Math.abs(correctedPoint.pitchZeroOffset-2)<.001,'GPS retains correction provenance');
node('reverseStart').onclick();node('reverseCancel').onclick();
assert(node('slope').textContent==='+10.0°','Cancel retains old offset');
sandbox.onNativeState({state:'retrying',message:'',metadata:''});
sandbox.onNativeState({state:'ready',message:'',metadata:''});feedPitch(12);intervals[0].fn();
assert(node('reverseClear').hidden&&node('slope').textContent==='+12.0°','Disconnect clears session correction');
function applyPairAgain(){
 feedPitch(-8);node('reverseStart').onclick();for(let i=0;i<102;i++)feedPitch(-8);
 node('reverseSecond').onclick();for(let i=0;i<102;i++)feedPitch(12);
 node('reverseApply').onclick();assert(node('slope').textContent==='+10.0°','Pair reapplies cleanly');
}
applyPairAgain();feedPitch(12,0,1|8|16|32|64|128);intervals[0].fn();
assert(node('reverseClear').hidden&&node('slope').textContent==='+12.0°','Changing chip calibration invalidates reversal only');
assert(node('imuStatus').textContent.includes('Saved profile'),'Existing chip saved record remains visible');
applyPairAgain();firmwareTime=10;feedPitch(12);intervals[0].fn();
assert(node('reverseClear').hidden&&node('slope').textContent==='+12.0°','Device restart invalidates reversal');
console.log('Reversal UI: two stable captures, review/apply/cancel, preserved IMU/filter, Grade/3D/relative zero/GPS and disconnect passed.');
const filterCode=source.split('// BEGIN LIVE VECTOR FILTER')[1].split('// END LIVE VECTOR FILTER')[0];
const filterTests=fs.readFileSync(fs.existsSync(path.join(__dirname,'live_filter_test.js'))?path.join(__dirname,'live_filter_test.js'):path.resolve(root,'../tests/live_filter_test.js'),'utf8');
const filterReport=vm.runInNewContext(filterCode+'\n'+filterTests+'\ntestLiveVectorFilter(LiveVectorFilter)');
console.log('Mobile protocol/UI: decode, flags, 20 Hz refresh, BLE controls, signed grade, zero/reset, diagram axes, disconnect and filter tests passed.');
const fusedReport=vm.runInNewContext(filterCode+`
const f=new LiveVectorFilter();let v;
for(let i=0;i<100;i++)v=f.update([0,0,9.80665],i*.05,'chip',[0,0,0],true);
for(let i=0;i<4;i++)v=f.update([4.903325,0,8.492808],5+i*.05,'chip',[0,90,0],true);
if(Math.abs(Math.atan2(v[0],v[2])*180/Math.PI-30)>.15)throw Error('Chip fused motion lag');
f.update([NaN,0,9],6,'chip',[0,0,0],true);
if(f.value!==null)throw Error('Invalid chip sample not cleared');
const staticFilter=new LiveVectorFilter(),raw=[],smooth=[];let seed=12345;
const random=()=>{seed=(1664525*seed+1013904223)>>>0;return seed/4294967296;};
for(let i=0;i<2000;i++){
 const a=[(random()-.5)*.07,(random()-.5)*.07,9.80665+(random()-.5)*.07];
 const b=staticFilter.update(a,i*.05,'chip',[0,0,0],true);
 if(i>100){raw.push(Math.atan2(a[0],a[2]));smooth.push(Math.atan2(b[0],b[2]));}
}
const sd=xs=>{const mean=xs.reduce((a,b)=>a+b)/xs.length;return Math.sqrt(xs.reduce((a,b)=>a+(b-mean)**2,0)/xs.length);};
const ratio=sd(smooth)/sd(raw);if(ratio>=.6)throw Error('Chip static jitter not sufficiently reduced');
const spike=new LiveVectorFilter();for(let i=0;i<100;i++)spike.update([0,0,9.80665],i*.05,'chip',[0,0,0],true);
if(Math.abs(spike.update([7,0,7],5,'chip',[0,0,0],true)[0])>.001)throw Error('Chip isolated spike leaked');
({chip_static_noise_sd_ratio:ratio,chip_motion_step_tolerance_deg:.15,chip_motion_step_time_s:.15,invalid_resets:true,isolated_spike_rejected:true})
`);
console.log(fusedReport);
console.log(JSON.stringify(filterReport,null,2));
module.exports={passed:true,refresh_ms:intervals[0].ms,filter:filterReport};
