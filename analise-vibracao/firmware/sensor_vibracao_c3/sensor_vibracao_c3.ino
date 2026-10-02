/*
  Monitoramento de vibracao fixo - ESP32-C3 Super Mini + ADXL345 + OLED
  PLATAFORMA v2 (recomeco do zero - colecoes v2_* no Firestore)

  O QUE MUDOU EM RELACAO AO FIRMWARE ANTERIOR (legado/mancal_vibracao_c3_supermini.ino):
    - O sensor NAO conhece mais nome de moinho/ponto. Ele se identifica so
      pelo MAC. O nome e atribuido (e editado) no painel web, gravado em
      v2_sensores/<MAC>, e aplicado na exibicao. Renomear e instantaneo e
      vale tambem para o historico ja gravado. Portal web, NVS de nome e
      botao BOOT foram removidos.
    - Data/hora da medicao e carimbada pelo SERVIDOR do Firestore
      (REQUEST_TIME), nao pelo relogio do ESP32. Elimina os timestamps de
      1970 quando NTP/HTTPS falhava. Nao ha mais sincronizacao de hora.
    - MAC lido direto do eFuse (esp_read_mac), que funciona antes do Wi-Fi
      iniciar. WiFi.macAddress() podia devolver 00:00:00:00:00:00.
    - Uma medicao = UM documento com H/V/A juntos. O ID do documento e
      gerado uma vez por ciclo e reaproveitado nas novas tentativas, entao
      reenvio nao duplica nem deixa eixo faltando.
    - Auto-cadastro: ao ligar (e a cada 1 h) o sensor atualiza
      v2_sensores/<MAC> com "visto", IP, RSSI e versao. Ele aparece no
      painel como "Novo sensor" assim que conecta, antes da 1a medicao.
    - 1a medicao apos ligar sai com PRIMEIRO_ENVIO_MS de maquina ligada
      (10 min), para conferir a instalacao; depois volta ao ciclo de 12 h.

  Processamento do sinal (filtros, integracao, RMS, criterio de maquina
  ligada, media entre envios) e IDENTICO ao firmware anterior.

  CONFIGURACAO NA ARDUINO IDE:
    - Board: "ESP32C3 Dev Module"
    - USB CDC On Boot: Enabled
    - Flash Size: 4MB
    - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
    - Bibliotecas: Adafruit ADXL345, Adafruit Unified Sensor, U8g2

  CREDENCIAIS: copie "segredos_exemplo.h" para "segredos.h" (mesma pasta)
  e preencha SSID/senha. segredos.h fica fora do git.

  ORIENTACAO DO ADXL345 (mesma do anterior - confira com o teste da gravidade):
    Z do chip -> HORIZONTAL radial | Y -> VERTICAL | X -> AXIAL
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_mac.h>
#include "segredos.h" // define WIFI_SSID e WIFI_SENHA

#define USAR_DISPLAY true
#if USAR_DISPLAY
  #include <U8g2lib.h>
#endif

const char* FW_VERSAO = "v2.0";

// ---------- Firestore (REST) ----------
// documents:commit permite carimbar o campo com a hora do servidor
// (setToServerValue: REQUEST_TIME) e gravar varios documentos de forma atomica.
const char* FIRESTORE_DOC_PREFIX =
  "projects/analise-de-vibracao-fe8fc/databases/(default)/documents/";
const char* FIRESTORE_COMMIT_URL =
  "https://firestore.googleapis.com/v1/projects/analise-de-vibracao-fe8fc/databases/(default)/documents:commit";
const char* COL_SENSORES = "v2_sensores";
const char* COL_MEDICOES = "v2_medicoes";

String macId     = ""; // 12 hex maiusculos, sem ":" - identificador unico do sensor
String macSufixo = ""; // ultimos 4 - so para exibicao

// ---------- Acelerometro / display ----------
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
#if USAR_DISPLAY
  U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
#endif

// ---------- Amostragem ----------
const int   SAMPLE_RATE_HZ = 200;
const int   WINDOW_SAMPLES = 200;     // janela de 1 segundo
const float DT             = 1.0 / SAMPLE_RATE_HZ;
const float HPF_CUTOFF_HZ  = 10.0;    // corte conforme faixa ISO 10816

// ---------- Limiares ISO 10816-3 ----------
const float LIMIAR_ALERTA_MMS = 4.5;
const float LIMIAR_ALARME_MMS = 7.1;

// ---------- Filtro passa-alta de 1 polo (usado 2x por eixo: accel e vel) ----------
struct HighPassFilter {
  float alpha = 0;
  float prevInput = 0;
  float prevOutput = 0;
  bool initialized = false;

  void begin(float cutoffHz, float dt) {
    float rc = 1.0 / (2.0 * PI * cutoffHz);
    alpha = rc / (rc + dt);
  }

  float apply(float input) {
    if (!initialized) {
      prevInput = input;
      prevOutput = 0;
      initialized = true;
      return 0;
    }
    float output = alpha * (prevOutput + input - prevInput);
    prevInput = input;
    prevOutput = output;
    return output;
  }
};

HighPassFilter hpfAccelH, hpfAccelV, hpfAccelA;
HighPassFilter hpfVelH,   hpfVelV,   hpfVelA;

float velH = 0, velV = 0, velA = 0; // acumuladores de integracao (reiniciados a cada janela)

float bufVelH[WINDOW_SAMPLES];
float bufVelV[WINDOW_SAMPLES];
float bufVelA[WINDOW_SAMPLES];
int   bufIndex = 0;

unsigned long lastSampleTime = 0;

// ---------- Ciclo de envio ----------
const unsigned long SEND_INTERVAL_MS   = 12UL * 60UL * 60UL * 1000UL; // 12 h DE MAQUINA LIGADA
const unsigned long PRIMEIRO_ENVIO_MS  = 10UL * 60UL * 1000UL;        // 1o envio apos ligar: 10 min de maquina ligada
const unsigned long RETRY_INTERVAL_MS  = 5UL * 60UL * 1000UL;         // falhou -> tenta de novo em 5 min
const float LIMIAR_MAQUINA_LIGADA_MMS  = 1.5; // os 3 eixos acima disso = maquina ligada

bool primeiroEnvioFeito = false;

double somaH = 0, somaV = 0, somaA = 0;
unsigned long contagemAmostras = 0;
unsigned long acumuladorLigadoMs  = 0;
unsigned long ultimaJanelaMillis  = 0;
unsigned long proximaTentativaMs  = 0;

// Media "congelada" do ciclo que esta sendo enviado. Fica fixa ate o
// envio dar certo, junto com o ID do documento - por isso uma nova
// tentativa regrava o MESMO documento (sem duplicar).
bool   envioPendente = false;
String pendenteDocId = "";
float  pendenteH = 0, pendenteV = 0, pendenteA = 0;
unsigned long pendenteJanelas = 0, pendenteLigadoMin = 0;

// ---------- Heartbeat / Wi-Fi ----------
const unsigned long HEARTBEAT_INTERVAL_MS  = 60UL * 60UL * 1000UL; // 1 h
const unsigned long WIFI_CHECK_INTERVAL_MS = 30UL * 1000UL;
unsigned long ultimoHeartbeat    = 0;
bool          heartbeatPendente  = true; // manda assim que tiver Wi-Fi
unsigned long ultimaChecagemWifi = 0;

// ---------- Identificacao ----------
void lerMac() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA); // le do eFuse - nao depende do Wi-Fi ter iniciado
  char buf[13];
  snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  macId = String(buf);
  macSufixo = macId.substring(8);
}

bool macValido() {
  return macId.length() == 12 && macId != "000000000000";
}

// ---------- Firestore REST ----------
int firestoreCommit(const String& corpo) {
  WiFiClientSecure client;
  client.setInsecure(); // sem validacao de certificado (mesma decisao do firmware anterior)
  HTTPClient http;
  http.begin(client, FIRESTORE_COMMIT_URL);
  http.addHeader("Content-Type", "application/json");
  int codigo = http.POST(corpo);
  if (codigo != 200) {
    Serial.printf("Firestore commit -> %d %s\n", codigo, http.getString().c_str());
  }
  http.end();
  return codigo;
}

// Write que atualiza SO os campos de status do sensor (updateMask) e
// carimba "visto" com a hora do servidor. Nao toca em moinho/ponto, que
// sao gravados pelo painel no mesmo documento.
String writeStatusSensor() {
  String nome = String(FIRESTORE_DOC_PREFIX) + COL_SENSORES + "/" + macId;
  String w = "{\"update\":{\"name\":\"" + nome + "\",\"fields\":{";
  w += "\"mac\":{\"stringValue\":\"" + macId + "\"},";
  w += "\"fw\":{\"stringValue\":\"" + String(FW_VERSAO) + "\"},";
  w += "\"ip\":{\"stringValue\":\"" + WiFi.localIP().toString() + "\"},";
  w += "\"rssi\":{\"integerValue\":\"" + String(WiFi.RSSI()) + "\"}";
  w += "}},\"updateMask\":{\"fieldPaths\":[\"mac\",\"fw\",\"ip\",\"rssi\"]},";
  w += "\"updateTransforms\":[{\"fieldPath\":\"visto\",\"setToServerValue\":\"REQUEST_TIME\"}]}";
  return w;
}

bool enviarHeartbeat() {
  if (WiFi.status() != WL_CONNECTED || !macValido()) return false;
  int codigo = firestoreCommit("{\"writes\":[" + writeStatusSensor() + "]}");
  Serial.printf("Heartbeat -> %d\n", codigo);
  return codigo == 200;
}

// Grava a medicao (H/V/A num documento so, "ts" = hora do servidor) e,
// na mesma transacao, atualiza o status do sensor.
bool enviarMedicaoPendente() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Envio adiado: sem Wi-Fi.");
    return false;
  }
  if (!macValido()) {
    Serial.println("Envio bloqueado: MAC invalido (000000000000).");
    return false;
  }
  String nome = String(FIRESTORE_DOC_PREFIX) + COL_MEDICOES + "/" + pendenteDocId;
  String w = "{\"update\":{\"name\":\"" + nome + "\",\"fields\":{";
  w += "\"mac\":{\"stringValue\":\"" + macId + "\"},";
  w += "\"h\":{\"doubleValue\":" + String(pendenteH, 3) + "},";
  w += "\"v\":{\"doubleValue\":" + String(pendenteV, 3) + "},";
  w += "\"a\":{\"doubleValue\":" + String(pendenteA, 3) + "},";
  w += "\"janelas\":{\"integerValue\":\"" + String(pendenteJanelas) + "\"},";
  w += "\"ligadoMin\":{\"integerValue\":\"" + String(pendenteLigadoMin) + "\"},";
  w += "\"fw\":{\"stringValue\":\"" + String(FW_VERSAO) + "\"}";
  w += "}},\"updateTransforms\":[{\"fieldPath\":\"ts\",\"setToServerValue\":\"REQUEST_TIME\"}]}";

  int codigo = firestoreCommit("{\"writes\":[" + w + "," + writeStatusSensor() + "]}");
  Serial.printf("Medicao %s -> %d\n", pendenteDocId.c_str(), codigo);
  return codigo == 200;
}

// ---------- Display ----------
String statusISO(float mms) {
  if (mms >= LIMIAR_ALARME_MMS) return "ALARME";
  if (mms >= LIMIAR_ALERTA_MMS) return "ALERTA";
  return "OK";
}

#if USAR_DISPLAY
void telaMensagem(const char* l1, const char* l2, const char* l3) {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 14, l1);
  u8g2.drawStr(0, 34, l2);
  u8g2.drawStr(0, 54, l3);
  u8g2.sendBuffer();
}

void atualizarDisplay(float rmsH, float rmsV, float rmsA) {
  char l0[24], l1[24], l2[24], l3[24];
  snprintf(l0, sizeof(l0), "ID %s  %s", macSufixo.c_str(),
           WiFi.status() == WL_CONNECTED ? "WiFi" : "sem WiFi");
  snprintf(l1, sizeof(l1), "H:%.2f %s", rmsH, statusISO(rmsH).c_str());
  snprintf(l2, sizeof(l2), "V:%.2f %s", rmsV, statusISO(rmsV).c_str());
  snprintf(l3, sizeof(l3), "A:%.2f %s", rmsA, statusISO(rmsA).c_str());
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, l0);
  u8g2.drawStr(0, 26, l1);
  u8g2.drawStr(0, 42, l2);
  u8g2.drawStr(0, 58, l3);
  u8g2.sendBuffer();
}
#endif

float calcularRMS(float* buffer, int n) {
  float somaQuadrados = 0;
  for (int i = 0; i < n; i++) somaQuadrados += buffer[i] * buffer[i];
  return sqrt(somaQuadrados / n);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(4, 5); // SDA=GPIO4, SCL=GPIO5 (evita GPIO8/9, pinos de strapping no C3)
#if USAR_DISPLAY
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  telaMensagem("Iniciando...", "", "");
#endif

  lerMac();
  Serial.printf("MAC: %s | firmware %s\n", macId.c_str(), FW_VERSAO);

#if USAR_DISPLAY
  {
    String l1 = "ID: " + macSufixo + "  " + FW_VERSAO;
    telaMensagem(l1.c_str(), macId.c_str(), macValido() ? "Nomear no painel" : "ERRO: MAC invalido");
    delay(2500); // tempo p/ o tecnico anotar o MAC na instalacao
  }
#endif

  if (!accel.begin()) {
    Serial.println("ADXL345 nao encontrado. Verifique a fiacao I2C.");
#if USAR_DISPLAY
    telaMensagem("ERRO:", "ADXL345 nao", "encontrado!");
#endif
    while (1) delay(10);
  }
  accel.setRange(ADXL345_RANGE_4_G);
  accel.setDataRate(ADXL345_DATARATE_200_HZ);

  hpfAccelH.begin(HPF_CUTOFF_HZ, DT);
  hpfAccelV.begin(HPF_CUTOFF_HZ, DT);
  hpfAccelA.begin(HPF_CUTOFF_HZ, DT);
  hpfVelH.begin(HPF_CUTOFF_HZ, DT);
  hpfVelV.begin(HPF_CUTOFF_HZ, DT);
  hpfVelA.begin(HPF_CUTOFF_HZ, DT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  Serial.printf("Conectando ao Wi-Fi \"%s\"", WIFI_SSID);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" conectado.");
    // DNS manual em IPv4 (mesmo contorno do firmware anterior - DNS via
    // IPv6 entregue por algumas redes nao resolve no stack do ESP32)
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8,8,8,8), IPAddress(1,1,1,1));
    delay(200);
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("RSSI: "); Serial.println(WiFi.RSSI());
  } else {
    Serial.println(" nao conectou em 20s. Segue medindo; envia quando a rede voltar.");
  }
}

void loop() {
  unsigned long agora = millis();

  // ---- Reconexao de Wi-Fi ----
  if (agora - ultimaChecagemWifi >= WIFI_CHECK_INTERVAL_MS) {
    ultimaChecagemWifi = agora;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi desconectado - tentando reconectar...");
      WiFi.reconnect();
    }
  }

  if (agora - lastSampleTime < (1000 / SAMPLE_RATE_HZ)) return;
  lastSampleTime = agora;

  sensors_event_t event;
  accel.getEvent(&event);

  // ---- MAPEAMENTO DE EIXOS: Z=Horizontal radial, Y=Vertical, X=Axial ----
  float accH_g = event.acceleration.z / 9.80665;
  float accV_g = event.acceleration.y / 9.80665;
  float accA_g = event.acceleration.x / 9.80665;

  // Filtro passa-alta na aceleracao (remove offset DC / gravidade)
  float accH_f = hpfAccelH.apply(accH_g) * 9.80665; // volta para m/s^2
  float accV_f = hpfAccelV.apply(accV_g) * 9.80665;
  float accA_f = hpfAccelA.apply(accA_g) * 9.80665;

  // Integracao retangular simples -> velocidade em m/s
  velH += accH_f * DT;
  velV += accV_f * DT;
  velA += accA_f * DT;

  // Filtro passa-alta na velocidade (remove drift da integracao)
  float velH_f = hpfVelH.apply(velH) * 1000.0; // m/s -> mm/s
  float velV_f = hpfVelV.apply(velV) * 1000.0;
  float velA_f = hpfVelA.apply(velA) * 1000.0;

  bufVelH[bufIndex] = velH_f;
  bufVelV[bufIndex] = velV_f;
  bufVelA[bufIndex] = velA_f;
  bufIndex++;

  if (bufIndex < WINDOW_SAMPLES) return;

  // ---- Fim da janela de 1 s ----
  float rmsH = calcularRMS(bufVelH, WINDOW_SAMPLES);
  float rmsV = calcularRMS(bufVelV, WINDOW_SAMPLES);
  float rmsA = calcularRMS(bufVelA, WINDOW_SAMPLES);

  Serial.printf("%s | H=%.2f (%s) V=%.2f (%s) A=%.2f (%s)\n", macSufixo.c_str(),
                rmsH, statusISO(rmsH).c_str(), rmsV, statusISO(rmsV).c_str(),
                rmsA, statusISO(rmsA).c_str());
#if USAR_DISPLAY
  atualizarDisplay(rmsH, rmsV, rmsA);
#endif

  bool maquinaLigada = (rmsH >= LIMIAR_MAQUINA_LIGADA_MMS) &&
                       (rmsV >= LIMIAR_MAQUINA_LIGADA_MMS) &&
                       (rmsA >= LIMIAR_MAQUINA_LIGADA_MMS);

  unsigned long agoraMs = millis();
  unsigned long deltaJanelaMs = (ultimaJanelaMillis == 0) ? 0 : (agoraMs - ultimaJanelaMillis);
  ultimaJanelaMillis = agoraMs;

  if (maquinaLigada) {
    // Tempo de maquina ligada so pausa quando ela para - nao zera.
    acumuladorLigadoMs += deltaJanelaMs;
    somaH += rmsH;
    somaV += rmsV;
    somaA += rmsA;
    contagemAmostras++;
  }

  // ---- Fechamento do ciclo: congela a media e gera o ID do documento ----
  unsigned long alvoMs = primeiroEnvioFeito ? SEND_INTERVAL_MS : PRIMEIRO_ENVIO_MS;
  if (!envioPendente && acumuladorLigadoMs >= alvoMs) {
    if (contagemAmostras > 0) {
      pendenteH = somaH / contagemAmostras;
      pendenteV = somaV / contagemAmostras;
      pendenteA = somaA / contagemAmostras;
      pendenteJanelas = contagemAmostras;
      pendenteLigadoMin = acumuladorLigadoMs / 60000UL;
      char sufixo[9];
      snprintf(sufixo, sizeof(sufixo), "%08lX", (unsigned long)random(0x7FFFFFFF)); // random() usa o RNG de hardware no ESP32
      pendenteDocId = macId + "_" + String(sufixo);
      envioPendente = true;
      proximaTentativaMs = 0;
    }
    // O proximo ciclo comeca a acumular ja, mesmo que o envio deste demore.
    somaH = 0; somaV = 0; somaA = 0;
    contagemAmostras = 0;
    acumuladorLigadoMs = 0;
    primeiroEnvioFeito = true;
  }

  // ---- Envio (com nova tentativa a cada 5 min, mesmo documento) ----
  if (envioPendente && (proximaTentativaMs == 0 || (long)(agoraMs - proximaTentativaMs) >= 0)) {
    if (enviarMedicaoPendente()) {
      envioPendente = false;
      ultimoHeartbeat = agoraMs; // a medicao ja atualizou "visto"
    } else {
      proximaTentativaMs = agoraMs + RETRY_INTERVAL_MS;
    }
  }

  // ---- Heartbeat (sensor aparece "online" no painel) ----
  // Feito aqui, na fronteira da janela (como o envio), para a pausa do
  // HTTPS nao cair no meio de uma janela de amostragem.
  if (WiFi.status() == WL_CONNECTED &&
      (heartbeatPendente || agoraMs - ultimoHeartbeat >= HEARTBEAT_INTERVAL_MS)) {
    bool ok = enviarHeartbeat();
    heartbeatPendente = false;
    // falhou -> tenta de novo em 5 min em vez de esperar 1 h
    ultimoHeartbeat = ok ? agoraMs : agoraMs - HEARTBEAT_INTERVAL_MS + RETRY_INTERVAL_MS;
  }

  // Reinicia acumuladores de integracao para evitar crescimento sem limite
  velH = 0; velV = 0; velA = 0;
  bufIndex = 0;
}
