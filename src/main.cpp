#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

static constexpr uint8_t PIN_CLK=18, PIN_DAT=23, PIN_LE=27, PIN_CE=17, PIN_LD=16, PIN_MUX=34, PIN_CLK_MON=35;
static constexpr double REF_HZ=10410500.0, FMIN=35.0, FMAX=4400.0, VCOMIN=2200000000.0, VCOMAX=4400000000.0;
static constexpr uint16_t MOD=4095;
const char* SSID="ADF4351-ESP32";
const char* PASSWORD="adf4351rf";
IPAddress apIP(192,168,4,2), gateway(192,168,4,2), subnet(255,255,255,0);
WebServer server(80);
Preferences prefs;
uint32_t regv[6]={};
double freqMHz=1000, sweepStart=100, sweepStop=200, sweepStep=1;
uint32_t sweepMs=100, lastSweep=0, lastWriteMs=0, writeCount=0, lockChanges=0, lastLockSample=0;
uint8_t powerCode=3, divider=1, spiHalfUs=0;
bool rf=true, sweep=false, autoClock=false, lastLock=false;
volatile uint32_t clkEdges=0; uint32_t clkSampleAt=0, clkLastCount=0; double clkMeasuredHz=0;
void IRAM_ATTR onClkEdge(){ ++clkEdges; }
String lastError="";

