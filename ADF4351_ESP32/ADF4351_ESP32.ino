#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

// ESP32 DevKit V1 38-pin wiring:
// ADF CLK->GPIO18, DAT->GPIO23, LE->GPIO27, CE->GPIO17,
// LD->GPIO16, MUX->GPIO34 (input only), GND->GND.
static constexpr uint8_t PIN_CLK=18, PIN_DAT=23, PIN_LE=27, PIN_CE=17, PIN_LD=16, PIN_MUX=34;
static constexpr double REF_HZ=25000000.0; // verify the fitted reference oscillator
static constexpr uint16_t MOD=4095;
static constexpr double FMIN=35.0, FMAX=4400.0, VCOMIN=2200000000.0, VCOMAX=4400000000.0;

const char* SSID="ADF4351-ESP32";
const char* PASSWORD="adf4351rf";
IPAddress apIP(192,168,4,2), gateway(192,168,4,2), subnet(255,255,255,0);
WebServer server(80);
Preferences prefs;
uint32_t regv[6]={};
double freqMHz=1000, sweepStart=100, sweepStop=200, sweepStep=1;
uint32_t sweepMs=100, lastSweep=0;
uint8_t powerCode=3, divider=1;
bool rf=true, sweep=false;

void writeReg(uint32_t v){
  digitalWrite(PIN_LE,LOW);
  for(int b=31;b>=0;--b){digitalWrite(PIN_CLK,LOW);digitalWrite(PIN_DAT,(v>>b)&1U);digitalWrite(PIN_CLK,HIGH);}
  digitalWrite(PIN_CLK,LOW);digitalWrite(PIN_LE,HIGH);delayMicroseconds(2);digitalWrite(PIN_LE,LOW);
}
void writeAll(){for(int i=5;i>=0;--i)writeReg(regv[i]);}

