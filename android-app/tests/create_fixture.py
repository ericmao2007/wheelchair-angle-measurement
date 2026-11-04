from pathlib import Path
import base64,json,struct
root=Path(__file__).resolve().parents[2]
floats=[i+.25 for i in range(30)]
payload=struct.pack('<II30fb7B',257,123456,*floats,-5,61,5,63,5,6,0,5)
fixture=dict(base64=base64.b64encode(payload).decode(),sequence=257,uptime_ms=123456,
             corrected=floats[3:6],fusion_accel=floats[6:9],gyro=floats[9:12],bias=floats[24:27],scale=floats[27:30],temperature=-5)
Path(__file__).with_name('fixture.json').write_text(json.dumps(fixture,indent=2))
