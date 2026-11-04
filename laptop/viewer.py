#!/usr/bin/env python3
"""STATIC2 BLE bridge. Raw and static measurements are recorded without browser tare."""
import argparse
import asyncio
import contextlib
import csv
import json
import math
from pathlib import Path
import time
import webbrowser
from aiohttp import web, WSMsgType
from protocol import SERVICE, TELEMETRY, CONTROL, METADATA, Reassembler

LOG_FIELDS = ['pc_time','elapsed_s','simulated','protocol','sequence','uptime_ms','valid','stable','state',
              'calibrating','saved','has_gyro','has_fusion','faces','calibration_result','sensor_mode','sensor_status','sensor_error',
              'roll','pitch','slope','grade_x','grade_y','grade','magnitude','noise','drift','temperature',
              'recovery_s','placement','mark']
VECTORS = ['accel','corrected','filtered_accel','mean_accel','fusion_accel','gyro','bias','scale']

class Viewer:
    def __init__(self, args):
        self.args=args; self.clients=set(); self.client=None
        self.queue=asyncio.Queue(maxsize=4); self.status='Starting'; self.fusion='unknown'
        self.latest=None; self.last_data=0; self.received=0; self.started=time.monotonic()
        self.reassembler=Reassembler(); self.log_file=None
        self.placement=0; self.was_stable=False; self.unstable_since=None
        self.pending_mark=''; self.last_mark=''; self.recovery_s=None
        if args.csv:
            self.log_file=open(args.csv,'x',newline='')
            self.log=csv.DictWriter(self.log_file,fieldnames=LOG_FIELDS+[f'{k}_{i}' for k in VECTORS for i in 'xyz'])
            self.log.writeheader()
        if args.csv:
            config_path=Path(__file__).resolve().parents[1]/'include'/'inclinometer.h'
            Path(str(args.csv)+'.meta.json').write_text(json.dumps(dict(
                firmware_config_source=config_path.read_text() if config_path.exists() else None,
                schema='STATIC2',created=time.time(),simulated=args.demo,
                angle_units='degrees',acceleration_units='m/s2',gyro_units='deg/s',
                note='No absolute accuracy claim; placement IDs are automatic stability episodes; marks identify fixture returns.'),indent=2))

    def accept(self, data):
        now=time.monotonic(); data['fusion']=self.fusion; data['simulated']=self.args.demo
        if data.get('protocol')!=2:
            data=dict(protocol=1,simulated=self.args.demo,valid=False,stable=False,state=0,
                      state_text='Legacy fusion firmware: flash STATIC2 to enable measurements',sequence=data['sequence'],uptime_ms=data['uptime_ms'])
        stable=data.get('stable',False) and data.get('valid',False) and not data.get('calibrating',False)
        if not stable and self.unstable_since is None:self.unstable_since=now
        if stable and not self.was_stable:
            self.placement+=1
            self.recovery_s=now-self.unstable_since if self.unstable_since is not None else None
            self.unstable_since=None
        self.was_stable=stable
        data.update(pc_time=time.time(),elapsed_s=now-self.started,placement=self.placement,
                    recovery_s=self.recovery_s if stable else None,mark=self.pending_mark)
        if self.log_file:
            row={k:data.get(k) for k in LOG_FIELDS}
            for k in VECTORS:
                for i,axis in enumerate('xyz'):row[f'{k}_{axis}']=(data.get(k) or [None]*3)[i]
            self.log.writerow(row)
            if self.received%10==0 or self.pending_mark:self.log_file.flush()
        if self.pending_mark:self.last_mark=self.pending_mark
        self.pending_mark='';data['last_mark']=self.last_mark
        self.last_data=now;self.latest=data;self.received+=1
        if self.queue.full():self.queue.get_nowait()
        self.queue.put_nowait(data)

    async def broadcast(self,message):
        async def send(ws):
            try:await asyncio.wait_for(ws.send_json(message),1)
            except (ConnectionError,RuntimeError,asyncio.TimeoutError):self.clients.discard(ws)
        await asyncio.gather(*(send(ws) for ws in tuple(self.clients)))

    async def publish(self):
        while True:
            try:await self.broadcast(dict(type='data',**await asyncio.wait_for(self.queue.get(),.5)))
            except asyncio.TimeoutError:pass
            age=time.monotonic()-self.last_data if self.last_data else None
            await self.broadcast(dict(type='status',message=self.status,age=age,received=self.received,
                                      incomplete=self.reassembler.incomplete,mode='SIMULATED' if self.args.demo else 'LIVE BLE',logging=bool(self.log_file)))

    async def websocket(self,request):
        if request.headers.get('Origin')!=f'http://127.0.0.1:{self.args.port}':raise web.HTTPForbidden()
        ws=web.WebSocketResponse(heartbeat=20);await ws.prepare(request);self.clients.add(ws)
        if self.latest:await ws.send_json(dict(type='data',**self.latest))
        try:
            async for msg in ws:
                if msg.type!=WSMsgType.TEXT:continue
                if msg.data.startswith('MARK:'):
                    self.pending_mark=msg.data[5:85].replace('\n',' ').replace('\r',' ')
                    await ws.send_json(dict(type='notice',message='Marker will be logged with the next frame: '+self.pending_mark));continue
                if msg.data not in ('CALSTART','CALCANCEL','SAVE','CLEAR','F0','F1','F2','F3','F4','F5'):continue
                if self.args.demo or not self.client or not self.client.is_connected:
                    await ws.send_json(dict(type='notice',message='No hardware connection'));continue
                if not self.fusion.startswith('STATIC2:'):
                    await ws.send_json(dict(type='notice',message='Flash STATIC2 firmware first'));continue
                try:
                    await self.client.write_gatt_char(CONTROL,msg.data.encode(),response=True)
                    await ws.send_json(dict(type='notice',message='Sent '+msg.data+'; check the device calibration status for the result'))
                except Exception as e:await ws.send_json(dict(type='notice',message=str(e)))
        finally:self.clients.discard(ws)
        return ws

    async def ble(self):
        from bleak import BleakClient,BleakScanner
        while True:
            try:
                self.status='Scanning for '+self.args.name
                devices=await BleakScanner.discover(timeout=6,return_adv=True,service_uuids=[SERVICE])
                candidates=[d for d,a in devices.values() if d.name==self.args.name or a.local_name==self.args.name]
                if not candidates:self.status='Device not found; retrying';await asyncio.sleep(2);continue
                async with BleakClient(candidates[0],timeout=20) as client:
                    self.client=client;self.reassembler=Reassembler();self.was_stable=False
                    self.unstable_since=None
                    self.fusion=(await client.read_gatt_char(METADATA)).decode('ascii').strip() if client.services.get_characteristic(METADATA) else 'legacy'
                    def notification(_,packet):
                        d=self.reassembler.feed(packet)
                        if d is not None:self.accept(d)
                    await client.start_notify(TELEMETRY,notification)
                    self.status='Connected to MAO_SLOPE · '+self.fusion
                    while client.is_connected:await asyncio.sleep(.5)
            except Exception as e:
                self.status=f'Bluetooth: {e}. Retrying';print(self.status,flush=True);await asyncio.sleep(3)
            finally:self.client=None

    async def demo(self):
        self.status='SIMULATED static measurement; not hardware validation';seq=0
        while True:
            t=time.monotonic()-self.started;s=5+.015*math.sin(t)
            a=[9.80665*math.sin(math.radians(s)),0,9.80665*math.cos(math.radians(s))]
            self.accept(dict(protocol=2,sequence=seq,uptime_ms=int(t*1000),valid=True,stable=t>4,
                state=5 if t>4 else 3,state_text='Demo: stable' if t>4 else 'Demo: waiting for stability',
                accel=a,corrected=a,filtered_accel=a,mean_accel=a if t>4 else [None]*3,
                gyro=[None]*3,has_gyro=False,roll=0 if t>4 else None,pitch=-s if t>4 else None,
                slope=s if t>4 else None,grade_x=100*math.tan(math.radians(s)) if t>4 else None,
                grade_y=0,grade=100*math.tan(math.radians(s)) if t>4 else None,
                magnitude=9.80665,noise=.025,drift=.02,temperature=25,saved=False,calibrating=False,
                faces=0,bias=[0]*3,scale=[1]*3,sensor_mode=1,sensor_status=6,sensor_error=0,calibration_result=0))
            seq+=1;await asyncio.sleep(.1)

