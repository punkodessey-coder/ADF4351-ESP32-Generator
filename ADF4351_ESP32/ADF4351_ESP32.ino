#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

// ESP32 DevKit V1 (38-pin) -> ADF4351 module
static constexpr uint8_t PIN_CLK = 18;
static constexpr uint8_t PIN_DAT = 23;
static constexpr uint8_t PIN_LE  = 27;
static constexpr uint8_t PIN_CE  = 17;
static constexpr uint8_t PIN_LD  = 16;
static constexpr uint8_t PIN_MUX = 34; // input only; MUXOUT is configured as digital lock detect

// The user's board photo shows a 25.000 MHz reference oscillator.
// Verify the actual fitted reference before relying on frequency accuracy.
static constexpr double REFERENCE_HZ = 25000000.0;
static constexpr uint16_t MODULUS = 4095;
static constexpr double MIN_OUTPUT_MHZ = 35.0;
static constexpr double MAX_OUTPUT_MHZ = 4400.0;
static constexpr double VCO_MIN_HZ = 2200000000.0;
static constexpr double VCO_MAX_HZ = 4400000000.0;

const char* AP_SSID = "ADF4351-ESP32";
const char* AP_PASSWORD = "adf4351rf";

WebServer server(80);
Preferences preferences;

double outputMHz = 1000.0;
double sweepStartMHz = 100.0;
double sweepStopMHz = 200.0;
double sweepStepMHz = 1.0;
uint32_t sweepIntervalMs = 100;
uint32_t lastSweepMs = 0;
bool sweepRunning = false;
bool rfEnabled = true;
uint8_t outputPower = 3; // ADF4351 codes: 0=-4, 1=-1, 2=+2, 3=+5 dBm (nominal)
uint8_t outputDivider = 1;
uint32_t registers[6] = {};

void writeRegister(uint32_t value) {
  digitalWrite(PIN_LE, LOW);
  for (int bit = 31; bit >= 0; --bit) {
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_DAT, (value >> bit) & 1U);
    digitalWrite(PIN_CLK, HIGH);
  }
  digitalWrite(PIN_CLK, LOW);
  digitalWrite(PIN_LE, HIGH);
  delayMicroseconds(2);
  digitalWrite(PIN_LE, LOW);
}

void writeRegisters() {
  // ADF4351 initialization order: R5, R4, R3, R2, R1, R0.
  for (int i = 5; i >= 0; --i) writeRegister(registers[i]);
}

bool setFrequency(double mhz) {
  if (!isfinite(mhz) || mhz < MIN_OUTPUT_MHZ || mhz > MAX_OUTPUT_MHZ) return false;

  double vcoHz = mhz * 1000000.0;
  uint8_t dividerCode = 0;
  while (vcoHz < VCO_MIN_HZ && dividerCode < 6) {
    vcoHz *= 2.0;
    ++dividerCode;
  }
  if (vcoHz < VCO_MIN_HZ || vcoHz > VCO_MAX_HZ) return false;

  // R counter=1, reference doubler/divide-by-2 disabled: PFD = 25 MHz.
  const double pfdHz = REFERENCE_HZ;
  const double n = vcoHz / pfdHz;
  uint32_t intValue = static_cast<uint32_t>(floor(n));
  uint32_t fracValue = static_cast<uint32_t>(llround((n - intValue) * MODULUS));
  if (fracValue >= MODULUS) {
    fracValue = 0;
    ++intValue;
  }
  // 8/9 prescaler is used; INT is comfortably above its minimum for this setup.
  if (intValue < 75 || intValue > 65535) return false;

  // R0: INT[30:15], FRAC[14:3], address 0.
  registers[0] = (intValue << 15) | (fracValue << 3);

  // R1: 8/9 prescaler, PHASE=1, MOD=4095, address 1.
  registers[1] = (1UL << 27) | (1UL << 15) | (static_cast<uint32_t>(MODULUS) << 3) | 1UL;

  // R2: MUXOUT=digital lock detect, R counter=1, positive phase detector polarity,
  // charge-pump current code 7. Confirm charge-pump/loop-filter suitability for the carrier.
  registers[2] = (6UL << 26) | (1UL << 14) | (7UL << 9) | (1UL << 6) | 2UL;

  // R3: default clock-divider and noise settings.
  registers[3] = 3UL;

  // Band-select clock divider: 200 gives 125 kHz from a 25 MHz PFD.
  // R4: fundamental VCO feedback, output divider, band-select divider,
  // RF output A enable and selected output power.
  registers[4] = (1UL << 23) |
                 (static_cast<uint32_t>(dividerCode) << 20) |
                 (200UL << 12) |
                 (rfEnabled ? (1UL << 5) : 0UL) |
                 (static_cast<uint32_t>(outputPower) << 3) |
                 4UL;

  // R5 reserved bits / digital lock-detect precision: standard reserved-bit pattern.
  registers[5] = 0x00580005UL;

  outputMHz = mhz;
  outputDivider = static_cast<uint8_t>(1U << dividerCode);
  writeRegisters();
  delay(30);
  return true;
}

