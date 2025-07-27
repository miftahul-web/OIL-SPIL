#include <WiFi.h>
#include <WebServer.h>

// ==================== WIFI ====================
const char* ssid     = "iPhone";
const char* password = "32119011";

// ==================== PIN TCS3200 ====================
#define S0 26
#define S1 25
#define S2 33
#define S3 32
#define SENSOR_OUT 27

// ==================== PIN RELAY ====================
#define RELAY_MERAH 22
#define RELAY_ACTIVE_LOW 1   // ubah ke 0 bila relay aktif HIGH

// ==================== PARAMETER DETEKSI HITAM ====================
int BLACK_TH = 40;   // jika R,G,B semuanya < BLACK_TH → dianggap HITAM

// ==================== PARAMETER KALIBRASI FREQ -> RGB ====================
// Sesuaikan setelah Anda logging nilai frekuensi sensor (putih / hitam).
const long FREQ_MIN = 30;   // frekuensi (lebih kecil = warna lebih kuat)
const long FREQ_MAX = 600;  // frekuensi (lebih besar = warna lebih lemah)

// ==================== VARIABEL ====================
long rFreq = 0, gFreq = 0, bFreq = 0;
int r = 0, g = 0, b = 0;

bool relayState = false;

enum Mode { AUTO_MODE, MANUAL_MODE };
Mode modeState = AUTO_MODE;

// ==================== WEBSERVER ====================
WebServer server(80);

