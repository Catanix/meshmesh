// MeshMesh for Android: the device's own page (web/index.html) with a connection screen and its
// fetch('/api/…') answered by the app over Wi-Fi, Bluetooth or USB (MainActivity / MeshService).
// Uses the page's globals: auth, timer, route, config, status, conn, request, command, refresh, show…
(()=>{'use strict';
const N=window.MeshNative;if(!N)return;
if(N.language)N.language(LANG); // the app's own texts and notifications in the page's language
const H={tab:null,devices:{wifi:[],ble:[],usb:[]},scanning:{},done:{},state:{state:'idle'},prefs:{},sel:'',inApp:false,armed:0,upd:{state:'idle'},manual:false,updNoted:false};
const KIND={wifi:['wifi','Wi-Fi'],ble:['ble','Bluetooth'],usb:['bolt','USB'],tcp:['bolt',L('USB через компьютер')]};

// fetch('/api/…') → the app; everything else (OpenStreetMap for map preparation) → the network.
const pending=new Map();let seq=0;const netFetch=window.fetch.bind(window);
function toBase64(bytes){let s='';for(let i=0;i<bytes.length;i+=32768)s+=String.fromCharCode.apply(null,bytes.subarray(i,i+32768));return btoa(s)}
window.fetch=function(input,init){
 const url=typeof input==='string'?input:input.url,path=url.replace(/^https:\/\/appassets\.androidplatform\.net/,'');
 if(!path.startsWith('/api/'))return netFetch(input,init);
 const method=(init&&init.method)||'GET',body=init&&init.body;
 return new Promise((resolve,reject)=>{const id=++seq;pending.set(id,{resolve,reject});
  N.request(id,method,path,body==null?null:body instanceof Uint8Array?toBase64(body):String(body))})};

window.MeshHost={
 done(id,status,body,b64){const p=pending.get(id);if(!p)return;pending.delete(id);
  if(!status){p.reject(new TypeError(body||L('нет связи')));return}
  const data=b64?Uint8Array.from(atob(body),c=>c.charCodeAt(0)):body;
  p.resolve(new Response(data,{status,headers:{'Content-Type':b64?'application/octet-stream':'text/plain; charset=utf-8'}}))},
 devices(kind,list,done){H.devices[kind]=list||[];H.scanning[kind]=!done;if(done)H.done[kind]=true;if(H.tab===kind)renderPanel()},
 state(s){const before=H.state;H.state=s||{state:'idle'};
  if(s.state==='connected')enterApp();
  else if(s.state==='flashing'){if(H.inApp)leaveApp(null);else renderPanel()}
  else if(s.state==='lost'||s.state==='idle'){if(H.inApp)leaveApp(s.message||L('Связь с устройством потеряна'),s.state==='lost'?'bad':'muted');else renderPanel()}
  else if(s.state==='failed'){renderPanel();notify(s.message||L('Не удалось подключиться'),'bad')}
  else renderPanel();
  void before},
 toast(text,tone){notify(text,tone||'accent')},
 // A scanned QR code or a meshcore:// link: the page asks to join the channel once a board is connected.
 channelLink(text){text=String(text||'').trim();
  const isChannel=/^meshcore:\/\/channel\/add\?/i.test(text),isPerson=/^meshcore:\/\/[0-9a-fA-F]{128,512}$/.test(text);
  if(!isChannel&&!isPerson){notify(L('Это не ссылка MeshCore (канал или контакт)'),'warn');return}
  H.link=text;if(H.inApp)openLink();else notify(isPerson?L('Подключитесь к устройству, чтобы импортировать контакт'):L('Подключитесь к устройству, чтобы добавить канал'),'accent')},
 go(target){if(H.inApp)go(target)},
 // Updates of the app (MainActivity → Updater): a card on the connection screen and on «Подключения».
 update(u){const was=H.upd.state;H.upd=u||{state:'idle'};const st=H.upd.state;
  if(st==='available'&&was!=='available'&&H.inApp&&!H.manual&&!H.updNoted){H.updNoted=true;notify(UPD_NOTE,'accent')}
  if(H.manual&&['none','error','off'].includes(st)){H.manual=false;notify(st==='none'?L('Установлена последняя версия приложения'):H.upd.message,st==='none'?'ok':st==='off'?'warn':'bad')}
  else if(st==='error')notify(H.upd.message,'bad');
  if(st!=='checking'&&st!=='downloading')H.manual=false;
  if(!H.inApp)renderPanel();else if(route==='connect')renderConnect();else if(route==='home')renderHome();else if(route==='settings')renderSettings()},
 back(){
  if(typeof standaloneMode!=='undefined'&&standaloneMode&&route==='library'){go('map');return true}
  if(typeof standaloneMode!=='undefined'&&standaloneMode){standaloneMode=false;$('app').hidden=true;$('login').hidden=false;loginHud();return true}
  if(H.inApp&&!$('back').hidden){$('back').click();return true}
  return false}};

// The connection screen replaces the password form of the login page.
const login=$('login');$('loginForm').hidden=true;
login.querySelector('.brand small').textContent=L('Ваши люди. Ваша сеть.');
login.querySelector('.brand').insertAdjacentHTML('afterend','<div id="hostPanel"></div>');
const panel=$('hostPanel');
function rssiBars(r){return r==null?'':bars(r>-60?4:r>-70?3:r>-80?2:1,'var(--accent)')}
function row(attrs,icon,cls,name,detail,side,sel){return `<button class="hrow${sel?' sel':''}" ${attrs}><span class="icbox">${ic(icon,cls)}</span><span class="main"><b>${esc(name)}</b><small>${esc(detail)}</small></span><span class="side">${side||''}</span></button>`}
function scanButton(kind,text){const busy=H.scanning[kind];return `<button class="btn save" data-hscan="${kind}"${busy?' disabled':''}>${busy?L('Поиск…'):text}</button>`}
function renderPanel(){
 const tab=H.tab,s=H.state,flashing=s.state==='flashing',busy=s.state==='connecting'||flashing;
 let h=`<div class="tabs">${['wifi','ble','usb'].map(k=>`<button data-htab="${k}" class="${k===tab?'on':''}">${ic(KIND[k][0])}${KIND[k][1]}</button>`).join('')}</div><div class="card hbody">`;
 if(tab==='wifi'){const list=H.devices.wifi,pass=H.prefs.passwords||{};
  h+=`<div class="hlink">${ic('wifi','info')}<span>${L`Включите точку доступа на устройстве: M9 — «Связь», Heltec — страница Wi-Fi.`}</span></div>`;
  h+=list.length?list.map(d=>row(`data-hwifi="${esc(d.id)}"`,'wifi','info',d.name,pass[d.id]?L('пароль сохранён'):L('нужен пароль'),rssiBars(d.rssi),d.id===H.sel)).join(''):`<div class="hempty">${H.done.wifi?L('Точки MM-… не найдены. Можно подключиться и без списка — Android покажет найденные сети.'):L('Найдите точку MM-… или подключитесь через запрос Android.')}</div>`;
  h+=scanButton('wifi',L('Искать точки MM-…'));
  h+=`<label class="field">${L`Пароль Wi-Fi${H.sel?L` для ${esc(H.sel)}`:''}`}<input id="hPass" type="password" autocomplete="off" value="${esc(pass[H.sel]||'')}"></label><p class="small muted">${L`Пароль показан на устройстве. Если раньше подключались по USB или Bluetooth, приложение уже знает пароль текущей загрузки.`}</p>`;
  h+=`<button class="btn primary save" data-hgo="wifi"${busy?' disabled':''}>${L`Подключиться`}</button>`;
  h+=`<details class="more" style="background:var(--bg)"><summary>${L`Другой адрес`}</summary><label class="field">${L`Адрес устройства`}<input id="hAddr" value="${esc(H.prefs.address||'192.168.4.1')}" autocomplete="off"></label><p class="small muted">${L`192.168.4.1 — точка доступа MeshMesh. Другой адрес — устройство в сети, к которой телефон уже подключён.`}</p></details>`}
 else if(tab==='ble'){const list=H.devices.ble;
  h+=`<div class="hlink">${ic('ble','info')}<span>${L`Включите BLE на устройстве. При первом подключении Android спросит PIN — он на экране устройства.`}</span></div>`;
  h+=list.length?list.map(d=>row(`data-hble="${esc(d.id)}"`,'ble','info',d.name,d.bonded?L('сопряжено'):L('новое устройство'),rssiBars(d.rssi))).join(''):`<div class="hempty">${H.scanning.ble?L('Ищу устройства MeshMesh…'):H.done.ble?L('Устройства MeshMesh не найдены: включён ли BLE на плате?'):L('Нажмите «Искать», чтобы найти устройства рядом.')}</div>`;
  h+=scanButton('ble',L('Искать устройства'))}
 else{const list=H.devices.usb;
  h+=`<div class="hlink">${ic('bolt','acc')}<span>${L`Подключите плату кабелем USB-C через OTG-переходник.`}</span></div>`;
  h+=list.length?list.map(d=>row(`data-husb="${esc(d.id)}"`,'bolt','acc',d.name,L('Нажмите, чтобы подключиться'),ic('next','faint'))).join(''):`<div class="hempty">${L`Плата по USB не найдена.`}</div>`;
  h+=`<button class="btn save" data-hscan="usb">${L`Обновить список`}</button>`;
  const [host,port]=(H.prefs.bridge||'10.0.2.2:8771').split(':');
  h+=`<details class="more" style="background:var(--bg)"><summary>${L`USB через компьютер`}</summary><p class="small muted">${L`Плата подключена к компьютеру, на нём запущен`} <span class="mono">tools/usb_tcp_bridge.py</span>${L`; 10.0.2.2 — компьютер для эмулятора Android.`}</p><div class="two"><label class="field">${L`Адрес`}<input id="hHost" value="${esc(host)}"></label><label class="field">${L`Порт`}<input id="hPort" type="number" value="${esc(port||8771)}"></label></div><button class="btn save" data-hgo="tcp"${busy?' disabled':''}>${L`Подключиться к мосту`}</button></details>`}
 h+='</div>';
 if(flashing)h=`<div class="card conn"><div class="top"><span class="icbox"><span class="spin"></span></span><div><b>${L`Обновление прошивки`}</b><small>${esc(s.message||'')}</small></div></div>${s.progress!=null?`<progress value="${s.progress/100}" style="width:100%;margin-top:10px"></progress>`:''}<p class="small muted" style="margin:8px 0 0">${L`Не отключайте кабель и не закрывайте приложение. Ключ, настройки, контакты и история сохранятся.`}</p></div>`+h;
 else if(busy)h+=`<div class="card hstate"><span class="spin"></span><div style="flex:1">${esc(s.message||L('Подключение…'))}</div><button class="btn" data-hstop>${L`Отмена`}</button></div>`;
 else if(s.state==='failed')h+=`<div class="card hstate">${ic('failed','bad')}<div class="bad" style="flex:1">${esc(s.message||L('Не удалось подключиться'))}</div>${s.flash?`<button class="btn primary" data-hflash="retry">${L`Повторить`}</button>`:''}</div>`;
 const last=H.prefs.last;
 if(last&&last.kind&&!busy)h+=`<h3>${L`Последнее устройство`}</h3><div class="card">${row('data-hlast','radio','acc',last.label||'MeshMesh',L`${KIND[last.kind]?.[1]} · подключиться снова`,'',false)}</div>`;
 panel.innerHTML=updateCard(false)+h}

function connect(spec){N.connect(JSON.stringify(spec))}
// A found app update, once connected: a dot on «Связь» (home) and «Подключения» (settings), where its card is,
// and one notice per run of the app, on entering the device or when the check finishes later.
const UPD_NOTE=L('Вышла новая версия приложения: «Связь» → «Обновить»');
function updatePending(){return ['available','downloading','ready'].includes(H.upd.state)}
function markConnect(list){if(!updatePending())return;const t=$(list).querySelector('[data-go="connect"]');if(t)t.insertAdjacentHTML('beforeend','<span class="hdot"></span>')}
const pageRenderHome=window.renderHome,pageRenderSettings=window.renderSettings;
window.renderHome=function(){pageRenderHome();markConnect('tiles');renderHomeLink()};
window.renderSettings=function(){pageRenderSettings();markConnect('settingsList')};
function updateCard(always){const u=H.upd,st=u.state,mb=u.size?L` · ${(u.size/1048576).toFixed(1)} МБ`:'';
 if(!always&&!['available','downloading','ready'].includes(st))return'';
 const text=st==='available'?L`Доступна версия ${esc(u.name)}${mb}`:st==='downloading'?L`Загрузка ${esc(u.name)}: ${u.progress|0}%`:st==='ready'?L('Загружено: подтвердите установку в окне Android'):st==='checking'?L('Проверка…'):L`Версия ${esc(N.version())}`;
 const btn=st==='available'?`<button class="btn primary" data-hupd="install">${L`Обновить`}</button>`:st==='ready'?`<button class="btn primary" data-hupd="install">${L`Установить`}</button>`:st==='downloading'||st==='checking'?'':`<button class="btn soft" data-hupd="check">${L`Проверить обновления`}</button>`;
 return `<div class="card conn"><div class="top"><span class="icbox">${ic('down',st==='available'||st==='ready'?'acc':'info')}</span><div><b>${L`Приложение MeshMesh`}</b><small>${text}</small></div></div>${st==='downloading'?`<progress value="${(u.progress|0)/100}" style="width:100%"></progress>`:''}${btn?`<div class="extra"><div class="btns">${btn}</div></div>`:''}</div>`}
// Firmware of the site over the USB link (MeshService.flashFirmware → flash/FirmwareUpdate): ESP32 boards
// by their ROM loader, nRF52 (GAT562, T114) by its serial DFU bootloader.
let fwSite=null;
function loadFwSite(){if(fwSite)return;fwSite={};netFetch('https://ikrasnodymov.github.io/meshmesh/firmware/boards.json',{cache:'no-store'}).then(r=>r.json()).then(j=>{fwSite=j}).catch(()=>{fwSite={error:true}}).finally(()=>{if(H.inApp&&route==='connect')renderConnect()})}
function newerVersion(a,b){const x=String(a).split('.').map(Number),y=String(b).split('.').map(Number);for(let i=0;i<Math.max(x.length,y.length);i++){if((x[i]||0)!==(y[i]||0))return (x[i]||0)>(y[i]||0)}return false}
function firmwareCard(){const s=H.state;if(!H.inApp||!['usb','tcp'].includes(s.kind))return'';loadFwSite();
 const env=status.board==='heltec_v4'&&status.psram>3e6?'heltec_v4_r8':status.board,b=fwSite.boards?.find(x=>x.env===env),cur=String(status.firmware||'').replace(/^MeshMesh\s*/,''),esp=b&&(b.install!=='uf2'||!!b.dfu),newer=esp&&newerVersion(fwSite.version,cur);
 const text=fwSite.error?L('Сайт недоступен: для обновления нужен интернет'):!fwSite.boards?L('Проверка версии на сайте…'):!b?L('На сайте нет прошивки для этой платы'):!esp?L`На сайте ${fwSite.version}: эта плата обновляется с сайта или компьютера`:L`Установлена ${esc(cur)} · на сайте ${esc(fwSite.version)}`;
 return `<div class="card conn"><div class="top"><span class="icbox">${ic('bolt',newer?'acc':'info')}</span><div><b>${L`Прошивка платы`}</b><small>${text}</small></div></div>${esp?`<div class="extra"><div class="btns"><button class="btn ${newer?'primary':'soft'}" data-hflash="go">${newer?L('Обновить прошивку'):L('Переустановить')}</button></div><small class="muted">${L`По USB с сайта, ${esc(b.name)}; данные платы сохраняются`}</small></div>`:''}</div>`}
document.addEventListener('click',e=>{const t=e.target.closest('[data-hflash]');if(!t)return;
 if(t.dataset.hflash==='retry'){N.flashFirmware(true);return}
 if(Date.now()-H.armed>6000){H.armed=Date.now();notify(L`Нажмите ещё раз: плата перезапустится в загрузчик, запись займёт 1–3 минуты${status.wifi_radio===false?L(' (Android может спросить доступ к USB загрузчика — разрешите)'):''}. Ключ, настройки, контакты и история сохранятся`,'warn');return}
 H.armed=0;N.flashFirmware(false)});
document.addEventListener('click',e=>{const t=e.target.closest('[data-hupd]');if(!t)return;
 if(t.dataset.hupd==='check'){H.manual=true;N.checkUpdate()}else N.installUpdate()});
// The connection screen's header: the page's hud() would keep the last page's title and BACK.
function loginHud(){hud();$('title').textContent='MeshMesh';$('title').className='home';$('back').hidden=true;$('icons').innerHTML=''}
panel.addEventListener('click',e=>{const t=e.target.closest('[data-htab],[data-hscan],[data-hwifi],[data-hble],[data-husb],[data-hgo],[data-hstop],[data-hlast]');if(!t)return;const d=t.dataset;
 if(d.htab){H.tab=d.htab;renderPanel();if(!H.done[d.htab]&&!H.scanning[d.htab])N.scan(d.htab);return}
 if(d.hscan){H.scanning[d.hscan]=d.hscan!=='usb';renderPanel();N.scan(d.hscan);return}
 if(d.hwifi!==undefined){H.sel=H.sel===d.hwifi?'':d.hwifi;renderPanel();return}
 if(d.hble){N.stopScan();H.scanning.ble=false;connect({kind:'ble',id:d.hble});return}
 if(d.husb){connect({kind:'usb',id:d.husb});return}
 if(d.hstop!==undefined){N.disconnect();return}
 if(d.hlast!==undefined){const l=H.prefs.last;if(l.kind==='wifi'){H.tab='wifi';H.sel=l.id&&l.id.startsWith('MM-')?l.id:'';renderPanel();return}
  if(l.kind==='tcp'){const [host,port]=String(l.id).split(':');connect({kind:'tcp',host,port:+port});return}connect({kind:l.kind,id:l.id});return}
 if(d.hgo==='wifi'){const password=$('hPass').value,address=($('hAddr')?.value||'192.168.4.1').trim();if(!password){notify(L('Введите пароль Wi-Fi с экрана устройства'),'warn');return}
  connect({kind:'wifi',ssid:address==='192.168.4.1'?H.sel:'',password,address});return}
 if(d.hgo==='tcp'){connect({kind:'tcp',host:$('hHost').value.trim(),port:+$('hPort').value||8771});return}});

function openLink(){const l=H.link;H.link='';if(!l)return;if(window.openMeshLink)openMeshLink(l);else if(window.openChannelLink)openChannelLink(l)}

// Connected: the same steps as the page's own login.
async function enterApp(){
 if(H.inApp)return;H.inApp=true;auth='MeshMesh app';
 try{config=await request('/api/config')}catch(e){H.inApp=false;auth='';N.disconnect();notify(L`Устройство не ответило: ${e.message}`,'bad');return}
 $('login').hidden=true;$('app').hidden=false;await refresh();clearInterval(timer);timer=setInterval(refresh,H.state.kind==='ble'?4000:3000);route='';show();
 const note=updatePending()&&!H.updNoted;if(note)H.updNoted=true;
 notify(L`Подключено: ${H.state.label||''}${note?'. '+UPD_NOTE:''}`,note?'accent':'ok');openLink()}
function leaveApp(message,tone){
 H.inApp=false;auth='';clearInterval(timer);if(radarTimer){clearInterval(radarTimer);radarTimer=null;radarData=null}
 for(const [,p] of pending)p.reject(new TypeError(L('нет связи')));pending.clear();
 H.prefs=JSON.parse(N.prefs());$('app').hidden=true;$('login').hidden=false;loginHud();renderPanel();if(message)notify(message,tone)}

// Connections page: the app's link first, with disconnect and, from Bluetooth, the faster Wi-Fi. Its own
// box above the page's list: the page skips an unchanged list (setHtml), so cards added to it would pile up.
function hostBox(id,anchor,where){let b=$(id);if(!b){$(anchor).insertAdjacentHTML(where,`<div id="${id}"></div>`);b=$(id)}return b}
const pageRenderConnect=window.renderConnect;
window.renderConnect=function(){pageRenderConnect();const s=H.state,box=hostBox('connApp','connList','beforebegin');if(!H.inApp||!KIND[s.kind]){setHtml(box,'');return}const c=conn||{};
 const toWifi=s.kind==='ble'&&status.wifi&&c.ssid&&c.password?`<button class="btn soft" data-hconn="wifi">${L`Перейти на Wi-Fi`}</button>`:'';
 setHtml(box,`<div class="list" style="gap:8px;margin-bottom:8px"><div class="card conn"><div class="top"><span class="icbox">${ic(KIND[s.kind][0],'acc')}</span><div><b>${L`Приложение · ${KIND[s.kind][1]}`}</b><small>${esc(s.label||'')}</small></div></div><div class="extra"><div class="btns">${toWifi}<button class="btn danger" data-hconn="off">${L`Отключиться`}</button></div><small class="muted">${L`${toWifi?L('Wi-Fi быстрее Bluetooth для карт и радара. '):''}«Назад» на главном экране сворачивает приложение, связь остаётся`}</small></div></div>`+firmwareCard()+updateCard(true)+'</div>')};
// Home: the link and a way out under the tiles (BACK on home only puts the app in the background).
function renderHomeLink(){const s=H.state,box=hostBox('homeLink','tiles','afterend');
 setHtml(box,H.inApp&&KIND[s.kind]?`<div class="card conn" style="margin-top:12px"><div class="top"><span class="icbox">${ic(KIND[s.kind][0],'acc')}</span><div><b>${L`Подключено · ${KIND[s.kind][1]}`}</b><small>${esc(s.label||'')}</small></div><button class="btn danger" data-hconn="off">${L`Отключиться`}</button></div></div>`:'')}
document.addEventListener('click',e=>{const t=e.target.closest('[data-hconn]');if(!t)return;
 if(t.dataset.hconn==='off'){N.disconnect();return}
 const c=conn||{};N.disconnect();setTimeout(()=>connect({kind:'wifi',ssid:c.ssid,password:c.password,address:'192.168.4.1'}),300)});

// Switches: over USB or BLE the access point is not this page's link; BLE off ends a BLE link.
const pageToggle=window.toggle;
window.toggle=async function(which){const k=H.state.kind;
 if(which==='wifi'&&k!=='wifi'){try{const r=await command('wifi');notify(r.includes('on')?L('Точка доступа включена'):L('Точка доступа выключена'));await refresh();await loadConnections()}catch(e){notify(e.message,'bad')}return}
 if(which==='wifi'){if(Date.now()-wifiArmed>5000){wifiArmed=Date.now();notify(L('Нажмите ещё раз: приложение потеряет связь по Wi-Fi. Включить точку снова можно на устройстве или по USB/Bluetooth'),'warn');return}
  try{await command('wifi');N.disconnect();notify(L('Точка доступа выключается'),'warn')}catch(e){notify(e.message,'bad')}return}
 if(which==='ble'&&k==='ble'&&status.ble){if(Date.now()-H.armed>5000){H.armed=Date.now();notify(L('Нажмите ещё раз: Bluetooth устройства выключится, приложение потеряет связь'),'warn');return}
  try{await command('ble')}catch{}N.disconnect();return}
 return pageToggle(which)};

// Files: the page's downloads (map packages, settings) go to an Android save dialog.
window.download=function(data,name,type){const bytes=typeof data==='string'?new TextEncoder().encode(data):data instanceof Uint8Array?data:new Uint8Array(data);N.saveFile(name,type||'application/octet-stream',toBase64(bytes))};

// Map areas in the phone (MainActivity → MapStore): the page's «Карты» save, list and show them, with or without a board.
function mapReply(t){t=String(t);if(t.startsWith('ERR '))throw Error(L`Карта в телефоне: ${t.slice(4)}`);return t}
window.phoneMaps={
 storages(){return JSON.parse(mapReply(N.mapStorages()))},
 areas(){return JSON.parse(mapReply(N.mapAreas()))},
 // The MMT1 file of a tile (of one area, or of the newest area that has it) or null.
 async tile(z,x,y,area){const r=await netFetch(`/phone-maps/tile?z=${z}&x=${x}&y=${y}${area?'&area='+encodeURIComponent(area):''}`);return r.ok?new Uint8Array(await r.arrayBuffer()):null},
 async save(storage,manifest,count,get,progress){const id=mapReply(N.mapBegin(storage,JSON.stringify(manifest)));
  try{for(let i=0;i<count;i++){const r=await get(i);mapReply(N.mapTile(id,r.z,r.x,r.y,toBase64(r.data)));progress(i+1,count)}return JSON.parse(mapReply(N.mapFinish(id)))}
  catch(e){N.mapDelete(id);throw e}},
 remove(id){mapReply(N.mapDelete(id))},
 tiles(id){return JSON.parse(mapReply(N.mapTiles(id)))}};

// Map upload over Bluetooth: about 2 KB/s, so a large package or phone area asks once more.
function bleSlow(bytes){if(H.state.kind!=='ble'||bytes<=300000||Date.now()-H.armed<=8000)return false;H.armed=Date.now();notify(L`По Bluetooth ${(bytes/1048576).toFixed(1)} МБ займут около ${Math.ceil(bytes/2048/60)} мин; по USB или Wi-Fi намного быстрее. Нажмите ещё раз, чтобы начать`,'warn');return true}
const pageUpload=window.uploadMap,pageToSd=window.phoneToSd;
window.uploadMap=function(){const f=$('mapFile').files[0];if(f&&$('uploadDest').value==='sd'&&bleSlow(f.size))return;return pageUpload()};
window.phoneToSd=function(id){const a=phoneAreas.find(x=>x.id===id);if(a&&bleSlow(a.bytes||0))return;return pageToSd(id)};
$('uploadMap').onclick=window.uploadMap;
$('saveBuilder').hidden=true; // the app itself prepares maps: no need to save the page
$('uploader').querySelector('p').textContent=L('Выберите файл .mmmap: карта сохранится в телефоне или передастся на SD устройства по текущему подключению. Интернет для этого не нужен.');
$('builder').querySelector('p').textContent=L('Нужен интернет на телефоне. Карта сохранится в телефоне и будет работать без интернета с любой платой; на SD устройства — и на экране M9.');
// Without a board: the phone's maps (and preparing new ones) from the connection screen.
$('standalone').closest('details').querySelector('summary').textContent=L('Карты без устройства');
$('standalone').closest('details').querySelector('p').textContent=L('Карты, сохранённые в телефоне, и подготовка новых. Для подготовки нужен интернет.');
$('standalone').textContent=L('Открыть карты');

H.upd=JSON.parse(N.updateInfo());
H.prefs=JSON.parse(N.prefs());const last=H.prefs.last;
H.tab=last&&KIND[last.kind]&&last.kind!=='tcp'?last.kind:'usb';
loginHud();renderPanel();N.scan('usb');if(H.tab!=='usb'&&H.tab==='ble'&&last)H.done.ble=false;
const s=JSON.parse(N.state());if(s.state!=='idle')MeshHost.state(s);
for(const v of document.querySelectorAll('.version'))v.textContent=N.version();
// Message taps: a context menu «Ответить» / «Путь пакета» on any tap over the message row
// (host.css stretches .bubble to the full row width). «Ответить» puts the "@[Name] " mention
// of MeshCore apps into the input; the menu replaces the page's tap on the meta line.
// Under own messages the hop count of the route joins the ticks and time.
let mmDown=null,mmMsg=null,mmTimer=null,mmLong=false;
function mmMenu(){let m=$('mmMenu');if(!m){document.body.insertAdjacentHTML('beforeend',
 `<div id="mmMenu" class="mm-menu" hidden><div class="mm-back"></div><div class="mm-sheet">
  <div class="mm-title">${L('Сообщение')}</div>
  <button class="mm-item" data-mm="reply">${L('Ответить')}</button>
  <button class="mm-item" data-mm="path">${L('Путь пакета')}</button></div></div>`);m=$('mmMenu')}return m}
// A compact popover next to the message (like Telegram/WhatsApp) over a dimmed
// backdrop: the title «Сообщение», then the two actions. It opens above the
// row when there is room and below it otherwise, clamped to the screen.
function mmOpen(msg,row){mmMsg=msg;const m=mmMenu();m.hidden=false;
 m.querySelector('[data-mm="reply"]').hidden=!!msg.outgoing;
 const sheet=m.querySelector('.mm-sheet'),r=row.getBoundingClientRect();
 sheet.style.visibility='hidden';sheet.style.left='0';sheet.style.top='0';
 const w=sheet.offsetWidth,h=sheet.offsetHeight,mid=innerWidth/2;
 // mostly centred for the thumb, with a gentle shift toward the bubble side
 const bias=Math.max(-0.3,Math.min(0.3,((r.left+r.right)/2-mid)/(innerWidth/2)));
 const x=Math.max(8,Math.min(innerWidth-w-8,mid-w/2+bias*(innerWidth-w-16)/2));
 const y=r.top-h-8<8?Math.min(innerHeight-h-8,r.bottom+8):r.top-h-8;
 sheet.style.left=x+'px';sheet.style.top=y+'px';
 sheet.style.visibility=''}
function mmClose(){mmMsg=null;$('mmMenu').hidden=true}
function mmRow(e){const inRow=e.target.closest('#messages .bubble[data-i]');
 if(inRow&&e.target.closest('button,a,[data-invite],[data-copy]'))return null;
 // the tap area is the whole row: outside the bubble (empty left/right) the
 // nearest row by Y is the message, as if the row were one big button
 return inRow||(()=>{if(!e.target.closest('#messages'))return null;
  const rows=[...$('messages').querySelectorAll('.bubble[data-i]')];
  return rows.find(r=>{const q=r.getBoundingClientRect();return e.clientY>=q.top&&e.clientY<=q.bottom})||null})()}
document.addEventListener('pointerdown',e=>{const b=mmRow(e);
 mmDown=b?{x:e.clientX,y:e.clientY,b}:null;
 clearTimeout(mmTimer);mmLong=false;
 if(mmDown)mmTimer=setTimeout(()=>{ // a long tap opens the same menu
  mmLong=true;
  const msg=chatRows[mmDown.b.dataset.i];if(msg)mmOpen(msg,mmDown.b)},450)},true);
document.addEventListener('pointermove',e=>{if(mmDown&&(Math.abs(e.clientX-mmDown.x)>10||Math.abs(e.clientY-mmDown.y)>10))clearTimeout(mmTimer)},true);
document.addEventListener('pointerup',()=>clearTimeout(mmTimer),true);
document.addEventListener('pointercancel',()=>clearTimeout(mmTimer),true);
document.addEventListener('click',e=>{
 if(mmLong){mmLong=false;e.preventDefault();e.stopPropagation();return}
 const item=e.target.closest('[data-mm]');
 if(item){const msg=mmMsg;mmClose();if(!msg)return;
  if(item.dataset.mm==='path')go('msg/'+encodeURIComponent(msgKey(msg)));
  else{const name=msg.outgoing?'':msg.name||nodeName(recipient),tag=name?'@['+name+'] ':'',inp=$('text');
   inp.focus();const s=inp.selectionStart??inp.value.length,t=inp.selectionEnd??s;
   inp.value=inp.value.slice(0,s)+tag+inp.value.slice(t);inp.setSelectionRange(s+tag.length,s+tag.length);
   byteCount();autosize()}
  return}
 if(e.target.closest('.mm-back')){mmClose();return}
 const inRow=e.target.closest('#messages .bubble[data-i]');
 if(inRow&&e.target.closest('button,a,[data-invite],[data-copy]'))return;
 // the tap area is the whole row: outside the bubble (empty left/right) the
 // nearest row by Y is the message, as if the row were one big button
 const b=inRow||(()=>{if(!e.target.closest('#messages'))return null;
  const rows=[...$('messages').querySelectorAll('.bubble[data-i]')];
  return rows.find(r=>{const q=r.getBoundingClientRect();return e.clientY>=q.top&&e.clientY<=q.bottom})||null})();
 if(!b)return;
 if(mmDown&&Math.abs(e.clientX-mmDown.x)+Math.abs(e.clientY-mmDown.y)>12)return; // a swipe keeps the page's reply
 const msg=chatRows[b.dataset.i];if(!msg)return;
 e.preventDefault();e.stopPropagation();
 mmOpen(msg,b)},true);
document.addEventListener('dblclick',e=>{if(e.target.closest('#messages .bubble[data-i]')){e.preventDefault();e.stopPropagation()}},true);
function mmHops(){const box=$('messages');if(!box||box.hidden)return;
 for(const b of box.querySelectorAll('.bubble.out[data-i]')){
  if(b.querySelector('.mm-hops'))continue;
  const m=chatRows[b.dataset.i];if(!m)continue;
  // the same hop count the packet path window shows: hops, else the path
  // count, else the repeaters whose copies were heard
  const count=m.path_len!=null?m.path_len&63:null,hops=m.hops??count;
  const repeats=(m.echoes&&m.echoes.length)||m.heard||0;
  if(hops==null&&!repeats)continue; // not sent yet: no route to count
  const txt=hops!=null?(hops?'→ '+plural(hops,'хоп','хопа','хопов'):'→ '+L('напрямую')):'→ '+L`через ${repeats} ретр.`;
  b.insertAdjacentHTML('beforeend',`<div class="mm-hops">${txt}</div>`)}}
new MutationObserver(()=>mmHops()).observe($('messages'),{childList:true,subtree:true});
mmHops();
})();