void setRf(bool enabled) {
  rfEnabled = enabled;
  if (enabled) registers[4] |= (1UL << 5);
  else registers[4] &= ~(1UL << 5);
  writeRegister(registers[4]);
}

double jsonNumber(const String& body, const char* key, double fallback) {
  const String needle = String("\"") + key + "\"";
  int pos = body.indexOf(needle);
  if (pos < 0) return fallback;
  pos = body.indexOf(':', pos);
  if (pos < 0) return fallback;
  int end = body.indexOf(',', pos + 1);
  if (end < 0) end = body.indexOf('}', pos + 1);
  if (end < 0) return fallback;
  return body.substring(pos + 1, end).toDouble();
}

bool jsonBool(const String& body, const char* key, bool fallback) {
  const String needle = String("\"") + key + "\"";
  int pos = body.indexOf(needle);
  if (pos < 0) return fallback;
  pos = body.indexOf(':', pos);
  if (pos < 0) return fallback;
  String value = body.substring(pos + 1);
  value.trim();
  return value.startsWith("true") ? true : value.startsWith("false") ? false : fallback;
}

void sendError(int status, const String& message) {
  server.send(status, "application/json", String("{\"error\":\"") + message + "\"}");
}

String statusJson() {
  const bool locked = digitalRead(PIN_LD) == HIGH;
  const bool muxHigh = digitalRead(PIN_MUX) == HIGH;
  String json = "{";
  json += "\"frequency_mhz\":" + String(outputMHz, 6) + ",";
  json += "\"power_index\":" + String(outputPower) + ",";
  json += "\"power_dbm\":" + String(-4 + 3 * outputPower) + ",";
  json += "\"rf_enabled\":" + String(rfEnabled ? "true" : "false") + ",";
  json += "\"pll_locked\":" + String(locked ? "true" : "false") + ",";
  json += "\"mux_high\":" + String(muxHigh ? "true" : "false") + ",";
  json += "\"sweep_running\":" + String(sweepRunning ? "true" : "false") + ",";
  json += "\"divider\":" + String(outputDivider);
  json += "}";
  return json;
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ADF4351 Generator</title>
<style>
:root{color-scheme:dark}body{font:16px system-ui;background:#10151d;color:#e8edf5;margin:0;padding:18px}
main{max-width:720px;margin:auto}.card{background:#1b2430;border:1px solid #344153;border-radius:14px;padding:16px;margin:12px 0}
h1{font-size:22px}.freq{font-size:32px;font-weight:700;margin:8px 0}
input,select,button{font:inherit;border-radius:9px;padding:11px;border:1px solid #53647a;background:#111923;color:#fff}
input{width:100%;box-sizing:border-box;margin:6px 0 12px}button{cursor:pointer;background:#245da8;border:0;margin:4px}
button.secondary{background:#465366}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.row>*{flex:1}
small,.muted{color:#a9b6c8}.ok{color:#66df9b}.bad{color:#ff8b8b}
</style></head><body><main>
<h1>ADF4351 Signal Generator</h1><p class="muted">ESP32 DevKit V1 · local Wi-Fi control</p>
<div class="card"><small>OUTPUT FREQUENCY</small><div class="freq" id="freq">---</div>
<div class="row"><input id="mhz" type="number" min="35" max="4400" step="0.000001" value="1000"><button onclick="setFreq()">Set MHz</button></div>
<div class="row"><button class="secondary" onclick="nudge(-1)">− 1 MHz</button><button class="secondary" onclick="nudge(1)">+ 1 MHz</button></div></div>
<div class="card"><b>RF output</b><div class="row"><button onclick="rf(true)">RF ON</button><button class="secondary" onclick="rf(false)">RF OFF</button></div>
<label>Output power</label><select id="power" onchange="setPower()"><option value="0">−4 dBm</option><option value="1">−1 dBm</option><option value="2">+2 dBm</option><option value="3">+5 dBm</option></select>
<p>PLL: <b id="pll">---</b> · RF: <b id="rfstate">---</b></p>
<p class="muted">MUXOUT: <span id="mux">---</span> · Output divider: <span id="divider">---</span></p></div>
<div class="card"><b>Frequency sweep</b><div class="row">
<div>Start MHz<input id="start" type="number" value="100" step="0.001"></div>
<div>Stop MHz<input id="stop" type="number" value="200" step="0.001"></div></div>
<div class="row"><div>Step MHz<input id="step" type="number" value="1" step="0.001"></div>
<div>Interval ms<input id="interval" type="number" value="100" min="20" max="60000"></div></div>
<button onclick="sweep(true)">Start sweep</button><button class="secondary" onclick="sweep(false)">Stop sweep</button></div>
<p class="muted">Connect to Wi-Fi <b>ADF4351-ESP32</b>, password <b>adf4351rf</b>, then open <b>http://192.168.4.1</b>.</p>
</main><script>
async function api(path,data){let r=await fetch(path,{method:data?'POST':'GET',headers:{'Content-Type':'application/json'},body:data?JSON.stringify(data):undefined});let j=await r.json();if(!r.ok)alert(j.error||'Request failed');return j}
async function refresh(){try{let s=await api('/api/status');freq.textContent=Number(s.frequency_mhz).toFixed(6)+' MHz';mhz.value=s.frequency_mhz;power.value=s.power_index;pll.textContent=s.pll_locked?'LOCKED':'UNLOCKED';pll.className=s.pll_locked?'ok':'bad';rfstate.textContent=s.rf_enabled?'ON':'OFF';mux.textContent=s.mux_high?'HIGH':'LOW';divider.textContent='÷'+s.divider}catch(e){}}
async function setFreq(){await api('/api/set',{mhz:Number(mhz.value)});refresh()}
async function nudge(d){mhz.value=Number(mhz.value)+d;setFreq()}
async function rf(enabled){await api('/api/rf',{enabled});refresh()}
async function setPower(){await api('/api/power',{index:Number(power.value)});refresh()}
async function sweep(enabled){await api('/api/sweep',{enabled,start:Number(start.value),stop:Number(stop.value),step:Number(step.value),interval:Number(interval.value)});refresh()}
setInterval(refresh,1000);refresh();
</script></body></html>
)HTML";

void configureRoutes() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status", HTTP_GET, []() { server.send(200, "application/json", statusJson()); });

  server.on("/api/set", HTTP_POST, []() {
    const double requested = jsonNumber(server.arg("plain"), "mhz", -1);
    if (!setFrequency(requested)) return sendError(400, "Frequency must be 35..4400 MHz");
    preferences.putDouble("freq", outputMHz);
    server.send(200, "application/json", statusJson());
  });

  server.on("/api/rf", HTTP_POST, []() {
    setRf(jsonBool(server.arg("plain"), "enabled", false));
    preferences.putBool("rf", rfEnabled);
    server.send(200, "application/json", statusJson());
  });

  server.on("/api/power", HTTP_POST, []() {
    const int index = static_cast<int>(jsonNumber(server.arg("plain"), "index", -1));
    if (index < 0 || index > 3) return sendError(400, "Power index must be 0..3");
    outputPower = static_cast<uint8_t>(index);
    setFrequency(outputMHz);
    preferences.putUInt("power", outputPower);
    server.send(200, "application/json", statusJson());
  });

  server.on("/api/sweep", HTTP_POST, []() {
    const String body = server.arg("plain");
    if (!jsonBool(body, "enabled", false)) {
      sweepRunning = false;
      return server.send(200, "application/json", statusJson());
    }
    const double start = jsonNumber(body, "start", 100);
    const double stop = jsonNumber(body, "stop", 200);
    const double step = jsonNumber(body, "step", 1);
    const double interval = jsonNumber(body, "interval", 100);
    if (start < MIN_OUTPUT_MHZ || stop > MAX_OUTPUT_MHZ || start >= stop ||
        step <= 0 || step > (stop - start) || interval < 20 || interval > 60000)
      return sendError(400, "Invalid sweep parameters");
    sweepStartMHz = start;
    sweepStopMHz = stop;
    sweepStepMHz = step;
    sweepIntervalMs = static_cast<uint32_t>(interval);
    sweepRunning = setFrequency(sweepStartMHz);
    lastSweepMs = millis();
    server.send(200, "application/json", statusJson());
  });
}

void setup() {
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_DAT, OUTPUT);
  pinMode(PIN_LE, OUTPUT);
  pinMode(PIN_CE, OUTPUT);
  pinMode(PIN_LD, INPUT);
  pinMode(PIN_MUX, INPUT);
  digitalWrite(PIN_CLK, LOW);
  digitalWrite(PIN_DAT, LOW);
  digitalWrite(PIN_LE, LOW);
  digitalWrite(PIN_CE, HIGH);

  preferences.begin("adf4351", false);
  outputMHz = preferences.getDouble("freq", 1000.0);
  outputPower = static_cast<uint8_t>(preferences.getUInt("power", 3));
  if (outputPower > 3) outputPower = 3;
  rfEnabled = preferences.getBool("rf", true);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  configureRoutes();
  server.begin();

  if (setFrequency(outputMHz)) setRf(rfEnabled);
}

void loop() {
  server.handleClient();
  if (sweepRunning && millis() - lastSweepMs >= sweepIntervalMs) {
    lastSweepMs = millis();
    double next = outputMHz + sweepStepMHz;
    if (next > sweepStopMHz + 1e-9) next = sweepStartMHz;
    if (!setFrequency(next)) sweepRunning = false;
  }
}
