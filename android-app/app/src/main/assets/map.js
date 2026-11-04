/* Small, local canvas slippy map. Only visible OSM tiles are requested. */
(function(root){
'use strict';
class GradientMap {
 constructor(canvas,info,onSelect){
  this.canvas=canvas;this.info=info;this.onSelect=onSelect;this.points=[];this.zoom=17;this.center=null;
  this.tiles=new Map();this.visible=false;this.hits=[];this.failed=false;
  canvas.addEventListener('pointerdown',e=>{this.drag={x:e.clientX,y:e.clientY,c:this.center&&this.center.slice()};canvas.setPointerCapture(e.pointerId);});
  canvas.addEventListener('pointermove',e=>{
   if(!this.drag?.c)return;
   this.center=[this.drag.c[0]-(e.clientX-this.drag.x),this.drag.c[1]-(e.clientY-this.drag.y)];this.draw();
  });
  canvas.addEventListener('pointerup',e=>{
   if(this.drag&&Math.hypot(e.clientX-this.drag.x,e.clientY-this.drag.y)<8){
    const r=canvas.getBoundingClientRect(),x=e.clientX-r.left,y=e.clientY-r.top;
    const h=this.hits.map(p=>({...p,d:Math.hypot(p.x-x,p.y-y)})).sort((a,b)=>a.d-b.d)[0];
    if(h&&h.d<32)this.onSelect(h.p);
   }
   this.drag=null;
  });
  canvas.addEventListener('pointercancel',()=>{this.drag=null;});
  canvas.addEventListener('wheel',e=>{e.preventDefault();this.changeZoom(e.deltaY<0?1:-1);},{passive:false});
  window.addEventListener('resize',()=>this.draw());
 }
 project(p){
  const size=256*2**this.zoom,lat=Math.max(-85.0511,Math.min(85.0511,p.latitude))*Math.PI/180;
  return [(p.longitude+180)/360*size,(1-Math.log(Math.tan(lat)+1/Math.cos(lat))/Math.PI)/2*size];
 }
 setPoints(points){this.points=points;if(!this.center&&points.length)this.focus(points[points.length-1]);else this.draw();}
 focus(p){this.center=this.project(p);this.draw();}
 changeZoom(delta){const next=Math.max(2,Math.min(19,this.zoom+delta));if(this.center)this.center=this.center.map(v=>v*2**(next-this.zoom));this.zoom=next;this.draw();}
 draw(){
  if(!this.visible)return;
  const el=this.canvas,w=el.clientWidth,h=el.clientHeight,d=window.devicePixelRatio||1;
  el.width=Math.round(w*d);el.height=Math.round(h*d);const c=el.getContext('2d');c.setTransform(d,0,0,d,0,0);
  c.fillStyle='#17283c';c.fillRect(0,0,w,h);this.hits=[];
  if(!this.center){this.info.textContent='No points yet. Enable GPS and hold the sensor steady.';return;}
  const size=256*2**this.zoom,n=2**this.zoom;
  this.center[0]=((this.center[0]%size)+size)%size;this.center[1]=Math.max(0,Math.min(size,this.center[1]));
  const left=this.center[0]-w/2,top=this.center[1]-h/2,needed=new Set();
  for(let x=Math.floor(left/256);x<=Math.floor((left+w)/256);x++)for(let y=Math.floor(top/256);y<=Math.floor((top+h)/256);y++){
   if(y<0||y>=n)continue;
   const tx=((x%n)+n)%n,key=`${this.zoom}/${tx}/${y}`;needed.add(key);
   let img=this.tiles.get(key);
   if(!img){
    img=new Image();this.tiles.set(key,img);
    img.onload=()=>this.draw();img.onerror=()=>{this.failed=true;this.draw();};
    img.src=`https://tile.openstreetmap.org/${key}.png`;
   }
   if(img.complete&&img.naturalWidth)c.drawImage(img,x*256-left,y*256-top,256,256);
  }
  if(this.tiles.size>100)for(const key of this.tiles.keys())if(!needed.has(key))this.tiles.delete(key);
  for(const p of this.points){
   const v=this.project(p);let dx=v[0]-this.center[0];dx-=Math.round(dx/size)*size;
   const x=w/2+dx,y=v[1]-top;if(x< -60||x>w+60||y< -30||y>h+30)continue;
   const color=Math.abs(p.pitch)<3?'#77dbc5':Math.abs(p.pitch)<8?'#f3c567':'#fa8c88';
   c.beginPath();c.arc(x,y,9,0,Math.PI*2);c.fillStyle=color;c.fill();c.strokeStyle='#0b1220';c.lineWidth=2;c.stroke();
   c.font='bold 12px system-ui';c.textAlign='left';const label=(Math.round(p.pitch*10)>0?'+':'')+(Math.round(p.pitch*10)/10||0).toFixed(1)+'°';
   c.fillStyle='#0b1220';c.fillRect(x+13,y-12,c.measureText(label).width+10,23);c.fillStyle=color;c.fillText(label,x+18,y+4);
   this.hits.push({x,y,p});
  }
  this.info.textContent=this.failed?'Base map unavailable. Saved points still work; reconnect to the internet and reopen Map.':'Drag to pan · + / − to zoom · tap a point';
 }
}
root.GradientMap=GradientMap;
})(globalThis);
