#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <atomic>
#include "inclinometer.h"
#include "bno086_orientation.h"
using namespace inclino;
constexpr char SERVICE[]="12345678-1234-1234-1234-1234567890ab";
constexpr char TELEMETRY[]="abcdefab-1234-5678-1234-abcdefabcdef";
constexpr char METADATA[]="abcdefad-1234-5678-1234-abcdefabcdef";
constexpr char CONTROL[]="abcdefac-1234-5678-1234-abcdefabcdef";
constexpr bool HAS_GYRO=true;
constexpr const char *MODE="STATIC2:ACCGYRO:FUSION6:BNO086:SH2";
// mode is an application protocol tag, not a BNO055 register value.
constexpr uint8_t SENSOR_MODE=6;
Adafruit_BNO08x bno(BNO_RESET);
uint8_t address=BNO_ADDRESS;
Vector latestAccel{},latestGyro{},latestGravity{};
uint32_t attitudeAt=0;bool haveAttitude=false,newAttitude=false,imuCalibrating=false,imuSaved=false;
uint8_t accelAccuracy=0,gyroAccuracy=0,attitudeAccuracy=0;
// The driver retains this pointer while synchronous SH-2 commands service reports.
sh2_SensorValue_t sensorEvent{};
uint32_t accelAt=0,gyroAt=0;
bool haveAccel=false,haveGyro=false,newAccel=false,newGyro=false;
Preferences prefs;Meter meter;Calibration calibration;SixFace six;
bool sensorReady=false,saved=false,calibrating=false;
std::atomic<bool> connected{false},advertise{false};std::atomic<int> command{0};
BLECharacteristic *telemetry;uint32_t sequence=0,lastSample=0,lastTransmit=0,lastRetry=0,lastLog=0;
uint8_t calResult=0;
struct __attribute__((packed)) Frame {
 uint32_t sequence,uptime;
 float raw[3],corrected[3],filtered[3],gyro[3],mean[3];
 float magnitude,noise,drift,roll,pitch,slope,gradeX,gradeY,grade;
 float bias[3],scale[3];
 int8_t temperature;uint8_t flags,state,faces,mode,status,error,calResult;
};
static_assert(sizeof(Frame)==136,"STATIC2 wire format");Frame frame{};
class ServerCallbacks:public BLEServerCallbacks{void onConnect(BLEServer*)override{connected=true;}void onDisconnect(BLEServer*)override{connected=false;advertise=true;}};
class Controls:public BLECharacteristicCallbacks{void onWrite(BLECharacteristic *c)override{
 auto s=c->getValue();if(s=="CALSTART")command=8;else if(s=="SAVE")command=7;else if(s=="CALCANCEL")command=9;else if(s=="CLEAR")command=10;else if(s=="IMUCAL")command=11;else if(s=="SAVEDCD")command=12;
 else if(s.size()==2&&s[0]=='F'&&s[1]>='0'&&s[1]<='5')command=1+s[1]-'0';
}};
bool enableReports(){
 // Bind permanent storage before commands: SH-2 may dispatch sensor callbacks
 // while waiting for calibration/config acknowledgements.
 bno.getSensorEvent(&sensorEvent);
 return sh2_setCalConfig(SH2_CAL_GYRO|(imuCalibrating?SH2_CAL_ACCEL:0))==0
   &&bno.enableReport(SH2_ACCELEROMETER,20000)
   &&bno.enableReport(SH2_GYROSCOPE_CALIBRATED,20000)
   &&bno.enableReport(SH2_GAME_ROTATION_VECTOR,10000);
}
void initializeSensor(){
 sensorReady=false;haveAccel=haveGyro=newAccel=newGyro=haveAttitude=newAttitude=false;
 accelAccuracy=gyroAccuracy=attitudeAccuracy=0;
 for(int i=0;i<2;i++){
  address=i?(BNO_ADDRESS==0x4B?0x4A:0x4B):BNO_ADDRESS;
  if(bno.begin_I2C(address,&Wire)&&enableReports()){sensorReady=true;break;}
 }
 meter.reset();lastSample=millis();
 Serial.printf("BNO086 init=%d address=0x%02X mode=%s units=m/s2,deg/s NVS-six-face=%d\n",sensorReady,address,MODE,saved);
}
void pollSensor(){
 if(!sensorReady)return;
 if(bno.wasReset()){
  haveAccel=haveGyro=newAccel=newGyro=haveAttitude=newAttitude=false;
  accelAccuracy=gyroAccuracy=attitudeAccuracy=0;
  meter.reset();sensorReady=enableReports();
  if(!sensorReady)return;
 }
 auto &event=sensorEvent;
 // Bound work so BLE and the sampling watchdog still run during a backlog.
 for(int i=0;i<16&&bno.getSensorEvent(&event);i++){
  if(event.sensorId==SH2_ACCELEROMETER){
   latestAccel={event.un.accelerometer.x,event.un.accelerometer.y,event.un.accelerometer.z};
   accelAt=millis();haveAccel=newAccel=true;accelAccuracy=event.status&3;
  }else if(event.sensorId==SH2_GYROSCOPE_CALIBRATED){
   // SH-2 gyro reports rad/s; existing fusion and BLE use degrees/s.
   latestGyro={event.un.gyroscope.x*DEG,event.un.gyroscope.y*DEG,event.un.gyroscope.z*DEG};
   gyroAt=millis();haveGyro=newGyro=true;gyroAccuracy=event.status&3;
  }else if(event.sensorId==SH2_GAME_ROTATION_VECTOR){
   const auto &q=event.un.gameRotationVector;
   if(gameGravity(q.real,q.i,q.j,q.k,latestGravity)){
    attitudeAt=millis();haveAttitude=newAttitude=true;attitudeAccuracy=event.status&3;
   }else{haveAttitude=false;}
  }
 }
}
void sendFrame(){
 auto data=reinterpret_cast<const uint8_t*>(&frame);constexpr int count=(sizeof(Frame)+15)/16;
 for(int i=0;i<count&&connected;i++){uint8_t packet[20]={0xA6,uint8_t(frame.sequence),uint8_t(i),count};size_t size=std::min(size_t(16),sizeof(Frame)-i*16);memcpy(packet+4,data+i*16,size);telemetry->setValue(packet,size+4);telemetry->notify();delay(1);}
}
void setup(){
 Serial.begin(115200);Wire.begin(BNO_SDA,BNO_SCL);Wire.setClock(400000);Wire.setTimeOut(50);
 prefs.begin("bno086-static",false);imuSaved=prefs.getBool("dcd-saved",false);Calibration stored;
 if(prefs.getBytesLength("sixaxis-v1")==sizeof(stored)&&prefs.getBytes("sixaxis-v1",&stored,sizeof(stored))==sizeof(stored)&&stored.valid()){calibration=stored;saved=true;}
 initializeSensor();BLEDevice::init("MAO_SLOPE");auto server=BLEDevice::createServer();server->setCallbacks(new ServerCallbacks());auto service=server->createService(SERVICE);
 telemetry=service->createCharacteristic(TELEMETRY,BLECharacteristic::PROPERTY_NOTIFY);telemetry->addDescriptor(new BLE2902());
 auto meta=service->createCharacteristic(METADATA,BLECharacteristic::PROPERTY_READ);meta->setValue(MODE);
 auto control=service->createCharacteristic(CONTROL,BLECharacteristic::PROPERTY_WRITE);control->setCallbacks(new Controls());service->start();
 BLEDevice::getAdvertising()->addServiceUUID(SERVICE);BLEDevice::getAdvertising()->setScanResponse(true);BLEDevice::startAdvertising();
 Serial.println("STATIC2 ready; no automatic tare; commands IMUCAL,SAVEDCD,CALSTART,F0..F5,SAVE,CALCANCEL,CLEAR");
}
void loop(){
 if(advertise.exchange(false))BLEDevice::startAdvertising();
 int cmd=command.exchange(0);
 if(cmd){
  calResult=0;
  if(cmd==11){
   if(sensorReady&&sh2_setCalConfig(SH2_CAL_ACCEL|SH2_CAL_GYRO)==0){imuCalibrating=true;calResult=8;}else calResult=11;
  }else if(cmd==12){
   if(!sensorReady||!haveAttitude||millis()-attitudeAt>=120||accelAccuracy<3||gyroAccuracy<3||meter.state!=Ready)calResult=10;
   else if(sh2_saveDcdNow()!=0)calResult=11;
   else if(sh2_setCalConfig(SH2_CAL_GYRO)!=0)calResult=11;
   else{imuCalibrating=false;imuSaved=true;prefs.putBool("dcd-saved",true);calResult=9;}
  }else if(cmd==8){six=SixFace();calibrating=true;meter.reset();}
  else if(cmd==9){calibrating=false;six=SixFace();meter.reset();calResult=6;}
  else if(cmd==10){if(!prefs.isKey("sixaxis-v1")||prefs.remove("sixaxis-v1")){calibration=Calibration();saved=false;calibrating=false;six=SixFace();meter.reset();calResult=7;}else calResult=4;}
  else if(cmd>=1&&cmd<=6){
   if(!calibrating||meter.state!=Ready)calResult=1;
   else if(!six.capture(cmd-1,meter.rawMean))calResult=2;
   else calResult=5;
  }else if(cmd==7){Calibration candidate;
   if(!calibrating||!six.solve(candidate))calResult=3;
   else if(prefs.putBytes("sixaxis-v1",&candidate,sizeof(candidate))!=sizeof(candidate))calResult=4;
   else{calibration=candidate;saved=true;calibrating=false;meter.reset();calResult=5;}
  }
  Serial.printf("Calibration command=%d result=%u faces=0x%02X saved=%d\n",cmd,calResult,six.mask,saved);
 }
 pollSensor();
 uint32_t now=millis();if(now-lastSample<20){delay(1);return;}
 const bool fresh=sensorReady&&haveAccel&&haveGyro&&haveAttitude&&now-accelAt<120&&now-gyroAt<120&&now-attitudeAt<120;
 // Consume each pair only once; emit an invalid frame when reports stop.
 if(fresh&&!(newAccel&&newGyro&&newAttitude)){delay(1);return;}
 lastSample=now;
 bool ok=fresh;newAccel=newGyro=newAttitude=false;
 Vector raw=latestAccel,gyro=latestGyro;
 uint8_t health[2]={uint8_t(ok?6:0),uint8_t(ok?0:1)};
 // No temperature report requested: INT8_MIN means unavailable.
 constexpr int8_t temp=INT8_MIN;
 // Calibration collection always uses sensor-frame raw means; application is host-owned.
 meter.update(raw,gyro,HAS_GYRO,calibrating?Calibration():calibration,now*.001f,ok);
 // SH-2 owns six-axis attitude. Host six-face coefficients apply only to diagnostic accel.
 const bool fused=ok;
 Vector fusedGravity=mounted(latestGravity);
 const Angles attitude=fused?angles(fusedGravity):Angles();
 frame={};frame.sequence=sequence++;frame.uptime=now;
 for(int i=0;i<3;i++){frame.raw[i]=ok?raw[i]:NAN;frame.gyro[i]=ok&&HAS_GYRO?gyro[i]:NAN;frame.corrected[i]=ok?meter.corrected[i]:NAN;frame.filtered[i]=ok?(fused?fusedGravity[i]:meter.filtered[i]):NAN;frame.mean[i]=meter.state==Ready?meter.mean[i]:NAN;frame.bias[i]=calibration.bias[i];frame.scale[i]=calibration.scale[i];}
 frame.magnitude=ok?meter.magnitude:NAN;frame.noise=meter.noise;frame.drift=meter.drift;
 frame.roll=attitude.roll;frame.pitch=attitude.pitch;frame.slope=attitude.slope;frame.gradeX=attitude.gradeX;frame.gradeY=attitude.gradeY;frame.grade=attitude.grade;
 frame.temperature=int8_t(temp);frame.flags=(ok?1:0)|(calibrating?2:0)|(saved?4:0)|(HAS_GYRO?8:0)|(meter.state==Ready?16:0)|(fused?32:0)|(imuCalibrating?64:0)|(imuSaved?128:0);
 frame.state=meter.state;frame.faces=six.mask;frame.mode=SENSOR_MODE;frame.status=(attitudeAccuracy&3)|((accelAccuracy&3)<<2)|((gyroAccuracy&3)<<4);frame.error=health[1];frame.calResult=calResult;
 if(connected&&now-lastTransmit>=50){lastTransmit=now;sendFrame();}
 if(now-lastLog>=1000){lastLog=now;Serial.printf("STATIC t=%lu ok=%d state=%u norm=%.4f noise=%.5f drift=%.4f roll=%.4f pitch=%.4f slope=%.4f temp=%d status=%u error=%u\n",(unsigned long)now,ok,meter.state,meter.magnitude,meter.noise,meter.drift,attitude.roll,attitude.pitch,attitude.slope,int8_t(temp),frame.status,health[1]);}
 if((!sensorReady||(haveAccel&&now-accelAt>1000)||(haveGyro&&now-gyroAt>1000)||(haveAttitude&&now-attitudeAt>1000)||(!haveAttitude&&now-lastRetry>3000)||(!haveAccel&&now-lastRetry>3000)||(!haveGyro&&now-lastRetry>3000))&&now-lastRetry>3000){lastRetry=now;initializeSensor();}
}
