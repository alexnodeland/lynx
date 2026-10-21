// SPDX-License-Identifier: MIT
// The two pages the device serves: setup (on its hotspot) and the dashboard (on your network).
// Self-contained: no external fonts, scripts or images, so they work with no internet.
#pragma once

namespace lynx {

static const char kStyleCss[] = R"CSS(
:root{--bg:#0c0d10;--ink:#f3f1ec;--muted:#a9a8a3;--dim:#6d6c69;--accent:#7fd6a4;--edge:rgba(255,255,255,.08);--glass:rgba(22,23,27,.8)}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Inter,sans-serif}
main{max-width:760px;margin:0 auto;padding:28px 16px 60px}
h1{font-size:30px;letter-spacing:-.03em;margin:0 0 4px}h2{font-size:17px;margin:0 0 12px}
.brand{display:flex;align-items:center;gap:10px;font-weight:700;font-size:20px;margin-bottom:24px}
.brand i{width:20px;height:20px;border-radius:50%;border:3px solid var(--accent);display:grid;place-items:center}
.brand i:after{content:"";width:6px;height:6px;border-radius:50%;background:var(--accent)}
.brand small{font:500 12px ui-monospace,Menlo,monospace;color:var(--dim);margin-left:auto}
.card{background:linear-gradient(160deg,rgba(255,255,255,.06),rgba(255,255,255,.01) 45%),var(--glass);border-radius:16px;padding:20px;margin:0 0 16px;
 box-shadow:inset 0 1px 0 rgba(255,255,255,.14),0 0 0 1px var(--edge),0 20px 40px -20px rgba(0,0,0,.7)}
p{color:var(--muted);margin:0 0 12px}.mono,code,pre{font-family:ui-monospace,Menlo,monospace;font-size:12.5px}
pre{background:#08090b;border-radius:10px;padding:12px;overflow-x:auto;color:#d8d6cf;margin:0 0 10px;white-space:pre-wrap;word-break:break-all}
label{display:block;font-size:13px;color:var(--muted);margin:10px 0 6px}
input,select{width:100%;padding:11px 12px;border-radius:10px;border:1px solid var(--edge);background:#08090b;color:var(--ink);font-size:15px}
input[type=range]{padding:0;accent-color:var(--accent)}
button,.btn{appearance:none;border:0;cursor:pointer;display:inline-block;text-decoration:none;padding:11px 18px;border-radius:11px;font-family:inherit;font-weight:600;font-size:14px;color:var(--ink);
 background:linear-gradient(160deg,rgba(255,255,255,.12),rgba(255,255,255,.03));box-shadow:inset 0 1px 0 rgba(255,255,255,.2),0 0 0 1px var(--edge),0 6px 16px -8px #000}
button.go{background:linear-gradient(160deg,rgba(127,214,164,.38),rgba(127,214,164,.12));}
button:active{transform:translateY(1px)}.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.net{display:flex;justify-content:space-between;padding:10px 12px;border-radius:10px;cursor:pointer}.net:hover,.net.on{background:rgba(127,214,164,.1)}
.ok{color:var(--accent)}.bad{color:#ff8a7a}.hide{display:none}
.view{position:relative;aspect-ratio:4/3;background:#000;border-radius:12px;overflow:hidden;margin-bottom:14px}
.view img{width:100%;height:100%;object-fit:contain;display:block}.view .msg{position:absolute;inset:0;display:grid;place-items:center;color:var(--dim)}.view .msg.hide{display:none}
.copy{float:right;padding:4px 10px;font-size:12px}
.view canvas{position:absolute;inset:0;width:100%;height:100%;pointer-events:none}
)CSS";

static const char kSetupHtml[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>lynx setup</title><link rel="stylesheet" href="/lynx.css"></head>
<body><main>
<div class="brand"><i></i>lynx <small id="ver"></small></div>
<section id="s1" class="card">
  <h1>Set up your lynx</h1>
  <p>Pick the Wi-Fi network lynx should join. It needs 2.4 GHz; 5 GHz-only networks won't appear.</p>
  <div id="nets"><p class="mono">scanning…</p></div>
  <label for="ssid">Network</label><input id="ssid" autocomplete="off" autocapitalize="off">
  <label for="pass">Password</label><input id="pass" type="password" autocomplete="off">
  <div class="row" style="margin-top:16px"><button class="go" id="join">Join network</button><button id="rescan">Scan again</button></div>
</section>
<section id="s2" class="card hide">
  <h2 id="st">Joining…</h2><p id="sp">This takes up to 20 seconds.</p>
</section>
<section id="s3" class="card hide">
  <h1 class="ok">lynx is online</h1>
  <p>Save this token now. Apps, scripts and AI agents need it to control the camera.</p>
  <label>Device token <button class="copy" data-c="tok">copy</button></label><pre id="tok"></pre>
  <label>Address</label><pre id="addr"></pre>
  <h2 style="margin-top:18px">Next</h2>
  <p>1. Reconnect this phone or computer to <b id="ssid2"></b>.<br>2. Open <b id="url"></b> and paste the token.<br>
  3. This setup hotspot closes by itself in three minutes.</p>
  <p class="mono">Lost the token later? Hold the BOOT button on the board for 3 seconds to reopen this hotspot.</p>
</section>
</main><script>
const $=id=>document.getElementById(id);
async function scan(){ $('nets').innerHTML='<p class="mono">scanning…</p>';
  try{const r=await (await fetch('/setup/scan')).json(); $('nets').innerHTML='';
    r.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{const d=document.createElement('div');d.className='net';
      d.innerHTML=`<span></span><span class="mono">${n.open?'open · ':''}${n.rssi} dBm</span>`;d.firstChild.textContent=n.ssid;
      d.onclick=()=>{$('ssid').value=n.ssid;document.querySelectorAll('.net').forEach(x=>x.classList.toggle('on',x===d));$('pass').focus()};$('nets').appendChild(d)});
    if(!r.networks.length) $('nets').innerHTML='<p>No networks found. Type the name below.</p>';
  }catch(e){$('nets').innerHTML='<p class="bad">Scan failed. Type the name below.</p>'}}
async function poll(){ const r=await (await fetch('/setup/state')).json();
  if(r.state==='connected'){ $('s2').classList.add('hide');$('s3').classList.remove('hide');
    $('tok').textContent=r.token; $('addr').textContent=`http://${r.mdns}/\nhttp://${r.ip}/`; $('ssid2').textContent=r.ssid; $('url').textContent=`http://${r.mdns}/`; return}
  if(r.state==='failed'){ $('st').textContent="Couldn't join "+r.ssid; $('st').className='bad'; $('sp').textContent='Check the password and that the network is 2.4 GHz.';
    $('s1').classList.remove('hide'); return}
  setTimeout(poll,1000)}
$('join').onclick=async()=>{ const ssid=$('ssid').value.trim(); if(!ssid) return $('ssid').focus();
  $('s1').classList.add('hide');$('s2').classList.remove('hide');$('st').className='';$('st').textContent='Joining '+ssid+'…';$('sp').textContent='This takes up to 20 seconds.';
  await fetch('/setup/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,pass:$('pass').value})}); setTimeout(poll,1500)};
$('rescan').onclick=scan;
document.querySelectorAll('.copy').forEach(b=>b.onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($(b.dataset.c).textContent));
fetch('/api/info').then(r=>r.json()).then(i=>$('ver').textContent=i.name+' · v'+i.version);
fetch('/setup/state').then(r=>r.json()).then(r=>{ if(r.state==='connected') poll(); else scan() });
</script></body></html>)HTML";

static const char kDashboardHtml[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>lynx</title><link rel="stylesheet" href="/lynx.css"></head>
<body><main>
<div class="brand"><i></i>lynx <small id="ver"></small></div>
<section id="gate" class="card hide">
  <h1>Connect to this lynx</h1>
  <p>Enter the device token. It was shown when you set the camera up.</p>
  <label for="t">Token</label><input id="t" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="32 characters">
  <div class="row" style="margin-top:14px"><button class="go" id="save">Connect</button><span id="err" class="bad"></span></div>
  <p class="mono" style="margin-top:16px">No token? Hold the BOOT button on the board for 3 seconds, join the
  <b class="name"></b> Wi-Fi hotspot it opens, and the setup page will show it.</p>
</section>
<div id="app" class="hide">
<section class="card">
  <div class="view"><img id="live" alt="live view"><canvas id="boxes"></canvas><div class="msg" id="vmsg">connecting to the stream…</div></div>
  <label>Pan <span id="pv" class="mono"></span></label><input id="pan" type="range" min="-88" max="88" step="1">
  <label>Tilt <span id="tv" class="mono"></span></label><input id="tilt" type="range" min="-58" max="58" step="1">
  <div class="row" style="margin-top:14px"><button id="center">Rest pose</button><button id="sweep">Sweep</button>
  <a class="btn" id="snap" target="_blank">Snapshot</a><button id="logout" style="margin-left:auto">Forget token</button></div>
</section>
<section class="card">
  <h2>Detection <span class="mono" id="dsum" style="color:var(--dim);font-weight:400"></span></h2>
  <p>On-device: the board finds faces (or cat faces) itself. Nothing is identified or sent anywhere.</p>
  <div class="row"><select id="dmodel" style="width:auto"><option value="off">Off</option><option value="face">Faces</option><option value="cat">Cat faces</option></select>
  <label style="display:flex;gap:8px;align-items:center;margin:0"><input type="checkbox" id="follow" style="width:auto"> Follow the nearest face</label></div>
</section>
<section class="card">
  <h2>Connect an AI agent (MCP)</h2>
  <p>lynx speaks the Model Context Protocol at <code>/mcp</code>. Tools: status, look, center, sweep, snapshot, stream_url, set_camera, detect, follow.</p>
  <label>Claude Code <button class="copy" data-c="mcp1">copy</button></label><pre id="mcp1"></pre>
  <label>Other clients (JSON config) <button class="copy" data-c="mcp2">copy</button></label><pre id="mcp2"></pre>
</section>
<section class="card">
  <h2>HTTP API</h2>
  <p>Every call takes <code>Authorization: Bearer &lt;token&gt;</code>; image URLs also accept <code>?token=</code>.</p>
  <label>Move <button class="copy" data-c="c1">copy</button></label><pre id="c1"></pre>
  <label>Status, rest pose, snapshot <button class="copy" data-c="c2">copy</button></label><pre id="c2"></pre>
  <label>Live video (MJPEG: browsers, VLC, ffmpeg, Home Assistant) <button class="copy" data-c="c3">copy</button></label><pre id="c3"></pre>
  <p class="mono">Full list: GET /api/info</p>
</section>
</div>
</main><script>
const $=id=>document.getElementById(id), H=location.host, HN=location.hostname;
let tok=''; try{tok=localStorage.getItem('lynx-token')||''}catch(e){}
const api=(p,o={})=>fetch(p,{...o,headers:{...(o.headers||{}),'Authorization':'Bearer '+tok,'Content-Type':'application/json'}});
async function check(){ const r=await api('/api/status'); return r.ok?r.json():null }
function fill(){
  const base=`http://${H}`, stream=`http://${HN}:81/stream?token=${tok}`;
  $('mcp1').textContent=`claude mcp add --transport http lynx ${base}/mcp --header "Authorization: Bearer ${tok}"`;
  $('mcp2').textContent=JSON.stringify({mcpServers:{lynx:{type:'http',url:`${base}/mcp`,headers:{Authorization:`Bearer ${tok}`}}}},null,2);
  $('c1').textContent=`curl -X POST ${base}/api/position -H "Authorization: Bearer ${tok}" -d '{"pan":30,"tilt":45}'`;
  $('c2').textContent=`curl ${base}/api/status -H "Authorization: Bearer ${tok}"\ncurl -X POST ${base}/api/center -H "Authorization: Bearer ${tok}"\ncurl -o lynx.jpg "${base}/snapshot.jpg?token=${tok}"`;
  $('c3').textContent=stream; $('snap').href=`/snapshot.jpg?token=${tok}`;
  const img=$('live'); const seen=setInterval(()=>{ if(img.naturalWidth){ $('vmsg').classList.add('hide'); } },300);  // MJPEG: onload is unreliable, watch for the first frame
  img.onerror=()=>{$('vmsg').textContent='stream busy or unavailable (one viewer at a time) — retrying…';$('vmsg').classList.remove('hide');setTimeout(()=>img.src=stream+'&r='+Date.now(),3000)};
  img.src=stream }
let dTimer=0;
function drawBoxes(d){ const c=$('boxes'), img=$('live'), r=c.getBoundingClientRect(); c.width=r.width*devicePixelRatio; c.height=r.height*devicePixelRatio;
  const x=c.getContext('2d'); x.clearRect(0,0,c.width,c.height); if(!d||!img.naturalWidth) return;
  const k=Math.min(c.width/img.naturalWidth,c.height/img.naturalHeight), iw=img.naturalWidth*k, ih=img.naturalHeight*k, ox=(c.width-iw)/2, oy=(c.height-ih)/2;
  x.lineWidth=2*devicePixelRatio; x.strokeStyle='#7fd6a4'; x.fillStyle='#7fd6a4'; x.font=`${11*devicePixelRatio}px ui-monospace,monospace`;
  for(const o of d.objects||[]){ const [bx,by,bw,bh]=o.box; x.strokeRect(ox+bx*iw,oy+by*ih,bw*iw,bh*ih); x.fillText(`${o.label} ${Math.round(o.score*100)}%`,ox+bx*iw,oy+by*ih-4*devicePixelRatio) } }
async function pollDetect(){ clearTimeout(dTimer); try{ const d=await (await api('/api/detections')).json();
  $('dmodel').value=d.enabled?d.model:'off'; $('follow').checked=!!d.follow;
  $('dsum').textContent=d.enabled?`· ${(d.objects||[]).length} in view · ${d.fps.toFixed(1)} fps`:''; drawBoxes(d.enabled?d:null);
  dTimer=setTimeout(pollDetect,d.enabled?300:3000) }catch(e){ dTimer=setTimeout(pollDetect,3000) } }
function show(s){ $('gate').classList.add('hide'); $('app').classList.remove('hide'); pollDetect();
  $('pan').value=Math.round(s.pan.target); $('tilt').value=Math.round(s.tilt.target); lbl(); fill() }
function lbl(){ $('pv').textContent=$('pan').value+'°'; $('tv').textContent=$('tilt').value+'°' }
let tmr=0; function send(){ lbl(); clearTimeout(tmr); tmr=setTimeout(()=>api('/api/position',{method:'POST',body:JSON.stringify({pan:+$('pan').value,tilt:+$('tilt').value})}),60) }
$('pan').oninput=$('tilt').oninput=send;
$('center').onclick=async()=>{const s=await (await api('/api/center',{method:'POST'})).json(); $('pan').value=Math.round(s.pan.target); $('tilt').value=Math.round(s.tilt.target); lbl()};
$('sweep').onclick=()=>api('/api/sweep',{method:'POST'});
$('dmodel').onchange=async()=>{ const v=$('dmodel').value; await api('/api/detect',{method:'POST',body:JSON.stringify(v==='off'?{enabled:false}:{enabled:true,model:v})}); pollDetect() };
$('follow').onchange=async()=>{ await api('/api/follow',{method:'POST',body:JSON.stringify({enabled:$('follow').checked})}); pollDetect() };
$('save').onclick=async()=>{ tok=$('t').value.trim(); const s=await check();
  if(!s){ $('err').textContent='That token was not accepted.'; return }
  try{localStorage.setItem('lynx-token',tok)}catch(e){} show(s) };
$('logout').onclick=()=>{ try{localStorage.removeItem('lynx-token')}catch(e){} location.reload() };
document.querySelectorAll('.copy').forEach(b=>b.onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($(b.dataset.c).textContent));
fetch('/api/info').then(r=>r.json()).then(i=>{ $('ver').textContent=i.name+' · v'+i.version; document.querySelectorAll('.name').forEach(e=>e.textContent=i.name) });
(async()=>{ const s=tok?await check():null; if(s) show(s); else $('gate').classList.remove('hide') })();
</script></body></html>)HTML";

}  // namespace lynx
