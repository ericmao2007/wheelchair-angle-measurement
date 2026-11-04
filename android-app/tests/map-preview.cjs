// Browser layout/integration test. Mock phone records are explicitly labelled as demo.
const {chromium}=require('playwright');
const http=require('node:http'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const assets=path.resolve(__dirname,'../app/src/main/assets');
(async()=>{
 const server=http.createServer((req,res)=>{
  const file=path.join(assets,req.url==='/'?'index.html':req.url);
  if(!file.startsWith(assets+path.sep)){res.writeHead(403).end();return;}
  try{res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript':'text/html');res.end(fs.readFileSync(file));}catch(_){res.writeHead(404).end();}
 });
 await new Promise(r=>server.listen(0,'127.0.0.1',r));
 let browser;
 try{
  browser=await chromium.launch({headless:true,executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE||undefined});
  const page=await browser.newPage({viewport:{width:390,height:844},deviceScaleFactor:1,userAgent:'LiveTiltPreview/1.1 (local development)'}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(()=>{
   const records=[{latitude:36.0014,longitude:-78.9382,pitch:1.5,roll:.4},{latitude:36.0020,longitude:-78.9390,pitch:5.2,roll:.8},{latitude:36.0016,longitude:-78.9400,pitch:-9.1,roll:-.3}].map(p=>({...p,accuracy:5,timestamp:Date.now(),referenceId:'demo',calibrated:true}));
   window.LiveTiltAndroid={ready(){},connect(){},send(){},location(){},loadRecords:()=>JSON.stringify(records),saveRecords:()=>true};
  });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.locator('h1').evaluate(el=>el.textContent='Live Tilt · Demo');
  assert.equal(await page.locator('#pointCount').textContent(),'3 points');
  await page.screenshot({path:path.join(__dirname,'gps-measure-preview.png'),fullPage:true});
  await page.locator('#mapTab').click();
  await page.waitForFunction(()=>[...slopeMap.tiles.values()].every(i=>i.complete),{timeout:15000}).catch(()=>{});
  const hit=await page.evaluate(()=>{const h=slopeMap.hits.find(h=>h.p.pitch===-9.1);return {x:h.x,y:h.y};});
  await page.locator('#mapCanvas').click({position:hit});
  assert((await page.locator('#pointDetail').textContent()).includes('-9.10°'));
  assert(await page.locator('#mapPage').isVisible());
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'No horizontal overflow');
  assert.equal(await page.evaluate(()=>slopeMap.hits.length),3,'All demo points projected');
  const tileReport=await page.evaluate(()=>({loaded:[...slopeMap.tiles.values()].filter(i=>i.naturalWidth>0).length,total:slopeMap.tiles.size}));
  await page.screenshot({path:path.join(__dirname,'gps-map-preview.png'),fullPage:true});
  await page.locator('#zoomOut').click();
  await page.reload();
  assert.equal(await page.locator('#pointCount').textContent(),'3 points','Saved records restore on startup');
  assert.deepEqual(errors,[],'No browser script errors');
  console.log(JSON.stringify({browser:'390 × 844',demo_points:3,tiles:tileReport,script_errors:errors,records_restored:true}));
 }finally{if(browser)await browser.close();server.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
