#include <WiFi.h>
#include <WebServer.h>
#include "web.h"
#include "config.h"
#include "globals.h"
#include "servos.h"
#include "display.h"
#include "storage.h"

WebServer server(WEB_SERVER_PORT);

// ---------------------------------------------------------------------------
// PAGINA HTML (servida em "/") - sliders + toggle de modo + calibracao +
// brilho do backlight
// ---------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>T-800 Controle</title>
<style>
  body { background:#111; color:#0f0; font-family: monospace; text-align:center; padding:20px; }
  h1 { font-size:18px; letter-spacing:2px; }
  .slider-box { margin:20px auto; max-width:320px; text-align:left; }
  label { display:block; margin-bottom:4px; }
  input[type=range] { width:100%; }
  .toggle-box { margin:24px auto; }
  button { background:#0f0; color:#111; border:none; padding:10px 16px; font-family:monospace;
           font-weight:bold; border-radius:4px; margin:4px; }
  button.off { background:#333; color:#0f0; border:1px solid #0f0; }
  #status { margin-top:16px; font-size:12px; opacity:0.7; }
</style>
</head>
<body>
  <h1>&gt; T-800 CONTROL_</h1>

  <div class="toggle-box">
    <button id="btnSensor" onclick="setMode('sensor')">MODO SENSORES</button>
    <button id="btnRemote" onclick="setMode('remote')">MODO CELULAR</button>
  </div>

  <div class="slider-box">
    <label>YAW (esquerda / direita): <span id="yawVal">90</span></label>
    <input type="range" min="45" max="135" value="90" id="yaw" oninput="sendMove()">
  </div>
  <div class="slider-box">
    <label>PITCH (cima / baixo): <span id="pitchVal">95</span></label>
    <input type="range" min="70" max="120" value="95" id="pitch" oninput="sendMove()">
  </div>
  <div class="slider-box">
    <label>OLHO ESQUERDO: <span id="eyeLVal">90</span></label>
    <input type="range" min="60" max="120" value="90" id="eyeL" oninput="sendMove()">
  </div>
  <div class="slider-box">
    <label>OLHO DIREITO: <span id="eyeRVal">90</span></label>
    <input type="range" min="60" max="120" value="90" id="eyeR" oninput="sendMove()">
  </div>

  <hr style="border-color:#333; margin:24px 0;">
  <h1 style="font-size:14px;">&gt; DISPLAY_</h1>
  <div class="slider-box">
    <label>Brilho do backlight: <span id="blkVal">200</span></label>
    <input type="range" min="0" max="255" value="200" id="blk" oninput="sendBacklight()">
  </div>

  <hr style="border-color:#333; margin:24px 0;">
  <h1 style="font-size:14px;">&gt; CALIBRACAO_ (centro/zero)</h1>
  <div class="slider-box">
    <label>Trim YAW: <span id="trimYawVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimYaw" oninput="sendTrim()">
  </div>
  <div class="slider-box">
    <label>Trim PITCH: <span id="trimPitchVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimPitch" oninput="sendTrim()">
  </div>
  <div class="slider-box">
    <label>Trim OLHO ESQUERDO: <span id="trimEyeLVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimEyeL" oninput="sendTrim()">
  </div>
  <div class="slider-box">
    <label>Trim OLHO DIREITO: <span id="trimEyeRVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimEyeR" oninput="sendTrim()">
  </div>
  <div class="toggle-box">
    <button onclick="saveTrim()">SALVAR CALIBRACAO</button>
    <button class="off" onclick="goIdle()">CENTRALIZAR E DESENERGIZAR</button>
  </div>
  <div id="trimStatus" style="font-size:11px; opacity:0.7;"></div>

  <div id="status">modo atual: --</div>

<script>
function sendMove() {
  const yaw = document.getElementById('yaw').value;
  const pitch = document.getElementById('pitch').value;
  const eyeL = document.getElementById('eyeL').value;
  const eyeR = document.getElementById('eyeR').value;
  document.getElementById('yawVal').innerText = yaw;
  document.getElementById('pitchVal').innerText = pitch;
  document.getElementById('eyeLVal').innerText = eyeL;
  document.getElementById('eyeRVal').innerText = eyeR;
  fetch(`/move?yaw=${yaw}&pitch=${pitch}&eyeL=${eyeL}&eyeR=${eyeR}`);
}
function setMode(m) {
  fetch(`/mode?state=${m}`).then(()=>refreshStatus());
}
function sendBacklight() {
  const v = document.getElementById('blk').value;
  document.getElementById('blkVal').innerText = v;
  fetch(`/backlight?val=${v}&save=1`);
}
let trimLoaded = false;
function refreshStatus() {
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('status').innerText = 'modo atual: ' + d.mode;
    document.getElementById('btnSensor').className = d.mode==='sensor' ? '' : 'off';
    document.getElementById('btnRemote').className = d.mode==='remote' ? '' : 'off';
    // so preenche os sliders de trim/backlight UMA vez (no carregamento da
    // pagina), com o valor realmente salvo no ESP32 - evita sobrescrever a
    // calibracao com 0 caso o usuario mexa no slider sem ter recarregado a
    // pagina antes.
    if (!trimLoaded) {
      document.getElementById('trimYaw').value = d.trimYaw;
      document.getElementById('trimPitch').value = d.trimPitch;
      document.getElementById('trimEyeL').value = d.trimEyeL;
      document.getElementById('trimEyeR').value = d.trimEyeR;
      document.getElementById('trimYawVal').innerText = d.trimYaw;
      document.getElementById('trimPitchVal').innerText = d.trimPitch;
      document.getElementById('trimEyeLVal').innerText = d.trimEyeL;
      document.getElementById('trimEyeRVal').innerText = d.trimEyeR;
      document.getElementById('blk').value = d.backlight;
      document.getElementById('blkVal').innerText = d.backlight;
      trimLoaded = true;
    }
  });
}
function sendTrim() {
  const ty = document.getElementById('trimYaw').value;
  const tp = document.getElementById('trimPitch').value;
  const tl = document.getElementById('trimEyeL').value;
  const tr = document.getElementById('trimEyeR').value;
  document.getElementById('trimYawVal').innerText = ty;
  document.getElementById('trimPitchVal').innerText = tp;
  document.getElementById('trimEyeLVal').innerText = tl;
  document.getElementById('trimEyeRVal').innerText = tr;
  // aplica na hora (RAM), sem gravar na NVS ainda - so move o servo pro novo centro
  fetch(`/trim?yaw=${ty}&pitch=${tp}&eyeL=${tl}&eyeR=${tr}`);
  document.getElementById('trimStatus').innerText = 'ajustando... (nao salvo)';
}
function saveTrim() {
  fetch(`/trim?save=1`).then(()=>{
    document.getElementById('trimStatus').innerText = 'calibracao salva na memoria do ESP32.';
  });
}
function goIdle() {
  fetch('/idle');
}
refreshStatus();
setInterval(refreshStatus, 4000);
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// HANDLERS DO SERVIDOR WEB
// ---------------------------------------------------------------------------
static void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

