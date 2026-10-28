(function(){
 'use strict';
 var $=function(id){return document.getElementById(id);};
 var cfg={language:'zh',enabled:false,uiKey:28,toggleKey:7,nextKey:9,previousKey:8,wheelKey:47},caps={};
 var panel=document.querySelector('main'),anchor=$('wheelAnchor');
 var controllerWheel=false,controllerEpoch=0,controllerSaveTimer;
 var mode='hud',blockUntil=0,editingVolume=false,volumeTimer,drag=null,selected='player',suppressClick=false;
 var lists=[],requested='',pageIndex=0,signature='',candidate='',token=0,holdKey=0,finished=false,aim={x:0,y:0};
 var ns='http://www.w3.org/2000/svg',step=Math.PI/4,context=document.createElement('canvas').getContext('2d');
 var lastState='';
 var layout=defaultLayout();
 function defaultLayout(){return {"player":{"x":0.0831298828125,"y":0.1267361111111111,"scale":1.08},"wheel":{"x":0.72578125,"y":0.5166666666666668,"scale":1.259712}};}

 function send(action,payload){if(typeof window.cloudwalkCommand==='function')window.cloudwalkCommand(JSON.stringify({action:action,payload:payload||{}}));}
 function tr(zh,en){return cfg.language==='en'?en:zh;}
 function finite(n){return typeof n==='number'&&isFinite(n);}
 function element(name){return name==='wheel'?anchor:panel;}
 function saveLayout(){try{localStorage.setItem('cloudwalk.layout.v3',JSON.stringify(layout));}catch(e){}}
 function applyLayout(name){
  var node=element(name),v=layout[name];
  if(name==='wheel'){var diameter=Math.min(520,innerWidth*.78,innerHeight*.78);node.style.width=diameter+'px';node.style.height=diameter+'px';}
  var width=node.offsetWidth,height=node.offsetHeight;
  var max=Math.max(.2,Math.min(2.5,(innerWidth-24)/width,(innerHeight-24)/height));
  v.scale=Math.max(Math.min(.4,max),Math.min(finite(v.scale)?v.scale:1,max));
  if(!finite(v.x))v.x=(innerWidth*.93-width/2)/innerWidth;
  if(!finite(v.y))v.y=(innerHeight*.90-height/2)/innerHeight;
  var halfW=width*v.scale/2,halfH=height*v.scale/2;
  v.x=Math.max(12+halfW,Math.min(v.x*innerWidth,innerWidth-12-halfW))/innerWidth;
  v.y=Math.max(12+halfH,Math.min(v.y*innerHeight,innerHeight-12-halfH))/innerHeight;
  node.style.left=(v.x*innerWidth)+'px';node.style.top=(v.y*innerHeight)+'px';node.style.right='auto';node.style.bottom='auto';
  node.style.transform='translate(-50%,-50%) scale('+v.scale+')';
 }
 function restoreLayout(){
  try{var saved=JSON.parse(localStorage.getItem('cloudwalk.layout.v3'));
   if(saved)['player','wheel'].forEach(function(name){var v=saved[name];if(v&&finite(v.x)&&finite(v.y)&&finite(v.scale))layout[name]={x:v.x,y:v.y,scale:v.scale};});
   else{var old=JSON.parse(localStorage.getItem('cloudwalk.position.v2'));if(old&&finite(old.x)&&finite(old.y)){layout.player.x=old.x+panel.offsetWidth/(2*innerWidth);layout.player.y=old.y+panel.offsetHeight/(2*innerHeight);}}
  }catch(e){}
  applyLayout('player');applyLayout('wheel');
 }
 window.cloudwalkResetLayout=function(){layout=defaultLayout();applyLayout('player');applyLayout('wheel');saveLayout();};
 window.cloudwalkResetPosition=window.cloudwalkResetLayout;
 function selectLayout(name){selected=name;panel.classList.toggle('layout-selected',mode==='layout'&&name==='player');anchor.classList.toggle('layout-selected',mode==='layout'&&name==='wheel');}
 function setMode(value){
  mode=value;document.body.classList.toggle('hud',mode==='hud');document.body.classList.toggle('wheel',mode==='wheel');document.body.classList.toggle('layout',mode==='layout');
  $('radioWheel').hidden=mode==='hud';resetAim();drag=null;
  if(mode!=='hud')renderWheel();selectLayout(selected);applyLayout('player');applyLayout('wheel');
 }
 window.cloudwalkInteraction=function(value){if(value==='1'||value===true)setMode('layout');else window.cloudwalkClosed();};
 window.cloudwalkOpened=function(raw){
  var data={};try{data=typeof raw==='string'?JSON.parse(raw):raw||{};}catch(e){}
  controllerEpoch=data.epoch||0;controllerWheel=false;finished=true;token=0;setMode('layout');blockUntil=Date.now()+220;
 };
 window.cloudwalkOpenWheel=function(raw){
  var data=typeof raw==='string'?JSON.parse(raw):raw;token=data.token;holdKey=data.key;finished=false;controllerWheel=!!data.controller;controllerEpoch=data.epoch||0;
  setMode('wheel');blockUntil=Date.now()+30;
  var at=-1;for(var i=0;i<lists.length;i++)if(lists[i].id===requested){at=i;break;}
  if(at>=0)pageIndex=Math.floor(at/8);renderWheel();
 };
 window.cloudwalkClosed=function(){finished=true;token=0;controllerEpoch=-1;controllerWheel=false;clearTimeout(controllerSaveTimer);setMode('hud');saveLayout();};
 function finishWheel(){
  if(mode!=='wheel'||finished)return;finished=true;
  send('finishWheel',{token:token,id:candidate});$('radioWheel').hidden=true;
 }
 window.cloudwalkReleaseWheel=function(raw){if(String(token)===String(raw))finishWheel();};
 function cancelWheel(){if(mode==='wheel'&&!finished){finished=true;send('cancelWheel',{token:token});$('radioWheel').hidden=true;}}
 function highlight(id,force){
  if(!force&&candidate===id)return;
  candidate=id;var nodes=$('stations').querySelectorAll('.station');
  for(var i=0;i<nodes.length;i++){
   var on=nodes[i].getAttribute('data-id')===id,mid=-Math.PI/2+i*step;
   nodes[i].classList.toggle('selected',on);nodes[i].style.transform=on?'translate('+(Math.cos(mid)*11)+'px,'+(Math.sin(mid)*11)+'px)':'translate(0px,0px)';
  }
 }
 function resetAim(){aim={x:0,y:0};highlight('');}
 function pointAt(x,y){
  var angle=Math.atan2(y,x)+Math.PI/2+Math.PI/8;
  if(angle<0)angle+=Math.PI*2;var index=Math.floor((angle%(Math.PI*2))/step);
  var item=lists[pageIndex*8+index];highlight(item?item.id:'');
 }
 function node(tag,attributes){var n=document.createElementNS(ns,tag);Object.keys(attributes||{}).forEach(function(k){n.setAttribute(k,attributes[k]);});return n;}
 function xy(radius,angle){return {x:300+radius*Math.cos(angle),y:300+radius*Math.sin(angle)};}
 function width(text,font){context.font=font+'px "Microsoft YaHei UI","Microsoft YaHei",sans-serif';return context.measureText(text).width;}
 function fitsBox(x,y,halfW,halfH,mid){
  var nearestX=Math.max(x-halfW,Math.min(300,x+halfW)),nearestY=Math.max(y-halfH,Math.min(300,y+halfH));
  if(Math.hypot(nearestX-300,nearestY-300)<169)return false;
  for(var i=-1;i<=1;i+=2)for(var j=-1;j<=1;j+=2){
   var dx=x+i*halfW-300,dy=y+j*halfH-300,angle=Math.atan2(dy,dx)-mid;
   angle=Math.atan2(Math.sin(angle),Math.cos(angle));
   if(Math.hypot(dx,dy)>264||Math.abs(angle)>step/2-.045)return false;
  }
  return true;
 }
 function lineWidth(x,y,font,mid){
  var low=0,high=160;
  for(var i=0;i<16;i++){var half=(low+high)/2;if(fitsBox(x,y,half,font*.62,mid))low=half;else high=half;}
  return low*2;
 }
 function wrap(text,font,count,center,mid,ellipsis){
  var chars=Array.from(text.replace(/\s+/g,' ').trim()),lines=[],lineHeight=font*1.25;
  for(var i=0;i<count;i++){
   var y=center.y+(i-(count-1)/2)*lineHeight,limit=lineWidth(center.x,y,font,mid),take=0;
   if(limit<font)return null;
   while(take<chars.length&&width(chars.slice(0,take+1).join(''),font)<=limit)take++;
   if(take<chars.length&&take>2){var space=chars.slice(0,take).lastIndexOf(' ');if(space>take*.45)take=space;}
   var part=chars.splice(0,take).join('').trim();while(chars[0]===' ')chars.shift();
   if(i===count-1&&chars.length&&ellipsis){while(part&&width(part+'…',font)>limit)part=Array.from(part).slice(0,-1).join('');part+='…';}
   if(part)lines.push({text:part,x:center.x,y:y+font*.35,limit:limit});
   if(!chars.length)return {font:font,lines:lines,truncated:false};
  }
  return ellipsis?{font:font,lines:lines,truncated:true}:null;
 }
 function wheelLabel(text,mid){
  var center=xy(216,mid),layout;
  for(var font=14;font>=11&&!layout;font-=.5)for(var count=1;count<=4&&!layout;count++)layout=wrap(text,font,count,center,mid,false);
  if(!layout)layout=wrap(text,11,4,center,mid,true);
  var t=node('text',{'text-anchor':'middle','font-size':layout.font,'data-full-name':text,'data-truncated':String(layout.truncated)});
  layout.lines.forEach(function(line){var span=node('tspan',{x:line.x,y:line.y,'data-line-width':line.limit});span.textContent=line.text;t.appendChild(span);});
  return t;
 }
 function renderWheel(){
  var pages=Math.max(1,Math.ceil(lists.length/8));pageIndex=Math.max(0,Math.min(pageIndex,pages-1));
  var next=JSON.stringify([lists,requested,pageIndex]);if(next===signature)return;signature=next;
  var svg=$('stations');while(svg.firstChild)svg.removeChild(svg.firstChild);
  var entries=lists.slice(pageIndex*8,pageIndex*8+8);
  function xy(radius,angle){return [300+radius*Math.cos(angle),300+radius*Math.sin(angle)];}
  if(!entries.length){var ring=document.createElementNS(ns,'circle');ring.setAttribute('cx','300');ring.setAttribute('cy','300');ring.setAttribute('r','206');ring.setAttribute('class','empty-ring');ring.style.fill='none';svg.appendChild(ring);}
  entries.forEach(function(item,index){
   var mid=-Math.PI/2+index*Math.PI/4,start=mid-Math.PI/8+.013,end=mid+Math.PI/8-.013;
   var a=xy(278,start),b=xy(278,end),c=xy(144,end),d=xy(144,start),g=document.createElementNS(ns,'g'),path=document.createElementNS(ns,'path');
   g.setAttribute('class','station'+(requested===item.id?' requested':''));g.setAttribute('data-id',item.id);g.setAttribute('aria-label',item.name);
   path.setAttribute('d','M'+a.join(',')+' A278,278 0 0,1 '+b.join(',')+' L'+c.join(',')+' A144,144 0 0,0 '+d.join(',')+' Z');g.appendChild(path);
   g.appendChild(wheelLabel(item.name,mid));
   if(requested===item.id){var dot=xy(154,mid);g.appendChild(node('circle',{'class':'current-marker',cx:dot[0],cy:dot[1],r:4.5}));}
   svg.appendChild(g);
  });
  var valid=false;for(var i=0;i<entries.length;i++)if(entries[i].id===candidate)valid=true;
  highlight(valid?candidate:'',true);
 }
 function changePage(delta){var pages=Math.max(1,Math.ceil(lists.length/8));pageIndex=(pageIndex+delta+pages)%pages;resetAim();renderWheel();}
 window.cloudwalkMouse=function(raw){
  var data;try{data=typeof raw==='string'?JSON.parse(raw):raw;}catch(e){return;}
  if(!data||mode!=='wheel'||finished||controllerWheel||data.epoch!==controllerEpoch||data.token!==token)return;
  if(data.page)changePage(data.page>0?1:-1);
  if(data.scaleStep){layout.wheel.scale*=Math.pow(1.08,data.scaleStep>0?1:-1);applyLayout('wheel');saveLayout();}
  if(data.page||!finite(data.dx)||!finite(data.dy)||(!data.dx&&!data.dy))return;
  aim.x+=data.dx;aim.y+=data.dy;var radius=Math.hypot(aim.x,aim.y),max=85;
  if(radius>max){aim.x*=max/radius;aim.y*=max/radius;radius=max;}
  if(radius<12){highlight('');return;}
  pointAt(aim.x/max,aim.y/max);
 };
 // Native XInput samples drive the wheel without synthetic mouse input.
 window.cloudwalkController=function(raw){
  var data;try{data=typeof raw==='string'?JSON.parse(raw):raw;}catch(e){return;}
  if(!data||mode==='hud'||data.epoch!==controllerEpoch)return;
  if(mode==='wheel'){
   if(finished||!controllerWheel||data.token!==token)return;
   if(data.page)changePage(data.page>0?1:-1);
   if(data.scaleStep){layout.wheel.scale*=Math.pow(1.08,data.scaleStep>0?1:-1);applyLayout('wheel');saveLayout();}
   if(!data.page&&finite(data.x)&&finite(data.y)){
    var radius=Math.sqrt(data.x*data.x+data.y*data.y);
    if(radius>0){
     var angle=Math.atan2(-data.y,data.x)+Math.PI/2+Math.PI/8;
     if(angle<0)angle+=Math.PI*2;
     var index=Math.floor((angle%(Math.PI*2))/(Math.PI/4)),item=lists[pageIndex*8+index];highlight(item?item.id:'');
    }
   }
   return;
  }
  if(data.select)selectLayout(selected==='player'?'wheel':'player');
  var moved=false,v=layout[selected];
  if(finite(data.dx)&&finite(data.dy)){v.x+=data.dx/innerWidth;v.y+=data.dy/innerHeight;moved=true;}
  if(finite(data.scale)){v.scale*=Math.exp(Math.max(-.2,Math.min(.2,data.scale)));moved=true;}
  if(moved){applyLayout(selected);clearTimeout(controllerSaveTimer);controllerSaveTimer=setTimeout(saveLayout,150);}
  if(finite(data.volumeStep)&&caps.volume){
   var volume=Math.max(0,Math.min(1,Number($('volume').value)+data.volumeStep));$('volume').value=volume;send('settings',{volume:volume});
  }
 };
 document.addEventListener('mousedown',function(e){
  if(mode==='wheel'){
   e.preventDefault();e.stopPropagation();return;
  }
  if(mode!=='layout'||e.button!==0)return;
  var target=e.target.closest&&e.target.closest('.walkman,.wheel-anchor');if(!target)return;
  var name=target===panel?'player':'wheel';selectLayout(name);
  drag={name:name,x:e.clientX,y:e.clientY,cx:layout[name].x*innerWidth,cy:layout[name].y*innerHeight,moved:false};
  e.preventDefault();
 },true);
 document.addEventListener('mousemove',function(e){
  if(drag&&mode==='layout'){
   var dx=e.clientX-drag.x,dy=e.clientY-drag.y;if(Math.abs(dx)+Math.abs(dy)>3)drag.moved=true;
   layout[drag.name].x=(drag.cx+dx)/innerWidth;layout[drag.name].y=(drag.cy+dy)/innerHeight;applyLayout(drag.name);e.preventDefault();return;
  }
 },true);
 document.addEventListener('mouseup',function(e){if(!drag||e.button!==0)return;suppressClick=drag.moved;drag=null;saveLayout();if(suppressClick)setTimeout(function(){suppressClick=false;},0);},true);
 document.addEventListener('click',function(e){if(mode==='wheel'||suppressClick){e.preventDefault();e.stopPropagation();}},true);
 document.addEventListener('contextmenu',function(e){if(mode!=='hud'){e.preventDefault();e.stopPropagation();}},true);
 document.addEventListener('wheel',function(e){
  if(mode==='hud'||finished&&mode==='wheel'||!e.deltaY)return;
  e.preventDefault();e.stopPropagation();if(mode==='wheel')return;var name=selected;
  layout[name].scale*=e.deltaY<0?1.08:1/1.08;applyLayout(name);saveLayout();
 },{capture:true,passive:false});
 window.addEventListener('blur',function(){if(drag){drag=null;saveLayout();}cancelWheel();});
 window.addEventListener('resize',function(){applyLayout('player');applyLayout('wheel');saveLayout();});
 window.cloudwalkState=function(raw){
  if(typeof raw==='string'&&raw===lastState)return;
  var s;try{s=typeof raw==='string'?JSON.parse(raw):raw;}catch(e){return;}
  var key=typeof raw==='string'?raw:JSON.stringify(s);if(key===lastState)return;lastState=key;
  cfg=s.config||cfg;caps=s.capabilities||{};lists=Array.isArray(s.playlists)?s.playlists:[];requested=s.requestedPlaylist||'';
  var error=s.commandError||'';$('playerError').textContent=error;$('playerError').hidden=!error;
  if(mode!=='hud')renderWheel();var connected=s.connected===true,on=cfg.enabled===true;
  document.body.classList.toggle('on',on);document.body.classList.toggle('connected',connected);document.body.classList.toggle('playing',on&&!!s.shouldPlay);
  document.documentElement.lang=cfg.language==='en'?'en':'zh-CN';
  $('status').setAttribute('aria-label',connected?tr('已连接','Connected'):tr('未连接','Disconnected'));$('title').textContent=s.now&&s.now.title?s.now.title:tr('网易云音乐','NetEase Cloud Music');
  $('artist').textContent=s.now&&s.now.artist?s.now.artist:connected?(on?tr('已开启','Enabled'):tr('已关闭','Disabled')):tr('未连接','Disconnected');
  $('playIcon').style.display=on?'none':'';$('pauseIcon').style.display=on?'':'none';
  $('toggle').setAttribute('aria-label',on?tr('关闭随声听','Turn playback off'):tr('开启随声听','Turn playback on'));$('toggle').setAttribute('aria-pressed',String(on));
  $('previous').setAttribute('aria-label',tr('上一首','Previous track'));$('next').setAttribute('aria-label',tr('下一首','Next track'));$('volume').setAttribute('aria-label',tr('网易云音量','NetEase volume'));
  $('previous').disabled=!on||!connected||!caps.previous;$('next').disabled=!on||!connected||!caps.next;
  $('volume').disabled=!connected||!caps.volume;if(!editingVolume)$('volume').value=cfg.volume==null?1:cfg.volume;
  applyLayout('player');
 };
 ['toggle','previous','next'].forEach(function(action){$(action).onclick=function(){send(action);};});
 $('close').onclick=function(){send('close');};
 $('volume').oninput=function(){editingVolume=true;var volume=Number(this.value);clearTimeout(volumeTimer);volumeTimer=setTimeout(function(){send('settings',{volume:volume});editingVolume=false;},120);};
 // Physical keyboard codes use SKSE/DirectInput scan codes, including extended keys.
 var dik={Escape:1,Digit1:2,Digit2:3,Digit3:4,Digit4:5,Digit5:6,Digit6:7,Digit7:8,Digit8:9,Digit9:10,Digit0:11,Minus:12,Equal:13,Backspace:14,Tab:15,BracketLeft:26,BracketRight:27,Enter:28,ControlLeft:29,Semicolon:39,Quote:40,Backquote:41,ShiftLeft:42,Backslash:43,Comma:51,Period:52,Slash:53,ShiftRight:54,NumpadMultiply:55,AltLeft:56,Space:57,CapsLock:58,NumLock:69,ScrollLock:70,Numpad7:71,Numpad8:72,Numpad9:73,NumpadSubtract:74,Numpad4:75,Numpad5:76,Numpad6:77,NumpadAdd:78,Numpad1:79,Numpad2:80,Numpad3:81,Numpad0:82,NumpadDecimal:83,IntlBackslash:86,F11:87,F12:88,NumpadEnter:156,ControlRight:157,NumpadDivide:181,PrintScreen:183,AltRight:184,Pause:197,Home:199,ArrowUp:200,PageUp:201,ArrowLeft:203,ArrowRight:205,End:207,ArrowDown:208,PageDown:209,Insert:210,Delete:211,MetaLeft:219,MetaRight:220,ContextMenu:221};
 'QWERTYUIOP'.split('').forEach(function(c,i){dik['Key'+c]=16+i;});'ASDFGHJKL'.split('').forEach(function(c,i){dik['Key'+c]=30+i;});'ZXCVBNM'.split('').forEach(function(c,i){dik['Key'+c]=44+i;});for(var f=1;f<=10;f++)dik['F'+f]=58+f;
 function scan(e){
  if(dik[e.code])return dik[e.code];var vk=e.keyCode||e.which||0;
  if(e.location===2){if(vk===16)return 54;if(vk===17)return 157;if(vk===18)return 184;}
  if(vk===13&&e.location===3)return 156;
  if(vk>=96&&vk<=105)return [82,79,80,81,75,76,77,71,72,73][vk-96];
  if(vk>=112&&vk<=121)return 59+vk-112;if(vk===122)return 87;if(vk===123)return 88;
  if(vk>=65&&vk<=90)return dik['Key'+String.fromCharCode(vk)]||0;
  if(vk>=48&&vk<=57)return dik['Digit'+String.fromCharCode(vk)]||0;
  var legacy={91:219,92:220,93:221,106:55,107:78,109:74,110:83,111:181,144:69,145:70,160:42,161:54,162:29,163:157,164:56,165:184,27:1,13:28,32:57,9:15,8:14,16:42,17:29,18:56,20:58,33:201,34:209,35:207,36:199,37:203,38:200,39:205,40:208,45:210,46:211,186:39,187:13,188:51,189:12,190:52,191:53,192:41,219:26,220:43,221:27,222:40};return legacy[vk]||0;
 }

 document.addEventListener('keydown',function(e){
  if(mode==='hud'||e.repeat)return;var key=scan(e);
  var escape=e.key==='Escape'||e.key==='Esc'||e.keyIdentifier==='U+001B'||key===1;
  if(escape){e.preventDefault();e.stopPropagation();if(mode==='wheel')cancelWheel();else send('close');return;}
  if(mode==='wheel'){e.preventDefault();return;}
  if(Date.now()<blockUntil)return;var action='';
  if(key&&key===cfg.uiKey)action='close';else if(key&&key===cfg.wheelKey)action='wheel';
  else if(key&&key===cfg.toggleKey)action='toggle';else if(key&&key===cfg.nextKey&&cfg.enabled&&caps.next)action='next';
  else if(key&&key===cfg.previousKey&&cfg.enabled&&caps.previous)action='previous';
  if(action){e.preventDefault();e.stopPropagation();send(action);}
 },true);
 document.addEventListener('keyup',function(e){if(mode==='wheel'&&scan(e)===holdKey){e.preventDefault();e.stopPropagation();finishWheel();}},true);
 restoreLayout();setMode('hud');
 if(location.search.indexOf('preview=1')!==-1)window.cloudwalkState({connected:true,capabilities:{next:true,previous:true,volume:true},config:{enabled:true,volume:.45,uiKey:28,toggleKey:7,nextKey:9,previousKey:8,wheelKey:47},now:{title:'山风经过的地方',artist:'网易云音乐'}});
})();