async def main(args):
    viewer=Viewer(args);app=web.Application()
    async def index(_):return web.FileResponse(Path(__file__).with_name('index.html'))
    app.router.add_get('/',index);app.router.add_get('/ws',viewer.websocket)
    runner=web.AppRunner(app);await runner.setup();tasks=[]
    try:
        await web.TCPSite(runner,'127.0.0.1',args.port).start()
        url=f'http://127.0.0.1:{args.port}';print('Viewer: '+url,flush=True)
        if not args.no_browser:webbrowser.open(url)
        tasks=[asyncio.create_task(viewer.publish()),asyncio.create_task(viewer.demo() if args.demo else viewer.ble())]
        await asyncio.gather(*tasks)
    finally:
        for task in tasks:task.cancel()
        await asyncio.gather(*tasks,return_exceptions=True)
        if viewer.log_file:viewer.log_file.close()
        await runner.cleanup()

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--demo',action='store_true');p.add_argument('--no-browser',action='store_true')
    p.add_argument('--name',default='MAO_SLOPE');p.add_argument('--port',type=int,default=8765)
    p.add_argument('--csv',help='Continuous raw + static CSV log (e.g. 30 minutes); Ctrl+C flushes and closes')
    with contextlib.suppress(KeyboardInterrupt):asyncio.run(main(p.parse_args()))