static void handleMove() {
  if (currentMode != MODE_REMOTE) {
    server.send(409, "text/plain", "Fora do modo remoto - troque o modo antes de mover.");
    return;
  }
  if (server.hasArg("yaw"))   posYaw   = server.arg("yaw").toInt();
  if (server.hasArg("pitch")) posPitch = server.arg("pitch").toInt();
  if (server.hasArg("eyeL"))  posEyeL  = server.arg("eyeL").toInt();
  if (server.hasArg("eyeR"))  posEyeR  = server.arg("eyeR").toInt();
  applyServoPositions();
  server.send(200, "text/plain", "ok");
}

static void handleSetMode() {
  if (server.hasArg("state")) {
    String s = server.arg("state");
    if (s == "remote") {
      currentMode = MODE_REMOTE;
      showRemoteModeBadge();
    } else {
      currentMode = MODE_SENSOR;
      showStandby();
    }
    storageSaveMode();
  }
  server.send(200, "text/plain", "ok");
}

static void handleStatus() {
  String json = String("{\"mode\":\"") + (currentMode == MODE_REMOTE ? "remote" : "sensor") +
                "\",\"trimYaw\":" + String(trimPulse[CH_YAW]) +
                ",\"trimPitch\":" + String(trimPulse[CH_PITCH]) +
                ",\"trimEyeL\":" + String(trimPulse[CH_EYE_L]) +
                ",\"trimEyeR\":" + String(trimPulse[CH_EYE_R]) +
                ",\"backlight\":" + String(backlightLevel) + "}";
  server.send(200, "application/json", json);
}

// Calibracao de centro/zero. Ajusta o trim em RAM e mostra o efeito na hora
// (posiciona o eixo no seu centro); so grava na NVS quando chamado com save=1.
static void handleTrim() {
  bool changed = false;
  if (server.hasArg("yaw"))   { trimPulse[CH_YAW]   = server.arg("yaw").toInt();   changed = true; }
  if (server.hasArg("pitch")) { trimPulse[CH_PITCH] = server.arg("pitch").toInt(); changed = true; }
  if (server.hasArg("eyeL"))  { trimPulse[CH_EYE_L] = server.arg("eyeL").toInt();  changed = true; }
  if (server.hasArg("eyeR"))  { trimPulse[CH_EYE_R] = server.arg("eyeR").toInt();  changed = true; }

  if (changed) {
    posYaw = YAW_CENTER; posPitch = PITCH_CENTER;
    posEyeL = EYE_L_CENTER; posEyeR = EYE_R_CENTER;
    applyServoPositions();   // reenergiza e mostra o novo centro na hora
  }

  if (server.hasArg("save") && server.arg("save") == "1") {
    storageSaveTrim();
  }
  server.send(200, "text/plain", "ok");
}

// Brilho do backlight (PWM, GPIO 18). save=1 persiste em NVS.
static void handleBacklight() {
  if (server.hasArg("val")) {
    int v = constrain(server.arg("val").toInt(), 0, 255);
    setBacklight((uint8_t)v);
  }
  if (server.hasArg("save") && server.arg("save") == "1") {
    storageSaveBacklight();
  }
  server.send(200, "text/plain", "ok");
}

// Centraliza (com o trim aplicado) e corta o PWM - usado apos calibrar manualmente.
static void handleIdle() {
  servosToCenter();
  delay(300);
  releaseServos();
  server.send(200, "text/plain", "ok");
}

void webInit() {
  WiFi.softAP(AP_SSID, AP_PASS);

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/mode", handleSetMode);
  server.on("/status", handleStatus);
  server.on("/trim", handleTrim);
  server.on("/backlight", handleBacklight);
  server.on("/idle", handleIdle);
  server.begin();
}

void webHandleClient() {
  server.handleClient();
}
