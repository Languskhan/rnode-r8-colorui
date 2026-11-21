// The SD-card file manager page served at http://<board IP>/ (one file, no
// external resources, so it works on a network with no internet).
#pragma once
static const char kWebPage[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>RNode files</title>
<style>
:root{--bg:#0b0f14;--card:#141a21;--card2:#1b232c;--stroke:#26303b;--text:#e8eef4;--sub:#9aa7b4;--muted:#66737f;--accent:#2dd4bf;--red:#f87171}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;gap:12px;padding:16px 20px;border-bottom:1px solid var(--stroke)}
header b{font-size:18px}header span{color:var(--muted);font-size:13px}
main{max-width:860px;margin:0 auto;padding:16px}
.card{background:var(--card);border-radius:14px;padding:14px 16px;margin-bottom:14px}
.tabs{display:flex;gap:8px;margin-bottom:14px}.tabs button.on{background:var(--accent);color:#04241f}
button,.btn{background:var(--card2);color:var(--text);border:1px solid var(--stroke);border-radius:10px;padding:8px 12px;font:inherit;cursor:pointer;text-decoration:none;display:inline-block}
button:hover,.btn:hover{border-color:var(--accent)}button.danger{color:var(--red)}
.row{display:flex;align-items:center;gap:10px;padding:9px 4px;border-top:1px solid var(--stroke)}.row:first-child{border-top:0}
.row .n{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;cursor:default}.row .n.d{cursor:pointer;color:var(--accent)}
.row .s{color:var(--muted);font-size:13px;width:80px;text-align:right}
.crumbs a{color:var(--accent);cursor:pointer}.crumbs{margin-bottom:10px;color:var(--muted)}
.bar{height:6px;background:var(--stroke);border-radius:3px;overflow:hidden;margin-top:6px}.bar i{display:block;height:100%;background:var(--accent)}
.tools{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:10px}
textarea{width:100%;min-height:420px;background:var(--bg);color:var(--text);border:1px solid var(--stroke);border-radius:10px;padding:12px;font:13px/1.45 ui-monospace,Consolas,monospace}
#msg{color:var(--sub);font-size:13px;min-height:18px;margin:6px 0}.quick{display:flex;gap:8px;flex-wrap:wrap}
.hint{color:var(--muted);font-size:13px}
</style></head><body>
<header><svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="#2dd4bf" stroke-width="2" stroke-linecap="round"><path d="M4.9 19.1a10 10 0 0 1 0-14.2M7.8 16.2a6 6 0 0 1 0-8.4M16.2 7.8a6 6 0 0 1 0 8.4M19.1 4.9a10 10 0 0 1 0 14.2"/><circle cx="12" cy="12" r="2"/></svg>
<div><b id="brand">RNode R8 ColorUI</b><br><span id="info">SD card over Wi-Fi</span></div></header>
<main>
<div class="tabs"><button id="t1" class="on" onclick="tab(1)">Files</button><button id="t2" onclick="tab(2)">Settings</button><button id="t3" onclick="tab(3)">Nicknames</button></div>
<section id="p1">
 <div class="card"><div class="quick">
  <button onclick="cd('/logs')">Logs</button><button onclick="cd('/splash')">Splash pictures</button>
  <button onclick="cd('/tiles')">Map tiles</button><button onclick="cd('/')">Top folder</button></div>
  <div id="space" class="hint" style="margin-top:10px"></div><div class="bar"><i id="used"></i></div></div>
 <div class="card">
  <div class="crumbs" id="crumbs"></div>
  <div class="tools">
   <label class="btn">Upload files<input type="file" multiple hidden onchange="up(this.files,false)"></label>
   <label class="btn">Upload a folder<input type="file" webkitdirectory multiple hidden onchange="up(this.files,true)"></label>
   <button onclick="mk()">New folder</button></div>
  <div id="msg"></div><div id="list"></div></div>
 <p class="hint">The screen pauses while files copy. Big map packs are faster with a card reader.</p>
</section>
<section id="p2" hidden><div class="card">
 <p class="hint">settings.ini - see the README for every key. Saving restarts the RNode so it reads the new settings.</p>
 <textarea id="ini" spellcheck="false"></textarea><div id="msg2"></div>
 <button onclick="save('/settings.ini','ini','msg2',true)">Save and restart</button></div></section>
<section id="p3" hidden><div class="card">
 <p class="hint">names.txt - one "&lt;hash&gt; &lt;nickname&gt;" per line. Saving restarts the RNode.</p>
 <textarea id="nam" spellcheck="false"></textarea><div id="msg3"></div>
 <button onclick="save('/names.txt','nam','msg3',true)">Save and restart</button></div></section>
</main>
<script>
let cwd='/';const $=id=>document.getElementById(id);
const H=s=>s.replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';'),E=encodeURIComponent,J=(a,b)=>(a.endsWith('/')?a:a+'/')+b;
function sz(n){return n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':n<1073741824?(n/1048576).toFixed(1)+' MB':(n/1073741824).toFixed(2)+' GB'}
function tab(n){for(let i=1;i<=3;i++){$('p'+i).hidden=i!=n;$('t'+i).className=i==n?'on':''}
 if(n==2)load('/settings.ini','ini');if(n==3)load('/names.txt','nam')}
async function load(p,id){const r=await fetch('/api/get?path='+E(p));$(id).value=r.ok?await r.text():''}
async function save(p,id,m,rs){const fd=new FormData();fd.append('f',new Blob([$(id).value],{type:'text/plain'}),'x');
 $(m).textContent='Saving...';const r=await fetch('/api/upload?path='+E(p),{method:'POST',body:fd});
 if(!r.ok){$(m).textContent='Could not save ('+r.status+')';return}
 if(rs){$(m).textContent='Saved. Restarting the RNode...';fetch('/api/restart',{method:'POST'});setTimeout(()=>location.reload(),9000)}else $(m).textContent='Saved.'}
async function cd(d){cwd=d;const r=await fetch('/api/ls?dir='+E(d));if(!r.ok){$('list').innerHTML='<div class="hint">'+(r.status==404?'This folder does not exist yet.':'Error '+r.status)+'</div>';crumbs();return}
 const j=await r.json();$('space').textContent=sz(j.used)+' used of '+sz(j.total);$('used').style.width=(100*j.used/Math.max(1,j.total))+'%';
 if(j.brand)$('brand').textContent=j.brand;$('info').textContent=j.ver+'  -  '+location.host;
 j.items.sort((a,b)=>(b.d-a.d)||a.n.localeCompare(b.n));let h='';
 for(const it of j.items){const p=J(cwd,it.n);
  if(it.d)h+='<div class="row"><span class="n d" onclick="cd(\''+p.replace(/'/g,"\\'")+'\')">&#128193; '+H(it.n)+'</span><span class="s">folder</span><button class="danger" onclick="rm(\''+p.replace(/'/g,"\\'")+'\',1)">Delete</button></div>';
  else h+='<div class="row"><span class="n">'+H(it.n)+'</span><span class="s">'+sz(it.s)+'</span><a class="btn" href="/api/get?dl=1&path='+E(p)+'">Download</a><button class="danger" onclick="rm(\''+p.replace(/'/g,"\\'")+'\',0)">Delete</button></div>'}
 $('list').innerHTML=h||'<div class="hint">Empty folder</div>';crumbs()}
function crumbs(){const parts=cwd.split('/').filter(x=>x);let h='<a onclick="cd(\'/\')">SD card</a>',p='';
 for(const x of parts){p+='/'+x;h+=' / <a onclick="cd(\''+p.replace(/'/g,"\\'")+'\')">'+H(x)+'</a>'}$('crumbs').innerHTML=h}
async function up(files,folder){const list=[...files];let i=0,bad=0;
 for(const f of list){i++;$('msg').textContent='Uploading '+i+' of '+list.length+': '+f.name;
  const rel=folder?(f.webkitRelativePath||f.name):f.name,fd=new FormData();fd.append('f',f,f.name);
  const r=await fetch('/api/upload?path='+E(J(cwd,rel)),{method:'POST',body:fd});if(!r.ok)bad++}
 $('msg').textContent=bad?bad+' of '+list.length+' failed':'Uploaded '+list.length+' file'+(list.length==1?'':'s');cd(cwd)}
async function rm(p,d){if(!confirm('Delete '+p+(d?' (folder must be empty)':'')+'?'))return;
 const r=await fetch('/api/rm?path='+E(p),{method:'POST'});$('msg').textContent=r.ok?'Deleted':'Could not delete';cd(cwd)}
async function mk(){const n=prompt('New folder name');if(!n)return;await fetch('/api/mkdir?path='+E(J(cwd,n)),{method:'POST'});cd(cwd)}
cd('/');
</script></body></html>)HTML";