// ==================== HTML ====================
const char PAGE_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Monitoring Warna</title>
<style>
  body{background:#0f172a;color:#e2e8f0;font-family:Arial;text-align:center;padding:20px}
  .colorbox{width:150px;height:150px;margin:20px auto;border-radius:12px;border:2px solid #fff}
  .btn{padding:10px 16px;border:none;border-radius:8px;margin:4px;cursor:pointer}
  .row{margin:12px 0}
  input{padding:8px;border-radius:6px;border:1px solid #334155;background:#1e293b;color:#e2e8f0}
</style>
</head>
<body>
  <h1>OIL SPILL DETECTION</h1>

  <div id="colorBox" class="colorbox"></div>
  <p>RGB: <span id="rgbTxt">0,0,0</span></p>
  <p>Relay: <span id="relayState">OFF</span></p>
  <p>Mode: <span id="modeState">AUTO</span></p>
  <p>BLACK_TH: <span id="blackTh">0</span></p>

  <div class="row">
    <button class="btn" onclick="setMode('auto')">Mode AUTO</button>
    <button class="btn" onclick="setMode('manual')">Mode MANUAL</button>
  </div>

  <div class="row">
    <button class="btn" onclick="relay('on')">Manual ON</button>
    <button class="btn" onclick="relay('off')">Manual OFF</button>
  </div>

  <div class="row">
    <input type="number" id="thInput" placeholder="BLACK_TH baru">
    <button class="btn" onclick="setTh()">Update Threshold</button>
  </div>

  <script>
    async function update(){
      try{
        const r = await fetch('/data');
        const d = await r.json();
        document.getElementById('rgbTxt').textContent = d.r+','+d.g+','+d.b;
        document.getElementById('colorBox').style.background = `rgb(${d.r},${d.g},${d.b})`;
        document.getElementById('relayState').textContent = d.relay ? "ON":"OFF";
        document.getElementById('modeState').textContent = d.mode;
        document.getElementById('blackTh').textContent = d.black_th;
      }catch(e){
        console.log(e);
      }
    }
    function setMode(m){ fetch('/mode?m='+m); }
    function relay(state){ fetch('/relay?state='+state); }
    function setTh(){
      const v = document.getElementById('thInput').value;
      if(v !== "") fetch('/set?black='+v);
    }
    setInterval(update, 200);
    update();
  </script>
</body>
</html>
)rawliteral";

// ==================== SENSOR ====================
long readPulseAvg(uint8_t s2, uint8_t s3, uint8_t samples = 3) {
  digitalWrite(S2, s2);
  digitalWrite(S3, s3);
  delayMicroseconds(100);
  long sum = 0; int valid = 0;
  for (uint8_t i=0;i<samples;i++){
    long v = pulseIn(SENSOR_OUT, LOW, 25000); // 25 ms timeout -> lebih responsif
    if(v>0){ sum += v; valid++; }
  }
  return valid ? sum/valid : 0;
}

int freqToColor(long freq){
  int val = map(freq, FREQ_MIN, FREQ_MAX, 255, 0);
  return constrain(val, 0, 255);
}

void readRGB(){
  rFreq = readPulseAvg(LOW,  LOW);
  gFreq = readPulseAvg(HIGH, HIGH);
  bFreq = readPulseAvg(LOW,  HIGH);

  r = freqToColor(rFreq);
  g = freqToColor(gFreq);
  b = freqToColor(bFreq);

  // Debugging ke Serial untuk kalibrasi
  static unsigned long lastLog = 0;
  if (millis() - lastLog > 1000) {
    lastLog = millis();
    Serial.printf("Freq R:%ld G:%ld B:%ld | RGB: %d,%d,%d | TH:%d | Mode:%s | Relay:%s\n",
                  rFreq, gFreq, bFreq, r, g, b, BLACK_TH,
                  (modeState==AUTO_MODE?"AUTO":"MANUAL"),
                  (relayState?"ON":"OFF"));
  }
}

// ==================== RELAY ====================
inline void doRelayWrite(bool on){
  digitalWrite(RELAY_MERAH, (RELAY_ACTIVE_LOW ? (!on) : on) ? HIGH : LOW);
}

void relayOn(){
  relayState = true;
  doRelayWrite(true);
}
void relayOff(){
  relayState = false;
  doRelayWrite(false);
}

// ==================== HTTP HANDLERS ====================
void handleRoot(){ server.send_P(200, "text/html", PAGE_INDEX); }

void handleData(){
  String json = "{";
  json += "\"r\":" + String(r) + ",";
  json += "\"g\":" + String(g) + ",";
  json += "\"b\":" + String(b) + ",";
  json += "\"relay\":" + String(relayState ? "true":"false") + ",";
  json += "\"mode\":\"" + String(modeState==AUTO_MODE?"AUTO":"MANUAL") + "\",";
  json += "\"black_th\":" + String(BLACK_TH);
  json += "}";
  server.send(200, "application/json", json);
}

void handleMode(){
  if(server.hasArg("m")){
    String m = server.arg("m");
    if(m.equalsIgnoreCase("auto")){
      modeState = AUTO_MODE;
    }else if(m.equalsIgnoreCase("manual")){
      modeState = MANUAL_MODE;
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleRelay(){
  if(server.hasArg("state")){
    String st = server.arg("state");
    modeState = MANUAL_MODE; // paksa manual bila user kirim perintah relay
    if(st.equalsIgnoreCase("on")) {
      relayOn();
    } else if(st.equalsIgnoreCase("off")) {
      relayOff();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSet(){
  if(server.hasArg("black")){
    int th = server.arg("black").toInt();
    if(th >= 0 && th <= 255){
      BLACK_TH = th;
    }
  }
  server.send(200, "text/plain", "OK");
}

// ==================== SETUP / LOOP ====================
void setup(){
  Serial.begin(115200);

  pinMode(S0, OUTPUT); pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT); pinMode(S3, OUTPUT);
  pinMode(SENSOR_OUT, INPUT);
  digitalWrite(S0, HIGH);  // 20% scaling
  digitalWrite(S1, LOW);

  pinMode(RELAY_MERAH, OUTPUT);
  relayOff();

  WiFi.begin(ssid, password);
  Serial.print("Menghubungkan ke WiFi");
  while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/mode", handleMode);
  server.on("/relay", handleRelay);
  server.on("/set", handleSet);
  server.begin();
}

unsigned long lastRead = 0;
void loop(){
  server.handleClient();

  if(millis() - lastRead >= 50){ // baca tiap 50 ms
    lastRead = millis();
    readRGB();

    if(modeState == AUTO_MODE){
      // ==== LOGIKA DETEKSI HITAM ====
      if(r < BLACK_TH && g < BLACK_TH && b < BLACK_TH){
        relayOn();
      }else{
        relayOff();
      }
    }
  }
}
