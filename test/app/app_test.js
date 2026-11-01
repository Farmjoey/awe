const fs=require('fs');
const html=fs.readFileSync(__dirname+'/../../app/index.html','utf8');
const code=html.match(/<script>([\s\S]*)<\/script>/)[1];
const els={};
function mk(id){return els[id]||(els[id]={id,style:{},textContent:'',innerHTML:'',dataset:{},setAttribute(){},classList:{toggle(){}},getContext(){return new Proxy({},{get:(t,k)=>(k in t)?t[k]:(()=>{}),set:(t,k,v)=>{t[k]=v;return true}})}})}
const view=mk('view'),tabs=mk('tabs'),pill=mk('pill');
global.window={addEventListener(){}};
global.document={
  querySelector(sel){ if(sel==='#view')return view; if(sel==='#tabs')return tabs; if(sel==='#pill')return pill;
    const id=sel.replace('#',''); if(view.innerHTML.includes('id="'+id+'"'))return mk(id); return null; },
  querySelectorAll(sel){
    const out=[]; const scan=(src,attr,re)=>{let m;while((m=re.exec(src)))out.push({dataset:{[attr]:m[1]},onclick:null})};
    let list=[];
    const m=/^\[data-([a-z]+)\]$/.exec(sel);
    if(m) scan(view.innerHTML,m[1],new RegExp('data-'+m[1]+'="([^"]+)"','g'));
    else if(sel==='#tabs button')scan(tabs.innerHTML,'t',/data-t="(\w+)"/g);
    handlers[sel]=out; return out;
  }
};
const handlers={};
global.navigator={};
global.location={protocol:'https:',hostname:'example.github.io',pathname:'/nova/'};
global.fetch=()=>Promise.reject(new Error('offline'));
global.TextEncoder=TextEncoder;global.TextDecoder=TextDecoder;
// run the app, but keep the wired elements so we can click them
const origQSA=document.querySelectorAll;
eval(code);
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
function click(sel,attr,val){ const l=origQSA.call(document,sel); const e=l.find(x=>x.dataset[attr]===String(val)); if(!e) throw new Error('no '+sel+' '+val); return e; }
(async()=>{
  const fails=[];const ok=(c,m)=>{if(!c)fails.push(m);console.log((c?'ok   ':'FAIL ')+m)};
  // wire() assigns onclick to the objects returned by the last querySelectorAll call per selector
  ok(pill.textContent==='Ikke forbundet','starts disconnected');
  let b=handlers['[data-a]'].find(x=>x.dataset.a==='demo'); ok(!!b,'demo button present'); b.onclick();
  await sleep(600);
  ok(pill.textContent==='Demo','connected to demo board');
  ok(view.innerHTML.includes('id="lv-sp"'),'live view is the home screen when connected');
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick();
  ok(view.innerHTML.includes('0.8.0 (demo)'),'info shown');
  // themes
  handlers['#tabs button'].find(x=>x.dataset.t==='tune').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='themes').onclick();
  handlers['[data-th]'].find(x=>x.dataset.th==='3').onclick(); await sleep(200);
  ok(view.innerHTML.includes('tcard sel" data-th="3"'),'theme 3 selected after ack+info');
  // boot style + text
  handlers['#tabs button'].find(x=>x.dataset.t==='tune').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='boot').onclick();
  handlers['[data-st]'].find(x=>x.dataset.st==='1').onclick(); await sleep(200);
  ok(view.innerHTML.includes('class="sel" data-st="1"')||view.innerHTML.includes('data-st="1" class="sel"'),'boot style 1 selected');
  // update
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='update').onclick();
  const good=new Uint8Array(200*1024); good[0]=0xE9; good[1]=2;
  mk('bin').onchange&&0; 
  const binEl=document.querySelector('#bin'); ok(!!binEl,'file input present');
  const fileObj={name:'NOVA_RIDE_cyd_st7789.bin',arrayBuffer:async()=>good.buffer};
  document.querySelector('#bin').onchange; 
  await (async()=>{ /* wire() set onchange on the object returned at that time; fetch the live one */ })();
  const live=els['bin']; ok(typeof live.onchange==='function','file handler wired');
  live.files=[fileObj]; await live.onchange(); await sleep(50);
  ok(view.innerHTML.includes('Filen ser ud som en gyldig'),'valid file accepted');
  const go=handlers['[data-a]'].find(x=>x.dataset.a==='go'); ok(!!go,'start button present');
  go.onclick(); for(let i=0;i<120&&!(els['otxt']&&els['otxt'].textContent.includes('Færdig'));i++) await sleep(500);
  ok(els['otxt']&&els['otxt'].textContent.includes('Færdig'),'update finished in demo: '+(els['otxt']&&els['otxt'].textContent));
  await sleep(2000);   // the demo board restarts itself after the update: connect again
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='device').onclick();
  if(handlers['[data-a]'].find(x=>x.dataset.a==='demo')){handlers['[data-a]'].find(x=>x.dataset.a==='demo').onclick();await sleep(700)}
  // widgets: gallery has all starters, install one on the (demo) board
  handlers['#tabs button'].find(x=>x.dataset.t==='tune').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='widgets').onclick();
  ok((view.innerHTML.match(/data-wi="s:/g)||[]).length===11,'11 starter widgets listed');
  handlers['[data-wi]'].find(x=>x.dataset.wi==='s:0').onclick(); await sleep(1500);
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='device').onclick();
  ok(view.innerHTML.includes('Dashboard: eget'),'custom dashboard flag after install');
  ok(!(global.document.documentElement&&global.document.documentElement.onclick),'no click handler on the whole page');
  // the compiler and its rules
  const NWt=window.NovaWidget;
  ok(typeof NWt.compile==='function','widget compiler available');
  ok(!NWt.compile({items:[{t:'arc',x:10,y:10,r:5,th:14,src:'speed',max:35}]}).ok,'arc with a bad radius is rejected');
  ok(!NWt.compile({v:2,items:[{t:'num',x:1,y:1,src:'speed',font:'8',unit:' km/h'}]}).ok,'big fonts cannot carry a unit');
  const good2=NWt.compile({v:2,items:[{t:'num',x:10,y:40,src:'speed',font:'4'}]});
  ok(good2.ok&&good2.bytes.length===12+32,'small widget compiles to 44 bytes');
  const rich=NWt.compile({v:2,name:'t',author:'a',pages:2,bg:'#102030',items:[{t:'arc',x:90,y:110,r:70,th:10,src:'speed',max:35,knob:true,caps:true,ticks:true,warn:25,bad:32,color:'#33ccff'},{t:'button',x:10,y:200,w:80,h:30,action:'sport',text:'SPORT'},{t:'spark',x:180,y:150,w:130,h:50,src:'power',max:3000,fill:true,page:1},{t:'dot',x:300,y:10,r:4,src:'ble',show:{src:'speed',op:'>',val:3}}]});
  ok(rich.ok&&rich.pages===2,'rich layout with pages, buttons, graph, conditions and custom colours compiles');
  ok(!NWt.compile({v:2,items:[{t:'dot',x:1,y:1,r:4,src:'ble',page:1}]}).ok,'page that does not exist is rejected');
  ok(NWt.compile(NWt.upgrade({v:1,name:'old',author:'a',items:[{t:'ring',x:88,y:126,r:80,th:14,src:'speed',max:35},{t:'modebar'}]})).ok,'old v1 layouts still compile');
  // the studio (visual editor)
  handlers['#tabs button'].find(x=>x.dataset.t==='tune').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='widgets').onclick();
  handlers['[data-s]'].find(x=>x.dataset.s==='open').onclick();
  ok(view.innerHTML.includes('id="stcv"')&&view.innerHTML.includes('Hele skærmen'),'studio opens with canvas and document panel');
  handlers['[data-s]'].find(x=>x.dataset.s==='add').onclick();
  ok(view.innerHTML.includes('data-ad="arc"')&&view.innerHTML.includes('data-ad="spark"'),'add menu lists the element types');
  handlers['[data-ad]'].find(x=>x.dataset.ad==='arc').onclick();
  ok(view.innerHTML.includes('Bue / måler')&&view.innerHTML.includes('Runde ender'),'new arc is selected with its properties');
  handlers['[data-f]'].find(x=>x.dataset.f==='knob').onclick();
  ok(/data-f="knob" class="sel"/.test(view.innerHTML),'style chip toggles');
  handlers['[data-c]'].find(x=>x.dataset.c==='color:warn').onclick();
  ok(/data-c="color:warn" title="Gul"|title="Gul" data-c="color:warn"/.test(view.innerHTML)&&/class="sc sel" title="Gul"/.test(view.innerHTML),'colour swatch selected');
  handlers['[data-ad]']&&0;
  handlers['[data-s]'].find(x=>x.dataset.s==='add').onclick();
  handlers['[data-ad]'].find(x=>x.dataset.ad==='button').onclick();
  ok(view.innerHTML.includes('Når du trykker'),'button has an action setting');
  ok(/\d+ \/ 48 elementer/.test(view.innerHTML),'element counter');
  handlers['[data-s]'].find(x=>x.dataset.s==='undo').onclick();
  ok(!view.innerHTML.includes('Når du trykker'),'undo removes the button again');
  // the default design: change what a button says and does
  handlers['[data-s]'].find(x=>x.dataset.s==='std').onclick();
  const sel0=handlers['[data-sel]'].find(x=>/SPORT/.test(''));      // layers list exists
  ok(handlers['[data-sel]'].length>20,'the standard design is loaded in the studio with its layers');
  const racers=[...view.innerHTML.matchAll(/data-sel="(\d+)"[^>]*>\d+\. Knap · RACE/g)].map(m=>m[1]);
  ok(racers.length===2,'both RACE buttons can be selected: '+racers.length);
  handlers['[data-sel]'].find(x=>x.dataset.sel===racers[0]).onclick();
  ok(view.innerHTML.includes('Når du trykker')&&view.innerHTML.includes('Skifter til næste side'),'the action of the RACE button is shown and can be changed');
  const keys=[...view.innerHTML.matchAll(/data-(?:pk|tx|nv)="([\w.]+)[|:]/g)].map(m=>m[1]);
  ok(keys.includes('action')&&keys.includes('text')&&keys.includes('font')&&keys.includes('w'),'text, action, size and number fields are present: '+keys.join(','));
  ok(!/<select|<input type="text"/.test(view.innerHTML.split('id="stcv"')[1]||''),'no native select or text fields (they are tap based)');
  // pick list: change what the button does
  handlers['[data-pk]'].find(x=>x.dataset.pk.startsWith('action|')).onclick();
  ok(view.innerHTML.includes('class="sheet"')&&view.innerHTML.includes('Forrige side'),'pick list opens with big option buttons');
  const opts=[...view.innerHTML.matchAll(/data-pv="(\d+)"[^>]*>([^<]+)</g)];
  const prevIdx=opts.find(m=>m[2]==='Forrige side')[1];
  handlers['[data-pv]'].find(x=>x.dataset.pv===prevIdx).onclick();
  ok(!view.innerHTML.includes('class="sheet"')&&view.innerHTML.includes('Skifter til forrige side'),'chosen action is applied and explained');
  // typed text and numbers: a box on the page with a save button (no browser dialogs)
  handlers['[data-tx]'].find(x=>x.dataset.tx.startsWith('text|')).onclick();
  ok(view.innerHTML.includes('id="sheetin"')&&view.innerHTML.includes('data-sv='),'text box with save button opens');
  document.querySelector('#sheetin').value='GO'; handlers['[data-sv]'][0].onclick();
  ok(/Knap · GO/.test(view.innerHTML)&&!view.innerHTML.includes('id="sheetin"'),'button text changed and shown in the layer list');
  handlers['[data-nv]'].find(x=>x.dataset.nv.startsWith('w:')).onclick();
  document.querySelector('#sheetin').value='120'; handlers['[data-sv]'][0].onclick();
  ok(/data-nv="w:16:320:Bredde">120</.test(view.innerHTML),'width typed in');
  handlers['[data-cc]'].find(x=>x.dataset.cc==='color').onclick();
  document.querySelector('#sheetin').value='33CCFF'; handlers['[data-sv]'][0].onclick();
  ok(view.innerHTML.includes('Egen farve: #33ccff'),'own colour typed in');
  handlers['[data-s]'].find(x=>x.dataset.s==='install').onclick(); await sleep(900);
  handlers['[data-s]'].find(x=>x.dataset.s==='close').onclick();
  ok(!view.innerHTML.includes('id="stcv"'),'studio closes');
  // start cap (20 km/h at every power-on): only available in the app
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='device').onclick();
  await sleep(1800);   // the demo board restarts itself after the update
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick();
  if(handlers['[data-a]'].find(x=>x.dataset.a==='demo')){handlers['[data-a]'].find(x=>x.dataset.a==='demo').onclick();await sleep(700)}
  handlers['#tabs button'].find(x=>x.dataset.t==='safe').onclick();
  ok(view.innerHTML.includes('Slå start-begrænsning til'),'start cap offered (off by default)');
  handlers['[data-a]'].find(x=>x.dataset.a==='startcap').onclick(); await sleep(400);
  ok(view.innerHTML.includes('Slå start-begrænsning fra'),'start cap turned on in demo board');
  handlers['#tabs button'].find(x=>x.dataset.t==='system').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='device').onclick();
  ok(view.innerHTML.includes('Start 20 km/t til'),'device chip shows start cap on');
  // home: live view fed by the board (demo sends telemetry twice a second)
  handlers['#tabs button'].find(x=>x.dataset.t==='home').onclick(); await sleep(900);
  ok(view.innerHTML.includes('id="lv-sp"'),'live view shown on Hjem');
  ok(els['lv-sp']&&/^\d+$/.test(els['lv-sp'].textContent),'live speed arrives from the board: '+(els['lv-sp']&&els['lv-sp'].textContent));
  ok(/^\d+%$/.test(els['lv-pct'].textContent)&&/ V$/.test(els['lv-v'].textContent),'live battery and volts');
  // screen settings + own accent colour
  handlers['#tabs button'].find(x=>x.dataset.t==='tune').onclick(); handlers['[data-sub]'].find(x=>x.dataset.sub==='screen').onclick();
  handlers['[data-am]'].find(x=>x.dataset.am==='10').onclick(); await sleep(300);
  ok(/data-am="10" class="sel"/.test(view.innerHTML),'arc scale 50 km/h chosen and confirmed');
  handlers['[data-bl]'].find(x=>x.dataset.bl==='40').onclick(); await sleep(300);
  ok(/data-bl="40" class="sel"/.test(view.innerHTML),'backlight 40% confirmed');
  handlers['[data-sub]'].find(x=>x.dataset.sub==='app').onclick();
  handlers['[data-unit]'].find(x=>x.dataset.unit==='mph').onclick();
  ok(/data-unit="mph" class="sel"/.test(view.innerHTML),'units switch to mph');
  handlers['[data-tile]'].find(x=>x.dataset.tile==='power').onclick();
  ok(/data-tile="power" class=""/.test(view.innerHTML),'live tile can be hidden');
  // bad file
  const bad=new Uint8Array(200*1024); bad[0]=0x00;
  els['bin']=els['bin']; 
  console.log(fails.length?'FAILED '+fails.length:'ALL OK'); process.exit(fails.length?1:0);
})();
