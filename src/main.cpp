#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "driver/twai.h"

#ifndef PIN_CAN_TX
#define PIN_CAN_TX 15
#endif
#ifndef PIN_CAN_RX
#define PIN_CAN_RX 16
#endif

static const char* AP_SSID = "Tesla-Ghost";
static const char* AP_PASS = "ghost2018";

WebServer server(80);

struct GhostState {
  bool can_ok = false;
  uint32_t rx_frames = 0;
  uint32_t id334_frames = 0;
  uint32_t checksum_fail = 0;
  uint32_t last_334_ms = 0;
  uint8_t last_334[8] = {0};
  uint8_t last_334_dlc = 0;
  int pedal_map = -1;       // 0 Chill, 1 Standard, 2 Performance, 3 unknown/reserved
  uint8_t counter = 0;
  bool checksum_ok = false;
  float vehicle_speed_kph = 0.0f;
} state;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tesla Ghost Monitor</title>
<style>
body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Arial;background:#090b10;color:#eee;margin:0;padding:18px}
h1{font-size:24px;margin:0 0 6px}.sub{color:#9da6b5;margin-bottom:16px}
.card{background:#141823;border:1px solid #252b38;border-radius:14px;padding:14px;margin:10px 0}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.k{color:#929cab;font-size:12px}.v{font-size:20px;font-weight:700;margin-top:3px}
.good{color:#58d68d}.bad{color:#ff6961}.warn{color:#ffd166}
code{word-break:break-all;color:#9fe3ff}
button,input[type=file]{font-size:16px;margin-top:8px}
button{padding:10px 14px;border:0;border-radius:10px;background:#3478f6;color:white}
small{color:#929cab}
</style></head>
<body>
<h1>Tesla Ghost Monitor</h1>
<div class="sub">Waveshare ESP32-S3 • 500 kbps • listen-only</div>

<div class="grid">
  <div class="card"><div class="k">CAN</div><div id="can" class="v">...</div></div>
  <div class="card"><div class="k">Vehicle speed</div><div id="spd" class="v">...</div></div>
</div>

<div class="card">
  <div class="k">0x334 pedal map</div><div id="map" class="v">...</div>
  <p><span class="k">Counter</span> <span id="ctr">-</span> &nbsp;
     <span class="k">Checksum</span> <span id="chk">-</span></p>
  <div class="k">Last frame</div><code id="raw">-</code>
</div>

<div class="grid">
  <div class="card"><div class="k">RX frames</div><div id="rx" class="v">0</div></div>
  <div class="card"><div class="k">0x334 frames</div><div id="n334" class="v">0</div></div>
</div>

<div class="card">
  <div class="k">Wi-Fi firmware update</div>
  <form method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" name="firmware" accept=".bin" required><br>
    <button type="submit">Upload firmware.bin</button>
  </form>
  <small>Device remains CAN listen-only during this build. After a successful upload it reboots automatically.</small>
</div>

<script>
function mapName(v){return ["CHILL","STANDARD","PERFORMANCE","RESERVED"][v]||"UNKNOWN";}
async function tick(){
  try{
    const r=await fetch('/api/status',{cache:'no-store'}); const s=await r.json();
    document.getElementById('can').textContent=s.can_ok?'CONNECTED':'WAITING';
    document.getElementById('can').className='v '+(s.can_ok?'good':'warn');
    document.getElementById('spd').textContent=s.speed_kph.toFixed(1)+' km/h';
    document.getElementById('map').textContent=mapName(s.pedal_map);
    document.getElementById('ctr').textContent=s.counter;
    document.getElementById('chk').textContent=s.checksum_ok?'VALID':'INVALID / NONE';
    document.getElementById('chk').className=s.checksum_ok?'good':'bad';
    document.getElementById('raw').textContent=s.frame334 || '-';
    document.getElementById('rx').textContent=s.rx_frames;
    document.getElementById('n334').textContent=s.id334_frames;
  }catch(e){}
}
setInterval(tick,500); tick();
</script>
</body></html>
)HTML";

static uint8_t checksum334(const twai_message_t& m) {
  uint16_t sum = (m.identifier & 0xFF) + ((m.identifier >> 8) & 0xFF);
  for (int i = 0; i < 7; ++i) sum += m.data[i];
  return (uint8_t)(sum & 0xFF);
}

static int decodePedalMap(uint8_t b0) {
  return (b0 >> 5) & 0x03;
}

static float decodeVehicleSpeedKph(const twai_message_t& m) {
  if (m.data_length_code < 3) return state.vehicle_speed_kph;
  uint16_t raw = (((uint16_t)m.data[2]) << 4) | (m.data[1] >> 4);
  float kph = raw * 0.08f - 40.0f;
  if (kph < 0.0f) kph = 0.0f;
  return kph;
}

static String frame334Hex() {
  if (!state.last_334_dlc) return "";
  char buf[3 * 8 + 1];
  size_t p = 0;
  for (uint8_t i = 0; i < state.last_334_dlc; ++i) {
    p += snprintf(buf + p, sizeof(buf) - p, "%02X%s",
                  state.last_334[i], i + 1 < state.last_334_dlc ? " " : "");
  }
  return String(buf);
}

static void handleStatus() {
  String json = "{";
  json += "\"can_ok\":" + String(state.can_ok ? "true" : "false") + ",";
  json += "\"rx_frames\":" + String(state.rx_frames) + ",";
  json += "\"id334_frames\":" + String(state.id334_frames) + ",";
  json += "\"checksum_fail\":" + String(state.checksum_fail) + ",";
  json += "\"pedal_map\":" + String(state.pedal_map) + ",";
  json += "\"counter\":" + String(state.counter) + ",";
  json += "\"checksum_ok\":" + String(state.checksum_ok ? "true" : "false") + ",";
  json += "\"speed_kph\":" + String(state.vehicle_speed_kph, 2) + ",";
  json += "\"frame334\":\"" + frame334Hex() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

static void setupWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, handleStatus);

  server.on(
    "/update", HTTP_POST,
    []() {
      bool ok = !Update.hasError();
      server.send(ok ? 200 : 500, "text/plain",
                  ok ? "Update complete. Rebooting..." : "Update failed.");
      delay(500);
      if (ok) ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START) {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) {
          Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
      }
    }
  );

  server.begin();
}

static bool setupCan() {
  twai_general_config_t g =
      TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)PIN_CAN_TX,
                                  (gpio_num_t)PIN_CAN_RX,
                                  TWAI_MODE_LISTEN_ONLY);

  g.rx_queue_len = 64;
  g.tx_queue_len = 0;

  twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
  twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
  if (twai_start() != ESP_OK) return false;
  return true;
}

static void processCan() {
  twai_message_t m;
  while (twai_receive(&m, 0) == ESP_OK) {
    if (m.extd || m.rtr || m.data_length_code > 8) continue;
    state.rx_frames++;
    state.can_ok = true;

    if (m.identifier == 0x257 && m.data_length_code >= 3) {
      state.vehicle_speed_kph = decodeVehicleSpeedKph(m);
    }

    if (m.identifier == 0x334 && m.data_length_code >= 8) {
      state.id334_frames++;
      state.last_334_ms = millis();
      state.last_334_dlc = 8;
      memcpy(state.last_334, m.data, 8);

      state.pedal_map = decodePedalMap(m.data[0]);
      state.counter = (m.data[6] >> 4) & 0x0F;
      uint8_t expect = checksum334(m);
      state.checksum_ok = (expect == m.data[7]);
      if (!state.checksum_ok) state.checksum_fail++;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nTesla Ghost Monitor v0.1");
  Serial.println("Waveshare ESP32-S3 RS485/CAN");
  Serial.println("CAN: 500 kbps, LISTEN-ONLY");
  Serial.println("WiFi AP: Tesla-Ghost");
  Serial.println("Dashboard: http://192.168.4.1");

  state.can_ok = setupCan();
  setupWeb();
}

void loop() {
  processCan();
  server.handleClient();

  // Mark CAN as stale if no frames have been seen for a while only if the
  // controller itself has reported no traffic; we retain the setup state.
  static uint32_t last_print = 0;
  if (millis() - last_print > 5000) {
    last_print = millis();
    Serial.printf("RX=%lu  334=%lu  speed=%.1f kph  map=%d  checksum=%s\n",
                  (unsigned long)state.rx_frames,
                  (unsigned long)state.id334_frames,
                  state.vehicle_speed_kph,
                  state.pedal_map,
                  state.checksum_ok ? "OK" : "NO/FAIL");
  }
  delay(1);
}