bool tune(double mhz){
  if(!isfinite(mhz)||mhz<FMIN||mhz>FMAX)return false;
  double vco=mhz*1e6;uint8_t dc=0;
  while(vco<VCOMIN&&dc<6){vco*=2;++dc;}
  if(vco<VCOMIN||vco>VCOMAX)return false;
  double n=vco/REF_HZ;uint32_t ni=(uint32_t)floor(n);
  uint32_t fr=(uint32_t)llround((n-ni)*MOD);
  if(fr>=MOD){fr=0;++ni;}
  if(ni<23||ni>65535)return false;
  regv[0]=(ni<<15)|(fr<<3);
  regv[1]=(1UL<<27)|(1UL<<15)|((uint32_t)MOD<<3)|1UL;
  // MUXOUT = digital lock detect; R counter=1; positive PFD polarity.
  regv[2]=(6UL<<26)|(1UL<<14)|(7UL<<9)|(1UL<<6)|2UL;
  regv[3]=3UL;
  // Band-select divider 200: 125 kHz at 25 MHz PFD.
  regv[4]=(1UL<<23)|((uint32_t)dc<<20)|(200UL<<12)|(rf?(1UL<<5):0UL)|((uint32_t)powerCode<<3)|4UL;
  regv[5]=0x00580005UL;
  freqMHz=mhz;divider=(uint8_t)(1U<<dc);writeAll();delay(30);return true;
}
void setRF(bool on){rf=on;if(on)regv[4]|=(1UL<<5);else regv[4]&=~(1UL<<5);writeReg(regv[4]);}
double num(const String& s,const char* key,double def){
  String k=String("\"")+key+"\"";int p=s.indexOf(k);if(p<0)return def;p=s.indexOf(':',p);if(p<0)return def;
  int e=s.indexOf(',',p+1);if(e<0)e=s.indexOf('}',p+1);if(e<0)return def;return s.substring(p+1,e).toDouble();
}
bool boolean(const String& s,const char* key,bool def){
  String k=String("\"")+key+"\"";int p=s.indexOf(k);if(p<0)return def;p=s.indexOf(':',p);if(p<0)return def;
  String v=s.substring(p+1);v.trim();if(v.startsWith("true"))return true;if(v.startsWith("false"))return false;return def;
}
void error(int code,const String& msg){server.send(code,"application/json",String("{\"error\":\"")+msg+"\"}");}
String status(){
  String s="{\"frequency_mhz\":"+String(freqMHz,6)+",\"power_index\":"+String(powerCode)+",\"power_dbm\":"+String(-4+3*powerCode);
  s+=",\"rf_enabled\":"+String(rf?"true":"false")+",\"pll_locked\":"+String(digitalRead(PIN_LD)?"true":"false");
  s+=",\"mux_high\":"+String(digitalRead(PIN_MUX)?"true":"false")+",\"sweep_running\":"+String(sweep?"true":"false");
  s+=",\"divider\":"+String(divider)+",\"ip\":\"192.168.4.2\"}";return s;
}
const char HTML[] PROGMEM=R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ADF4351 Generator</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{font:16px system-ui;background:#10151d;color:#e8edf5;margin:0;padding:16px}
main{max-width:760px;margin:auto}.card{background:#1b2430;border:1px solid #344153;border-radius:14px;padding:16px;margin:12px 0}
h1{font-size:23px}.freq{font-size:clamp(27px,7vw,38px);font-weight:750;margin:10px 0}
input,select,button{font:inherit;border-radius:9px;padding:11px;border:1px solid #53647a;background:#111923;color:#fff;min-width:0}
input{width:100%;margin:6px 0 12px}button{cursor:pointer;background:#245da8;border:0}button.alt{background:#465366}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row>*{flex:1}.muted,small{color:#a9b6c8}
.good{color:#66df9b}.bad{color:#ff8b8b}.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
@media(max-width:480px){.grid{grid-template-columns:1fr}.card{padding:13px}}</style></head><body><main>
<h1>ADF4351 Signal Generator</h1><p class="muted">Wi-Fi control · <b>http://192.168.4.2</b></p>
<section class="card"><small>OUTPUT FREQUENCY</small><div class="freq" id="freq">---</div>
<div class="row"><input id="mhz" type="number" min="35" max="4400" step="0.000001" value="1000"><button onclick="setFreq()">Set MHz</button></div>
<div class="row"><button class="alt" onclick="nudge(-1)">− 1 MHz</button><button class="alt" onclick="nudge(1)">+ 1 MHz</button></div></section>
<section class="card"><b>RF output</b><div class="row" style="margin:10px 0"><button onclick="rfSet(true)">RF ON</button><button class="alt" onclick="rfSet(false)">RF OFF</button></div>
<label>Output power</label><select id="power" onchange="setPower()" style="width:100%;margin-top:8px">
<option value="0">−4 dBm</option><option value="1">−1 dBm</option><option value="2">+2 dBm</option><option value="3">+5 dBm</option></select>
<p>PLL: <b id="pll">---</b> · RF: <b id="rfstate">---</b></p><p class="muted">MUXOUT: <span id="mux">---</span> · Divider: <span id="divider">---</span></p></section>
<section class="card"><b>Frequency sweep</b><div class="grid">
<label>Start MHz<input id="start" type="number" value="100" step="0.001"></label><label>Stop MHz<input id="stop" type="number" value="200" step="0.001"></label>
<label>Step MHz<input id="step" type="number" value="1" step="0.001"></label><label>Interval ms<input id="interval" type="number" value="100" min="20" max="60000"></label></div>
<div class="row"><button onclick="sweepSet(true)">Start sweep</button><button class="alt" onclick="sweepSet(false)">Stop sweep</button></div></section>
<p class="muted">Join Wi-Fi <b>ADF4351-ESP32</b> (password <b>adf4351rf</b>), then open <b>http://192.168.4.2</b> on your phone or PC.</p>
</main><script>
async function api(path,data){let r=await fetch(path,{method:data?'POST':'GET',headers:{'Content-Type':'application/json'},body:data?JSON.stringify(data):undefined});let j=await r.json();if(!r.ok)alert(j.error||'Request failed');return j}
async function refresh(){try{let s=await api('/api/status');document.getElementById('freq').textContent=Number(s.frequency_mhz).toFixed(6)+' MHz';document.getElementById('mhz').value=s.frequency_mhz;document.getElementById('power').value=s.power_index;let p=document.getElementById('pll');p.textContent=s.pll_locked?'LOCKED':'UNLOCKED';p.className=s.pll_locked?'good':'bad';document.getElementById('rfstate').textContent=s.rf_enabled?'ON':'OFF';document.getElementById('mux').textContent=s.mux_high?'HIGH':'LOW';document.getElementById('divider').textContent='÷'+s.divider}catch(e){}}
async function setFreq(){await api('/api/set',{mhz:Number(document.getElementById('mhz').value)});refresh()}
async function nudge(d){let e=document.getElementById('mhz');e.value=Number(e.value)+d;setFreq()}
async function rfSet(enabled){await api('/api/rf',{enabled});refresh()}
async function setPower(){await api('/api/power',{index:Number(document.getElementById('power').value)});refresh()}
async function sweepSet(enabled){await api('/api/sweep',{enabled,start:Number(start.value),stop:Number(stop.value),step:Number(step.value),interval:Number(interval.value)});refresh()}
setInterval(refresh,1000);refresh();</script></body></html>)HTML";

void setupRoutes(){
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",HTML);});
 server.on("/api/status",HTTP_GET,[]{server.send(200,"application/json",status());});
 server.on("/api/set",HTTP_POST,[]{double f=num(server.arg("plain"),"mhz",-1);if(!tune(f))return error(400,"Frequency must be 35..4400 MHz");prefs.putDouble("freq",freqMHz);server.send(200,"application/json",status());});
 server.on("/api/rf",HTTP_POST,[]{setRF(boolean(server.arg("plain"),"enabled",false));prefs.putBool("rf",rf);server.send(200,"application/json",status());});
 server.on("/api/power",HTTP_POST,[]{int p=(int)num(server.arg("plain"),"index",-1);if(p<0||p>3)return error(400,"Power index must be 0..3");powerCode=p;tune(freqMHz);prefs.putUInt("power",powerCode);server.send(200,"application/json",status());});
 server.on("/api/sweep",HTTP_POST,[]{String b=server.arg("plain");if(!boolean(b,"enabled",false)){sweep=false;return server.send(200,"application/json",status());}
 double a=num(b,"start",100),z=num(b,"stop",200),st=num(b,"step",1),iv=num(b,"interval",100);
 if(a<FMIN||z>FMAX||a>=z||st<=0||st>(z-a)||iv<20||iv>60000)return error(400,"Invalid sweep parameters");
 sweepStart=a;sweepStop=z;sweepStep=st;sweepMs=(uint32_t)iv;sweep=tune(sweepStart);lastSweep=millis();server.send(200,"application/json",status());});
}
void setup(){
 pinMode(PIN_CLK,OUTPUT);pinMode(PIN_DAT,OUTPUT);pinMode(PIN_LE,OUTPUT);pinMode(PIN_CE,OUTPUT);pinMode(PIN_LD,INPUT);pinMode(PIN_MUX,INPUT);
 digitalWrite(PIN_CLK,LOW);digitalWrite(PIN_DAT,LOW);digitalWrite(PIN_LE,LOW);digitalWrite(PIN_CE,HIGH);
 prefs.begin("adf4351",false);freqMHz=prefs.getDouble("freq",1000);powerCode=(uint8_t)prefs.getUInt("power",3);if(powerCode>3)powerCode=3;rf=prefs.getBool("rf",true);
 WiFi.mode(WIFI_AP);WiFi.softAPConfig(apIP,gateway,subnet);WiFi.softAP(SSID,PASSWORD);setupRoutes();server.begin();
 if(tune(freqMHz))setRF(rf);
}
void loop(){server.handleClient();if(sweep&&millis()-lastSweep>=sweepMs){lastSweep=millis();double next=freqMHz+sweepStep;if(next>sweepStop+1e-9)next=sweepStart;if(!tune(next))sweep=false;}}