void writeReg(uint32_t v){
  digitalWrite(PIN_LE,LOW);
  for(int b=31;b>=0;--b){
    digitalWrite(PIN_CLK,LOW); digitalWrite(PIN_DAT,(v>>b)&1U);
    if(spiHalfUs) delayMicroseconds(spiHalfUs);
    digitalWrite(PIN_CLK,HIGH);
    if(spiHalfUs) delayMicroseconds(spiHalfUs);
  }
  digitalWrite(PIN_CLK,LOW); digitalWrite(PIN_LE,HIGH); delayMicroseconds(2); digitalWrite(PIN_LE,LOW);
  ++writeCount; lastWriteMs=millis();
}
void writeAll(){for(int i=5;i>=0;--i)writeReg(regv[i]);}
bool tune(double mhz){
  if(!isfinite(mhz)||mhz<FMIN||mhz>FMAX){lastError="Frequency outside 35..4400 MHz";return false;}
  double vco=mhz*1e6; uint8_t dc=0;
  while(vco<VCOMIN&&dc<6){vco*=2;++dc;}
  if(vco<VCOMIN||vco>VCOMAX){lastError="VCO frequency out of range";return false;}
  double n=vco/REF_HZ; uint32_t ni=(uint32_t)floor(n);
  uint32_t fr=(uint32_t)llround((n-ni)*MOD);
  if(fr>=MOD){fr=0;++ni;}
  if(ni<23||ni>65535){lastError="Invalid integer divider";return false;}
  regv[0]=(ni<<15)|(fr<<3);
  regv[1]=(1UL<<27)|(1UL<<15)|((uint32_t)MOD<<3)|1UL;
  regv[2]=(6UL<<26)|(1UL<<14)|(7UL<<9)|(1UL<<6)|2UL;
  regv[3]=3UL;
  regv[4]=(1UL<<23)|((uint32_t)dc<<20)|(200UL<<12)|(rf?(1UL<<5):0UL)|((uint32_t)powerCode<<3)|4UL;
  regv[5]=0x00580005UL;
  freqMHz=mhz; divider=(uint8_t)(1U<<dc); writeAll(); delay(35); lastError=""; return true;
}
void setRF(bool on){rf=on;if(on)regv[4]|=(1UL<<5);else regv[4]&=~(1UL<<5);writeReg(regv[4]);}
double num(const String&s,const char*key,double def){String k=String("\"")+key+"\"";int p=s.indexOf(k);if(p<0)return def;p=s.indexOf(':',p);if(p<0)return def;int e=s.indexOf(',',p+1);if(e<0)e=s.indexOf('}',p+1);return e<0?def:s.substring(p+1,e).toDouble();}
bool jsonBool(const String&s,const char*key,bool def){String k=String("\"")+key+"\"";int p=s.indexOf(k);if(p<0)return def;p=s.indexOf(':',p);if(p<0)return def;String v=s.substring(p+1);v.trim();if(v.startsWith("true"))return true;if(v.startsWith("false"))return false;return def;}
void error(int c,const String&m){server.send(c,"application/json",String("{\"error\":\"")+m+"\"}");}
String status(){
 bool ld=digitalRead(PIN_LD); if(ld!=lastLock){++lockChanges;lastLock=ld;}
 String s="{\"frequency_mhz\":"+String(freqMHz,6)+",\"power_index\":"+String(powerCode)+",\"power_dbm\":"+String(-4+3*powerCode);
 s+=",\"rf_enabled\":"+String(rf?"true":"false")+",\"pll_locked\":"+String(ld?"true":"false");
 s+=",\"mux_high\":"+String(digitalRead(PIN_MUX)?"true":"false")+",\"sweep_running\":"+String(sweep?"true":"false");
 s+=",\"divider\":"+String(divider)+",\"ip\":\"192.168.4.2\",\"spi_half_us\":"+String(spiHalfUs);
 s+=",\"spi_khz\":"+String(spiHalfUs?500.0/spiHalfUs:0,1)+",\"auto_clock\":"+String(autoClock?"true":"false");
 s+=",\"writes\":"+String(writeCount)+",\"last_write_age_ms\":"+String(millis()-lastWriteMs);
 s+=",\"lock_changes\":"+String(lockChanges)+",\"uptime_s\":"+String(millis()/1000);
 uint32_t now=millis(); if(now-clkSampleAt>=1000){ noInterrupts(); uint32_t n=clkEdges; interrupts(); clkMeasuredHz=(double)(n-clkLastCount)*1000.0/(now-clkSampleAt); clkLastCount=n; clkSampleAt=now; }
 s+=",\"clk_edges\":"+String(clkEdges)+",\"clk_hz\":"+String(clkMeasuredHz,1)+",\"clk_detected\":"+String(clkMeasuredHz>0?"true":"false");
 s+=",\"wifi_clients\":"+String(WiFi.softAPgetStationNum())+",\"heap\":"+String(ESP.getFreeHeap());
 s+=",\"chip\":\"ESP32\",\"last_error\":\""+lastError+"\"}";
 return s;
}
const char HTML[] PROGMEM=R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ADF4351 Generator</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{font:15px system-ui;background:#10151d;color:#e8edf5;margin:0;padding:14px}main{max-width:850px;margin:auto}
h1{font-size:23px;margin:8px 0}.muted,small{color:#a9b6c8}.tabs{display:flex;gap:7px;overflow:auto;margin:15px 0}.tabs button{white-space:nowrap}
.card{background:#1b2430;border:1px solid #344153;border-radius:14px;padding:16px;margin:12px 0}.freq{font-size:clamp(27px,7vw,38px);font-weight:750;margin:10px 0}
input,select,button{font:inherit;border-radius:9px;padding:11px;border:1px solid #53647a;background:#111923;color:#fff;min-width:0}
input{width:100%;margin:6px 0 12px}button{cursor:pointer;background:#245da8;border:0}button.alt{background:#465366}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row>*{flex:1}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.hidden{display:none!important}.good{color:#66df9b}.bad{color:#ff8b8b}
.metric{background:#111923;border-radius:10px;padding:12px;min-width:0}.metric b{display:block;font-size:19px;overflow-wrap:anywhere}.preset{display:flex;gap:8px;align-items:center;margin:8px 0}.preset>*{flex:1}
@media(max-width:480px){.grid{grid-template-columns:1fr}.card{padding:13px}.tabs button{padding:9px}}
</style></head><body><main><h1>Генератор сигналов ADF4351</h1><p class="muted">Управление по Wi-Fi · 192.168.4.2</p>
<nav class="tabs"><button onclick="tab('gen')">Генератор</button><button class="alt" onclick="tab('presets')">Пресеты</button><button class="alt" onclick="tab('monitor')">Мониторинг</button><button class="alt" onclick="tab('settings')">Настройки / О проекте</button></nav>
<div id="gen">
<section class="card"><small>ЧАСТОТА ВЫХОДА</small><div class="freq" id="freq">---</div><div class="row"><input id="mhz" type="number" min="35" max="4400" step="0.000001" value="1000"><button onclick="setFreq()">Установить</button></div><div class="row"><button class="alt" onclick="nudge(-1)">− 1 MHz</button><button class="alt" onclick="nudge(1)">+ 1 MHz</button></div></section>
<section class="card"><b>Выход RF</b><div class="row" style="margin:10px 0"><button onclick="rfSet(true)">ВКЛ</button><button class="alt" onclick="rfSet(false)">ВЫКЛ</button></div><label>Output power</label><select id="power" onchange="setPower()" style="width:100%;margin-top:8px"><option value="0">−4 dBm</option><option value="1">−1 dBm</option><option value="2">+2 dBm</option><option value="3">+5 dBm</option></select><p>PLL: <b id="pll">---</b> · RF: <b id="rfstate">---</b></p><p class="muted">MUXOUT: <span id="mux">---</span> · Divider: <span id="divider">---</span></p></section>
<section class="card"><b>Частотная развёртка</b><div class="grid"><label>Начальная, МГц<input id="start" type="number" value="100" step="0.001"></label><label>Конечная, МГц<input id="stop" type="number" value="200" step="0.001"></label><label>Шаг, МГц<input id="step" type="number" value="1" step="0.001"></label><label>Интервал, мс<input id="interval" type="number" value="100" min="20" max="60000"></label></div><div class="row"><button onclick="sweepSet(true)">Запустить</button><button class="alt" onclick="sweepSet(false)">Остановить</button></div></section></div>
<div id="presets" class="hidden"><section class="card"><b>Сохранённые настройки</b><p class="muted">Сохранение частоты и мощности во внутренней памяти ESP32.</p><div id="presetList"></div></section></div>
<div id="monitor" class="hidden"><section class="card"><div class="row"><b>Монитор соединения с ADF4351</b><button class="alt" onclick="refresh()">Refresh</button></div><p class="muted">Диагностика обновляется каждую секунду, пока открыта эта вкладка.</p><div class="grid">
<div class="metric">Захват PLL<b id="mLock">---</b></div><div class="metric">MUXOUT<b id="mMux">---</b></div>
<div class="metric">CLK на контрольном входе<b id="mClk">---</b></div><div class="metric">Частота CLK<b id="mSpi">---</b></div><div class="metric">Записи регистров<b id="mWrites">---</b></div>
<div class="metric">После последней записи<b id="mAge">---</b></div><div class="metric">Изменения LOCK<b id="mChanges">---</b></div>
<div class="metric">Клиенты Wi-Fi<b id="mClients">---</b></div><div class="metric">Время работы ESP32<b id="mUptime">---</b></div>
<div class="metric">Свободная память<b id="mHeap">---</b></div><div class="metric">RF output<b id="mRF">---</b></div></div>
<p id="diag" class="muted">Ожидание диагностики…</p><p class="muted">Важно: ADF4351 не возвращает подтверждение записи или содержимое регистров. Счётчик импульсов контролирует выход ESP32, а LOCK — косвенный признак работы PLL.</p></section></div>
<div id="settings" class="hidden"><section class="card"><b>Настройки последовательного интерфейса</b><p class="muted">Настройка программного CLK. Меньший полупериод означает более высокую частоту.</p><label>Скорость CLK<select id="spi" style="width:100%"><option value="0">Максимальная</option><option value="1">Около 500 кГц</option><option value="2">Около 250 кГц</option><option value="5">Около 100 кГц</option></select></label><div class="row"><button onclick="saveClock(false)">Применить</button><button class="alt" onclick="saveClock(true)">Автоподбор</button></div><p id="clockMsg" class="muted"></p></section>
<section class="card"><b>Информация об устройстве</b><p>Controller: ESP32 DevKit V1</p><p>RF synthesizer: Analog Devices ADF4351</p><p>Опорная частота в настройках: 10,4105 МГц</p><p>Control interface: 3-wire serial (CLK, DATA, LE), LD and MUX inputs</p><p>Точка доступа: ADF4351-ESP32 / 192.168.4.2</p><p class="muted">The fitted reference oscillator, carrier-board supply and logic-level compatibility must be verified on the actual hardware.</p></section></div>
<footer class="muted">Подключитесь к Wi-Fi <b>ADF4351-ESP32</b> (password <b>adf4351rf</b>) and open <b>http://192.168.4.2</b>.</footer></main>
<script>
let active='gen';function tab(t){active=t;['gen','presets','monitor','settings'].forEach(x=>document.getElementById(x).classList.toggle('hidden',x!==t));if(t==='presets')loadPresets();refresh()}
async function api(path,data){let r=await fetch(path,{method:data?'POST':'GET',headers:{'Content-Type':'application/json'},body:data?JSON.stringify(data):undefined});let j=await r.json();if(!r.ok)throw Error(j.error||'Request failed');return j}
async function act(fn){try{await fn();await refresh()}catch(e){alert(e.message)}}
async function refresh(){try{let s=await api('/api/status');document.getElementById('freq').textContent=Number(s.frequency_mhz).toFixed(6)+' MHz';if(document.activeElement!==document.getElementById('mhz'))document.getElementById('mhz').value=s.frequency_mhz;document.getElementById('power').value=s.power_index;let p=document.getElementById('pll');p.textContent=s.pll_locked?'ЗАХВАТ':'НЕТ ЗАХВАТА';p.className=s.pll_locked?'good':'bad';document.getElementById('rfstate').textContent=s.rf_enabled?'ВКЛ':'ВЫКЛ';document.getElementById('mux').textContent=s.mux_high?'ВЫСОКИЙ':'НИЗКИЙ';document.getElementById('divider').textContent='÷'+s.divider;
[['mLock',s.pll_locked?'ЗАХВАТ':'НЕТ ЗАХВАТА'],['mMux',s.mux_high?'HIGH':'LOW'],['mClk',s.clk_detected?'Импульсы есть':'Импульсов нет'],['mSpi',s.clk_hz>=1?(s.clk_hz>=1000?(s.clk_hz/1000).toFixed(2)+' кГц':s.clk_hz.toFixed(0)+' Гц'):'Нет сигнала'],['mWrites',s.writes],['mAge',s.last_write_age_ms+' мс'],['mChanges',s.lock_changes],['mClients',s.wifi_clients],['mUptime',s.uptime_s+' с'],['mHeap',s.heap+' байт'],['mRF',s.rf_enabled?'ВКЛ':'ВЫКЛ']].forEach(([id,v])=>{let e=document.getElementById(id);if(e)e.textContent=v});
document.getElementById('diag').textContent=s.last_error?'Ошибка прошивки: '+s.last_error:(s.pll_locked?'PLL сообщает о захвате; передача команд выполнена.':'Нет сигнала захвата PLL. Проверьте опору, питание, соединения, регистры и фильтр петли.');document.getElementById('spi').value=s.spi_half_us}catch(e){}}
function setFreq(){const el=document.getElementById('mhz');const v=el.valueAsNumber;if(!Number.isFinite(v)||v<35||v>4400){alert('Введите частоту от 35 до 4400 МГц');return}act(()=>api('/api/set',{mhz:v}))}function nudge(d){const el=document.getElementById('mhz');el.value=el.valueAsNumber+d;setFreq()}
function rfSet(enabled){act(()=>api('/api/rf',{enabled}))}function setPower(){act(()=>api('/api/power',{index:Number(document.getElementById('power').value)}))}
function sweepSet(enabled){if(!enabled){act(()=>api('/api/sweep',{enabled:false}));return}const a=document.getElementById('start').valueAsNumber,z=document.getElementById('stop').valueAsNumber,st=document.getElementById('step').valueAsNumber,iv=document.getElementById('interval').valueAsNumber;if(![a,z,st,iv].every(Number.isFinite)||a<35||z>4400||a>=z||st<=0||iv<20||iv>60000){alert('Проверьте параметры: 35 ≤ начало < конец ≤ 4400 МГц; шаг > 0; интервал 20–60000 мс');return}act(()=>api('/api/sweep',{enabled:true,start:a,stop:z,step:st,interval:iv}))}
async function loadPresets(){try{let a=await api('/api/presets');const list=document.getElementById('presetList');list.innerHTML='';a.presets.forEach((p,i)=>{let d=document.createElement('div');d.className='preset';d.innerHTML='<span>Ячейка '+(i+1)+': '+(p.valid?p.mhz.toFixed(3)+' МГц / '+p.power+' дБм':'Пусто')+'</span><button>Загрузить</button><button class="alt">Сохранить</button>';d.children[1].onclick=()=>act(()=>api('/api/preset/load',{slot:i}));d.children[2].onclick=()=>act(()=>api('/api/preset/save',{slot:i})).then(loadPresets);list.appendChild(d)})}catch(e){alert(e.message)}}
async function saveClock(auto){await act(async()=>{let j=await api('/api/clock',{half_us:Number(document.getElementById('spi').value),auto});clockMsg.textContent=j.message||'Clock setting applied';})}
setInterval(refresh,1000);refresh();
</script></body></html>)HTML";

void setupRoutes(){
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",HTML);});
 server.on("/api/status",HTTP_GET,[]{server.send(200,"application/json",status());});
 server.on("/api/set",HTTP_POST,[]{double f=num(server.arg("plain"),"mhz",-1);if(!tune(f))return error(400,lastError);prefs.putDouble("freq",freqMHz);server.send(200,"application/json",status());});
 server.on("/api/rf",HTTP_POST,[]{setRF(jsonBool(server.arg("plain"),"enabled",false));prefs.putBool("rf",rf);server.send(200,"application/json",status());});
 server.on("/api/power",HTTP_POST,[]{int p=(int)num(server.arg("plain"),"index",-1);if(p<0||p>3)return error(400,"Power index must be 0..3");powerCode=p;tune(freqMHz);prefs.putUInt("power",powerCode);server.send(200,"application/json",status());});
 server.on("/api/sweep",HTTP_POST,[]{String b=server.arg("plain");if(!jsonBool(b,"enabled",false)){sweep=false;return server.send(200,"application/json",status());}double a=num(b,"start",100),z=num(b,"stop",200),st=num(b,"step",1),iv=num(b,"interval",100);if(!isfinite(a)||!isfinite(z)||!isfinite(st)||!isfinite(iv)||a<FMIN||z>FMAX||a>=z||st<=0||iv<20||iv>60000)return error(400,"Invalid sweep parameters");sweepStart=a;sweepStop=z;sweepStep=st;sweepMs=(uint32_t)iv;sweep=tune(sweepStart);lastSweep=millis();server.send(200,"application/json",status());});
 server.on("/api/presets",HTTP_GET,[]{String s="{\"presets\":[";for(int i=0;i<8;i++){if(i)s+=",";bool v=prefs.getBool((String("p")+i+"v").c_str(),false);s+="{\"valid\":"+String(v?"true":"false")+",\"mhz\":"+String(prefs.getDouble((String("p")+i+"f").c_str(),0),6)+",\"power\":"+String(-4+3*(int)prefs.getUInt((String("p")+i+"p").c_str(),3))+"}";}s+="]}";server.send(200,"application/json",s);});
 server.on("/api/preset/save",HTTP_POST,[]{int i=(int)num(server.arg("plain"),"slot",-1);if(i<0||i>7)return error(400,"Slot must be 0..7");prefs.putDouble((String("p")+i+"f").c_str(),freqMHz);prefs.putUInt((String("p")+i+"p").c_str(),powerCode);prefs.putBool((String("p")+i+"v").c_str(),true);server.send(200,"application/json",status());});
 server.on("/api/preset/load",HTTP_POST,[]{int i=(int)num(server.arg("plain"),"slot",-1);if(i<0||i>7||!prefs.getBool((String("p")+i+"v").c_str(),false))return error(400,"Preset is empty");double f=prefs.getDouble((String("p")+i+"f").c_str(),1000);uint32_t p=prefs.getUInt((String("p")+i+"p").c_str(),3);if(p>3)return error(400,"Invalid saved power");powerCode=p;if(!tune(f))return error(400,lastError);prefs.putDouble("freq",freqMHz);prefs.putUInt("power",powerCode);server.send(200,"application/json",status());});
 server.on("/api/clock",HTTP_POST,[]{String b=server.arg("plain");int h=(int)num(b,"half_us",0);bool a=jsonBool(b,"auto",false);if(!a&&h!=0&&h!=1&&h!=2&&h!=5)return error(400,"Allowed half periods: 0, 1, 2, 5 us");autoClock=a;if(a){uint8_t candidates[]={0,1,2,5};uint8_t chosen=spiHalfUs;bool found=false;for(uint8_t c:candidates){spiHalfUs=c;if(tune(freqMHz)){delay(100);if(digitalRead(PIN_LD)){chosen=c;found=true;break;}}}if(found)spiHalfUs=chosen;else {spiHalfUs=2;tune(freqMHz);}prefs.putUChar("spi",spiHalfUs);String msg=found?"Selected fastest tested clock with PLL lock (heuristic).":"No tested speed produced lock; using conservative 250 kHz setting.";server.send(200,"application/json",String("{\"message\":\"")+msg+"\"}");}else{spiHalfUs=h;prefs.putUChar("spi",spiHalfUs);tune(freqMHz);server.send(200,"application/json",String("{\"message\":\"CLK timing updated.\"}"));}});
}
void setup(){
 pinMode(PIN_CLK,OUTPUT);pinMode(PIN_DAT,OUTPUT);pinMode(PIN_LE,OUTPUT);pinMode(PIN_CE,OUTPUT);pinMode(PIN_LD,INPUT);pinMode(PIN_MUX,INPUT);pinMode(PIN_CLK_MON,INPUT);attachInterrupt(digitalPinToInterrupt(PIN_CLK_MON),onClkEdge,RISING);clkSampleAt=millis();
 digitalWrite(PIN_CLK,LOW);digitalWrite(PIN_DAT,LOW);digitalWrite(PIN_LE,LOW);digitalWrite(PIN_CE,HIGH);
 prefs.begin("adf4351",false);freqMHz=prefs.getDouble("freq",1000);powerCode=(uint8_t)prefs.getUInt("power",3);if(powerCode>3)powerCode=3;rf=prefs.getBool("rf",true);spiHalfUs=prefs.getUChar("spi",0);
 WiFi.mode(WIFI_AP);WiFi.softAPConfig(apIP,gateway,subnet);WiFi.softAP(SSID,PASSWORD);setupRoutes();server.begin();if(tune(freqMHz))setRF(rf);
 lastLock=digitalRead(PIN_LD);
}
void loop(){server.handleClient();if(sweep&&millis()-lastSweep>=sweepMs){lastSweep=millis();double next=freqMHz+sweepStep;if(next>sweepStop+1e-9)next=sweepStart;if(!tune(next))sweep=false;}}
