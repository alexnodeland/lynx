// SPDX-License-Identifier: MIT
// Tiny control page served at "/".
#pragma once

static const char kIndexHtml[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>lynx</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:28rem;margin:2rem auto;padding:0 1rem}
label{display:block;margin:1.2rem 0 .3rem}input[type=range]{width:100%}
button{margin-top:1.5rem;padding:.6rem 1.2rem;font-size:1rem}#s{opacity:.7;font-size:.85rem;margin-top:1rem}
</style></head><body>
<h1>lynx</h1>
<label>Pan <span id="pv">0</span>&deg;</label><input id="pan" type="range" min="-88" max="88" value="0">
<label>Tilt <span id="tv">45</span>&deg;</label><input id="tilt" type="range" min="-58" max="58" value="45">
<button id="c">Centre</button> <button id="w">Sweep</button>
<div id="s"></div>
<script>
const $=id=>document.getElementById(id);let timer=null;
function post(p,b){return fetch(p,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):'{}'});}
function look(){$('pv').textContent=$('pan').value;$('tv').textContent=$('tilt').value;
 clearTimeout(timer);timer=setTimeout(()=>post('/api/look',{pan:+$('pan').value,tilt:+$('tilt').value}),60);}
$('pan').oninput=look;$('tilt').oninput=look;
$('c').onclick=()=>{$('pan').value=0;$('tilt').value=45;look();post('/api/center');};
$('w').onclick=()=>post('/api/sweep');
fetch('/api/status').then(r=>r.json()).then(j=>{
 $('pan').min=j.pan.min;$('pan').max=j.pan.max;$('tilt').min=j.tilt.min;$('tilt').max=j.tilt.max;
 $('pan').value=Math.round(j.pan.target);$('tilt').value=Math.round(j.tilt.target);
 $('pv').textContent=$('pan').value;$('tv').textContent=$('tilt').value;$('s').textContent=j.ip;});
</script></body></html>)HTML";
