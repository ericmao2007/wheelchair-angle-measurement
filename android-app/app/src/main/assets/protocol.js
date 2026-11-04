'use strict';
// STATIC2 little-endian frame. Reassembly and fragment validation happen natively.
globalThis.LiveTiltProtocol = {
 decode(bytes, now=Date.now()/1000) {
  if (!(bytes instanceof Uint8Array) || bytes.length!==136) throw Error('Expected a 136-byte STATIC2 frame');
  const view=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength);
  const f=Array.from({length:30},(_,i)=>{const x=view.getFloat32(8+4*i,true);return Number.isFinite(x)?x:null;});
  const flags=bytes[129];
  const states=['Sensor fault','Moving','Settling','Collecting samples','Unstable','Stable measurement'];
  return {protocol:2,sequence:view.getUint32(0,true),uptime_ms:view.getUint32(4,true),
   accel:f.slice(0,3),corrected:f.slice(3,6),
   filtered_accel:flags&32?[null,null,null]:f.slice(6,9),fusion_accel:flags&32?f.slice(6,9):[null,null,null],
   gyro:f.slice(9,12),mean_accel:f.slice(12,15),magnitude:f[15],noise:f[16],drift:f[17],
   roll:f[18],pitch:f[19],slope:f[20],grade_x:f[21],grade_y:f[22],grade:f[23],
   bias:f.slice(24,27),scale:f.slice(27,30),temperature:view.getInt8(128)===-128?null:view.getInt8(128),
   chip_fusion:bytes[132]===6,imu_calibrating:!!(flags&64),imu_saved:!!(flags&128),
   attitude_accuracy:bytes[133]&3,accel_accuracy:(bytes[133]>>2)&3,gyro_accuracy:(bytes[133]>>4)&3,
   valid:!!(flags&1),calibrating:!!(flags&2),saved:!!(flags&4),has_gyro:!!(flags&8),
   stable:!!(flags&16),has_fusion:!!(flags&32),state:bytes[130],state_text:states[bytes[130]]||'Unknown',
   faces:bytes[131],sensor_mode:bytes[132],sensor_status:bytes[133],sensor_error:bytes[134],calibration_result:bytes[135],
   pc_time:now,simulated:false};
 },
 fromBase64(value) {
  const binary=atob(value);
  return this.decode(Uint8Array.from(binary,c=>c.charCodeAt(0)));
 }
};
