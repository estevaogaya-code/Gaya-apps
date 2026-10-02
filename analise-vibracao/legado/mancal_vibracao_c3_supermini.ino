/*
  Monitoramento de vibracao fixo - ESP32-C3 Super Mini + ADXL345 + OLED
  Ponto de fixacao: eixo HORIZONTAL do mancal

  ADAPTADO DO FIRMWARE ORIGINAL PARA ESP32-S3-N8R2/N16R8. Motivo da troca:
  o LED RGB onboard das placas S3 "Super Mini" (3 unidades testadas)
  apresentou falha consistente e nao resolvida por software (FastLED
  trava em branco, Adafruit_NeoPixel fica mudo, NeoPixelBus nem compila
  por bug de biblioteca com o core novo do ESP32). Trocando de familia de
  chip para descartar hardware problematico especifico dessas unidades.

  DIFERENCAS DE HARDWARE RELEVANTES (ESP32-C3 Super Mini x ESP32-S3 Super
  Mini) - motivo de cada pino ter mudado:
    - I2C: GPIO8/GPIO9 (usados no S3) sao pinos de STRAPPING/BOOT no C3
      (GPIO8 = LED onboard azul, GPIO9 = botao BOOT fisico). Usando os
      pinos oficialmente recomendados pelo fabricante para I2C nesta
      placa: SDA=GPIO4, SCL=GPIO5.
    - Botao BOOT fisico: fica no GPIO9 nesta placa (no S3 Super Mini era
      GPIO0). Usado para entrar no portal de configuracao sob demanda.
    - LED de status: NAO USADO neste firmware, por decisao do usuario (o
      LED RGB onboard das placas S3 anteriores deu problema recorrente em
      3 unidades, e a sinalizacao via LED foi descartada em favor de usar
      so o display OLED). O display mostra H/V/A com status (OK/ALERTA/
      ALARME) por eixo, cobrindo a mesma necessidade de indicacao visual.
    - Flash: 4MB (bem menor que os 8MB da S3-N8R2). O firmware compilado
      passa de 1,3MB, entao e OBRIGATORIO usar Partition Scheme "Huge APP
      (3MB No OTA/1MB SPIFFS)" com Flash Size "4MB" - o esquema padrao
      (1,2MB de app) NAO da conta deste firmware.

  CONFIGURACAO NA ARDUINO IDE:
    - Board: "ESP32C3 Dev Module"
    - USB CDC On Boot: Enabled (necessario para Serial Monitor via USB nativo)
    - Flash Size: 4MB
    - Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)

  DISPLAY: chip SH1106, biblioteca U8g2 (driver U8G2_SH1106_128X64_NONAME_F_HW_I2C).
  Mesma ressalva do firmware S3: Adafruit_SH110X nao inicializa esse painel
  corretamente nessa combinacao de hardware, U8g2 funciona normal.
  Requer biblioteca "U8g2" (Library Manager -> "U8g2 by oliver").

  SENSOR DE TEMPERATURA (NTC): nao incluido, mesma decisao do firmware anterior.

  ASSUNCOES QUE PRECISAM SER CONFERIDAS ANTES DE GRAVAR:

  1. Orientacao fisica do chip ADXL345 na montagem - mesma convencao do
     firmware anterior (confirme com o teste da gravidade na instalacao):
     - Eixo Z do chip -> HORIZONTAL radial
     - Eixo Y do chip -> VERTICAL
     - Eixo X do chip -> AXIAL
     Os valores H/V/A exibidos sao velocidade RMS ja filtrada (passa-alta),
     NAO mostram 9,8 mesmo com Y apontado pra cima - isso e esperado.

  2. Integracao com o app do celular (analise-de-vibracao-fe8fc): a colecao
     "leituras" recebe UM DOCUMENTO POR EIXO, formato:
     { ts, moinho, ponto, eixo, valor, freqDominante }
     "eixo" precisa ser exatamente "Horizontal" | "Vertical" | "Axial".
     Moinho e Ponto (configurados via portal web) precisam bater EXATAMENTE
     com o catalogo do app (MOINHOS_DEFAULT / PONTOS_DEFAULT no JS).

  3. Wi-Fi FIXO em firmware (rede padrao "CCM", igual em todas as
     unidades - ver CORRECOES 2026-09-07 abaixo). So Moinho/Ponto sao
     configuraveis via portal web, e sao OPCIONAIS: se nao configurados,
     o sensor usa um nome provisorio baseado no MAC (ver secao de
     identificacao no setup()) e ja opera/envia dados normalmente.
     Para abrir o portal e configurar/renomear, segure o botao BOOT
     (GPIO9) por 3s com o sensor ja ligado e funcionando (NAO durante a
     energizacao - ver CORRECOES 2026-09-07). Acesse http://192.168.4.1
     na rede "VibraSensor-Config" (senha: vibracao123).

  4. Integracao velocidade = acumulo retangular simples, reiniciado a cada
     janela de 1s. O filtro passa-alta de velocidade absorve a descontinuidade.

  CORRECOES 2026-09-07 (sensores paravam de subir dados / portal de
  config nao abria apos regravar):

  a) GPIO9 e pino de STRAPPING de boot no ESP32-C3: se estiver em LOW
     (botao BOOT pressionado) no momento exato da energizacao/reset, o
     chip entra no bootloader UART de gravacao (esperando o esptool) e
     NUNCA chega a rodar o firmware - a tela fica apagada e nada
     acontece. Por isso "segurar o BOOT ao ligar" para entrar no portal,
     do jeito que o firmware anterior (e a doc antiga deste arquivo)
     descrevia, e uma armadilha nesta placa: e fisicamente impossivel de
     funcionar de forma confiavel. Substituido por deteccao de
     pressionamento longo (3s) DURANTE a operacao normal (ver loop()),
     que nao depende do strapping.
  b) Sem logica de reconexao de Wi-Fi: se a conexao inicial no setup()
     falhasse ou caisse depois (rede fora do ar, sinal fraco), o sensor
     nunca mais tentava reconectar e ficava acumulando leituras
     localmente para sempre, sem jamais subir ao Firestore. Adicionada
     checagem periodica em loop() que chama WiFi.reconnect() quando cai.
  c) HTTPClient com "https://..." sem WiFiClientSecure/setInsecure()
     explicito pode falhar o handshake TLS silenciosamente dependendo da
     versao do core ESP32 instalada. Passado a usar WiFiClientSecure com
     setInsecure() nas duas chamadas HTTPS (POST ao Firestore e GET de
     sincronizacao de hora).
  d) Rede Wi-Fi fixada em firmware (SSID "CCM") para todas as unidades,
     eliminando configuracao errada/desatualizada salva na NVS de uma
     rede antiga como causa de "parou de subir dados".
*/

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// Display validado com U8g2 (driver SH1106). Troque para false se
// remover o display fisicamente.
#define USAR_DISPLAY true

#if USAR_DISPLAY
  #include <U8g2lib.h>
#endif

// ---------- Wi-Fi padrao (fixo em firmware - ver CORRECOES 2026-09-07) ----------
// Mesma rede para todas as unidades. Nao e configuravel via portal.
const char* DEFAULT_SSID  = "CCM";
const char* DEFAULT_SENHA = "***REMOVIDO***"; // senha real fora do repositorio

// ---------- Configuracao gravada em memoria nao-volatil (NVS) ----------
// Moinho e Ponto (unicos campos configuraveis) NAO ficam fixos no
// codigo-fonte. Tem 3 fontes possiveis, nesta ordem de prioridade:
//   1. NOMEACAO REMOTA (ver secao abaixo): se existir um documento
//      "sensores/<macId>" no Firestore com moinho/ponto preenchidos,
//      esse nome e adotado automaticamente (e cacheado na NVS) - e o
//      fluxo pensado p/ uso normal: nomear pelo app, sem tocar no sensor.
//   2. Portal web local (mantendo o BOOT pressionado 3s) - uso manual/
//      offline, sobrescrito pela nomeacao remota na proxima checagem se
//      houver um documento remoto para este macId.
//   3. Nome provisorio baseado no MAC (Sensor-XXXX / "A CONFIGURAR"), se
//      nenhuma das duas fontes acima tiver nada ainda - o sensor ja
//      opera e envia dados com esse nome enquanto isso.
Preferences prefs;
String configMoinho = "";
String configPonto  = "";
String macCompleto  = ""; // MAC sem ":", maiusculo - chave do documento sensores/<macId>
String macSufixo    = ""; // ultimos 4 chars do MAC - so para exibicao/placeholder

const char* FIRESTORE_BASE_URL =
  "https://firestore.googleapis.com/v1/projects/analise-de-vibracao-fe8fc/databases/(default)/documents";
const char* FIRESTORE_URL =
  "https://firestore.googleapis.com/v1/projects/analise-de-vibracao-fe8fc/databases/(default)/documents/leituras";

// ---------- Nomeacao remota (Moinho/Ponto atribuidos pelo app) ----------
// Consulta o documento "sensores/<macId>" no Firestore. Se existir com
// os 2 campos preenchidos, o app ja atribuiu um nome definitivo a este
// sensor (via tela de "Atribuir nome" no index.html) - nesse caso ele
// tem prioridade sobre o que estiver salvo localmente. Se nao existir
// (404) ou estiver incompleto, retorna false e o firmware mantem o nome
// atual (remoto ou provisorio) sem alteracao.
// AVISO: requer que as regras de seguranca do Firestore permitam leitura
// (get) da colecao "sensores" sem autenticacao, igual ao que ja e feito
// hoje para escrever em "leituras".
bool buscarNomeRemoto(String &moinhoOut, String &pontoOut) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(FIRESTORE_BASE_URL) + "/sensores/" + macCompleto;
  http.begin(client, url);

  int codigo = http.GET();
  bool ok = false;
  if (codigo == 200) {
    String corpo = http.getString();
    Serial.print("  [debug] Resposta bruta de sensores/"); Serial.print(macCompleto);
    Serial.print(": "); Serial.println(corpo);

    String moinho = extrairCampoStringJSON(corpo, "moinho");
    String ponto  = extrairCampoStringJSON(corpo, "ponto");
    if (moinho.length() > 0 && ponto.length() > 0) {
      moinhoOut = moinho;
      pontoOut  = ponto;
      ok = true;
    } else {
      Serial.println("Nomeacao remota: documento existe (200) mas campos "
                      "'moinho'/'ponto' vieram vazios ou em formato "
                      "inesperado - veja a resposta bruta acima.");
    }
  } else if (codigo == 404) {
    Serial.println("Nomeacao remota: nenhum nome atribuido ainda para este MAC (normal).");
  } else {
    Serial.printf("Nomeacao remota: erro HTTP %d ao consultar sensores/%s\n", codigo, macCompleto.c_str());
  }
  http.end();
  return ok;
}

// ---------- Acelerometro ----------
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// ---------- Display OLED (so existe se USAR_DISPLAY = true) ----------
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
const unsigned long SEND_INTERVAL_MS  = 12UL * 60UL * 60UL * 1000UL; // envia ao Firestore a cada 12 horas DE MAQUINA LIGADA

// Limiar de "maquina ligada": os 3 eixos (H, V, A) precisam estar
// simultaneamente acima disso na janela de 1s para ela contar como
// tempo de maquina ligada. Evita que leituras de maquina parada/
// vibracao residual puxem a media para baixo e mascarem um alarme real
// durante o periodo em que estava efetivamente operando.
const float LIMIAR_MAQUINA_LIGADA_MMS = 1.5;
const unsigned long RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL;        // se falhar, tenta de novo em 5 min (nao espera as 12h inteiras)

// Acumuladores para media das leituras RMS entre um envio e outro
double somaH = 0, somaV = 0, somaA = 0;
unsigned long contagemAmostras = 0;

// Tempo de maquina ligada acumulado (nao e tempo de relogio). Se algum
// eixo cair abaixo do limiar (maquina parada), a contagem so FICA
// PAUSADA - nao zera. Quando a maquina voltar a rodar, ela continua de
// onde parou. Ao atingir SEND_INTERVAL_MS de tempo ligado acumulado,
// envia a media ao Firestore e zera o acumulador.
unsigned long acumuladorLigadoMs  = 0;
unsigned long ultimaJanelaMillis  = 0; // p/ calcular o delta real entre janelas
unsigned long proximaTentativaMs  = 0; // throttle de reenvio em caso de falha

// ---------- Sincronizacao de hora via HTTPS (fallback quando NTP/UDP falha) ----------
// Algumas redes bloqueiam a porta UDP 123 (NTP) mas permitem HTTPS normalmente.
// Como toda resposta HTTP/HTTPS traz um cabecalho "Date" com a hora atual do
// servidor, aproveitamos isso pra acertar o relogio sem depender de NTP.

// Extrai um campo string de uma resposta JSON do Firestore REST, formato
// "fields":{"<campo>":{"stringValue":"<valor>"},...}. Parsing manual (sem
// biblioteca JSON), tolerante a espacos/quebras de linha entre tokens -
// a API do Firestore retorna JSON "pretty printed" (com indentacao), nao
// compacto, entao nao da pra buscar a string exata "campo":{"stringValue":
// sem espaco. Em vez disso, localiza cada token separadamente e pula
// qualquer espaco em branco entre eles. Assume que o valor nao contem
// aspas nao-escapadas, mesma limitacao ja existente na montagem do
// payload em enviarDocumentoEixo. Retorna "" se o campo nao for encontrado.
String extrairCampoStringJSON(const String& json, const String& campo) {
  String chaveCampo = "\"" + campo + "\"";
  int posCampo = json.indexOf(chaveCampo);
  if (posCampo < 0) return "";

  int posStringValue = json.indexOf("\"stringValue\"", posCampo + chaveCampo.length());
  if (posStringValue < 0) return "";

  int posDoisPontos = json.indexOf(':', posStringValue);
  if (posDoisPontos < 0) return "";

  int posAspaAbertura = json.indexOf('"', posDoisPontos + 1);
  if (posAspaAbertura < 0) return "";

  int posAspaFechamento = json.indexOf('"', posAspaAbertura + 1);
  if (posAspaFechamento < 0) return "";

  return json.substring(posAspaAbertura + 1, posAspaFechamento);
}

// Converte data/hora em UTC para epoch em segundos, sem depender do fuso
// horario configurado no sistema (calculo manual, sempre em UTC).
time_t utcParaEpoch(int ano, int mes, int dia, int hora, int min, int seg) {
  static const int diasAcumulados[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  long dias = 0;
  for (int y = 1970; y < ano; y++) {
    dias += ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
  }
  dias += diasAcumulados[mes - 1];
  if (mes > 2 && ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0)) dias += 1;
  dias += (dia - 1);
  return (time_t)dias * 86400L + hora * 3600L + min * 60L + seg;
}

// Interpreta o cabecalho HTTP Date, formato RFC 7231: "Wed, 21 Oct 2015 07:28:00 GMT"
bool interpretarDataHTTP(const String& s, int &ano, int &mes, int &dia, int &hora, int &min, int &seg) {
  const char* meses[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int p1 = s.indexOf(' ');
  int p2 = s.indexOf(' ', p1 + 1);
  int p3 = s.indexOf(' ', p2 + 1);
  int p4 = s.indexOf(' ', p3 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0) return false;

  dia = s.substring(p1 + 1, p2).toInt();
  String mesStr = s.substring(p2 + 1, p3);
  mes = 0;
  for (int i = 0; i < 12; i++) if (mesStr == meses[i]) { mes = i + 1; break; }
  if (mes == 0) return false;
  ano = s.substring(p3 + 1, p4).toInt();

  String horaStr = s.substring(p4 + 1, p4 + 9); // "07:28:00"
  if (horaStr.length() < 8) return false;
  hora = horaStr.substring(0, 2).toInt();
  min  = horaStr.substring(3, 5).toInt();
  seg  = horaStr.substring(6, 8).toInt();
  return true;
}

// Faz uma requisicao HTTPS leve so pra ler o cabecalho Date e acertar o
// relogio do ESP32 com ele. Retorna true se conseguiu.
bool sincronizarHoraViaHTTP() {
  WiFiClientSecure client;
  client.setInsecure(); // sem validacao de certificado - so lemos o header Date, nao ha dado sensivel aqui
  HTTPClient http;
  http.begin(client, "https://firestore.googleapis.com/"); // mesmo host que ja usamos p/ enviar leituras - sabemos que funciona nessa rede
  const char* headersDesejados[] = {"Date"};
  http.collectHeaders(headersDesejados, 1);

  int codigo = http.GET();
  Serial.printf("  [debug] sincronizarHoraViaHTTP: GET retornou codigo %d\n", codigo);
  bool ok = false;
  if (codigo > 0) {
    String dataHeader = http.header("Date");
    Serial.printf("  [debug] cabecalho Date recebido: \"%s\"\n", dataHeader.c_str());
    int ano, mes, dia, hora, min, seg;
    if (dataHeader.length() > 0 && interpretarDataHTTP(dataHeader, ano, mes, dia, hora, min, seg)) {
      time_t epoch = utcParaEpoch(ano, mes, dia, hora, min, seg);
      struct timeval tv = { epoch, 0 };
      settimeofday(&tv, NULL);
      Serial.printf("Hora obtida via HTTPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n", ano, mes, dia, hora, min, seg);
      ok = true;
    } else {
      Serial.println("  [debug] falha ao interpretar o cabecalho Date (vazio ou formato inesperado).");
    }
  } else {
    Serial.printf("  [debug] GET falhou: %s\n", http.errorToString(codigo).c_str());
  }
  http.end();
  return ok;
}

// ---------- PORTAL DE CONFIGURACAO (Moinho/Ponto via navegador) ----------
// Segure o botao BOOT por 3s (deteccao em loop() - NUNCA durante a
// energizacao, ver CORRECOES 2026-09-07 no cabecalho) para o ESP32 virar
// um ponto de acesso Wi-Fi proprio. Conecte um celular/notebook nessa
// rede, abra o navegador (a maioria dos aparelhos abre a pagina de
// configuracao sozinho, tipo "login de rede publica"; se nao abrir
// sozinho, acesse 192.168.4.1 manualmente) e preencha Moinho/Ponto. Ao
// salvar, o sensor reinicia e volta a conectar na rede Wi-Fi fixa.

#define BOTAO_BOOT_PIN 9 // GPIO9 no ESP32-C3 Super Mini - e pino de strapping de boot, so ler DEPOIS do boot (ver CORRECOES 2026-09-07)

WebServer servidorConfig(80);
DNSServer dnsServidor;
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

void carregarConfiguracaoSalva() {
  prefs.begin("config", true); // somente leitura
  configMoinho = prefs.getString("moinho", "");
  configPonto  = prefs.getString("ponto", "");
  prefs.end();
}

void salvarConfiguracao(const String& moinho, const String& ponto) {
  prefs.begin("config", false); // leitura/escrita
  prefs.putString("moinho", moinho);
  prefs.putString("ponto", ponto);
  prefs.end();
}

String paginaConfigHTML() {
  String html = R"HTML(
<!DOCTYPE html><html lang="pt-BR"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Configurar Sensor de Vibracao</title>
<style>
  body{background:#12151a;color:#e4e7eb;font-family:Arial,sans-serif;margin:0;padding:20px;}
  .card{background:#1b1f26;border:1px solid #2a2f38;border-radius:10px;padding:20px;max-width:420px;margin:0 auto;}
  h1{font-size:18px;margin:0 0 4px;}
  p{font-size:12px;color:#8891a0;margin:0 0 18px;}
  label{display:block;font-size:12px;color:#8891a0;margin:14px 0 4px;}
  input,select{width:100%;padding:10px;border-radius:6px;border:1px solid #2a2f38;background:#20252d;color:#e4e7eb;font-size:14px;box-sizing:border-box;}
  button{width:100%;padding:12px;margin-top:20px;border:none;border-radius:6px;background:#3fa9f5;color:#fff;font-size:15px;font-weight:600;cursor:pointer;}
  .aviso{font-size:11px;color:#e8a83f;margin-top:14px;}
</style></head><body>
<div class="card">
  <h1>Configurar Sensor de Vibracao</h1>
  <p>Wi-Fi e fixo (rede "CCM") - so a identificacao deste ponto de medicao e configuravel.</p>
  <form action="/salvar" method="POST">
    <label>Moinho (ex: 501BM140)</label>
    <input type="text" name="moinho" value="%MOINHO%" required>
    <label>Ponto (ex: Mancal Motor LOA)</label>
    <input type="text" name="ponto" value="%PONTO%" required>
    <button type="submit">Salvar e reiniciar</button>
  </form>
  <div class="aviso">O texto do Moinho/Ponto precisa bater exatamente com o catalogo do aplicativo (mesmos espacos e maiusculas).</div>
</div>
</body></html>
)HTML";
  html.replace("%MOINHO%", configMoinho);
  html.replace("%PONTO%", configPonto);
  return html;
}

void iniciarPortalConfiguracao() {
  Serial.println("Iniciando portal de configuracao...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("VibraSensor-Config", "vibracao123"); // senha fixa minima (WPA2 exige >=8 caracteres)
  dnsServidor.start(DNS_PORT, "*", apIP); // redireciona qualquer DNS para o proprio ESP32 (captive portal)

  servidorConfig.on("/", HTTP_GET, []() {
    servidorConfig.send(200, "text/html", paginaConfigHTML());
  });

  servidorConfig.on("/salvar", HTTP_POST, []() {
    String moinho = servidorConfig.arg("moinho");
    String ponto  = servidorConfig.arg("ponto");
    salvarConfiguracao(moinho, ponto);
    servidorConfig.send(200, "text/html",
      "<html><body style='background:#12151a;color:#e4e7eb;font-family:Arial;text-align:center;padding-top:60px;'>"
      "<h2>Configuracao salva.</h2><p>Reiniciando...</p></body></html>");
    delay(1500);
    ESP.restart();
  });

  // Qualquer outro caminho redireciona para "/" - ajuda o celular a abrir
  // a pagina de configuracao sozinho (deteccao de portal cativo)
  servidorConfig.onNotFound([]() {
    servidorConfig.sendHeader("Location", "/", true);
    servidorConfig.send(302, "text/plain", "");
  });

  servidorConfig.begin();
  Serial.println("Portal ativo. Conecte-se a rede \"VibraSensor-Config\" (senha: vibracao123) e acesse http://192.168.4.1");

  // Loop dedicado - nunca sai daqui enquanto o portal estiver ativo.
  // O ESP32 reinicia sozinho apos salvar (ESP.restart() dentro do handler /salvar).
  while (true) {
    dnsServidor.processNextRequest();
    servidorConfig.handleClient();
    delay(1);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // Display inicializado JA NO INICIO, antes de qualquer outra logica.
  // Antes disso o u8g2.begin() so rodava depois da checagem do ADXL345
  // e da decisao de entrar (ou nao) no portal de configuracao - se
  // qualquer uma dessas travasse ou desviasse o fluxo (accel.begin()
  // falhando -> while(1) trava; ou config incompleta -> portal, que tem
  // seu proprio loop e nunca retorna), a tela nunca chegava a acender.
  // Inicializando aqui, a tela sempre mostra algo, mesmo em caso de erro.
  Wire.begin(4, 5); // SDA=GPIO4, SCL=GPIO5 (recomendacao oficial p/ ESP32-C3 Super Mini - evita GPIO8/9)
#if USAR_DISPLAY
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.clearBuffer();
  u8g2.drawStr(0, 14, "Iniciando...");
  u8g2.sendBuffer();
#endif

  pinMode(BOTAO_BOOT_PIN, INPUT_PULLUP);

  // Identificador unico do sensor, baseado no MAC do chip. Usado como
  // Ponto padrao enquanto o sensor nao for configurado manualmente -
  // permite ligar o sensor e ja comecar a operar/enviar dados sem
  // precisar passar pelo portal antes (ver ASSUNCAO 3 no cabecalho).
  WiFi.mode(WIFI_STA);
  macCompleto = WiFi.macAddress(); // "AA:BB:CC:DD:EE:FF"
  macCompleto.replace(":", "");
  macCompleto.toUpperCase();
  macSufixo = macCompleto.substring(macCompleto.length() - 4);

  carregarConfiguracaoSalva();
  if (configMoinho == "") configMoinho = "A CONFIGURAR";
  if (configPonto  == "") configPonto  = "Sensor-" + macSufixo;

  Serial.printf("MAC: %s | Moinho/Ponto atuais: %s / %s\n",
                macSufixo.c_str(), configMoinho.c_str(), configPonto.c_str());

#if USAR_DISPLAY
  u8g2.clearBuffer();
  u8g2.drawStr(0, 14, ("ID: Sensor-" + macSufixo).c_str());
  u8g2.drawStr(0, 34, configMoinho.c_str());
  u8g2.drawStr(0, 54, configPonto.c_str());
  u8g2.sendBuffer();
  delay(2500); // tempo p/ o tecnico ler o ID/config na instalacao
#endif

  if (!accel.begin()) {
    Serial.println("ADXL345 nao encontrado. Verifique a fiacao I2C.");
#if USAR_DISPLAY
    u8g2.clearBuffer();
    u8g2.drawStr(0, 14, "ERRO:");
    u8g2.drawStr(0, 34, "ADXL345 nao");
    u8g2.drawStr(0, 54, "encontrado!");
    u8g2.sendBuffer();
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

  WiFi.begin(DEFAULT_SSID, DEFAULT_SENHA);
  Serial.printf("Conectando ao Wi-Fi \"%s\"", DEFAULT_SSID);
  const unsigned long WIFI_TIMEOUT_MS = 20000; // desiste apos 20s e segue sem rede
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" conectado.");

    // Forca DNS manual em IPv4 (algumas redes entregam DNS via IPv6 que o
    // stack do ESP32 nao resolve corretamente - ja vimos isso derrubar
    // NTP e chamadas HTTPS ao Firestore no firmware anterior).
    IPAddress ipAtual = WiFi.localIP();
    IPAddress gatewayAtual = WiFi.gatewayIP();
    IPAddress mascaraAtual = WiFi.subnetMask();
    WiFi.config(ipAtual, gatewayAtual, mascaraAtual, IPAddress(8,8,8,8), IPAddress(1,1,1,1));
    delay(200); // da tempo do stack aplicar a mudanca

    Serial.print("IP: "); Serial.println(WiFi.localIP());
    Serial.print("Gateway: "); Serial.println(WiFi.gatewayIP());
    Serial.print("DNS: "); Serial.println(WiFi.dnsIP());
    Serial.print("RSSI: "); Serial.println(WiFi.RSSI());

    IPAddress ipTeste;
    bool dnsOk = WiFi.hostByName("firestore.googleapis.com", ipTeste) && ipTeste != IPAddress(0,0,0,0);
    if (dnsOk) {
      Serial.print("DNS OK - firestore.googleapis.com resolveu para: ");
      Serial.println(ipTeste);
    } else {
      Serial.println("DNS FALHOU - nao conseguiu resolver firestore.googleapis.com (ou resolveu 0.0.0.0). Provavel problema de DNS/gateway na rede.");
    }

    // Nomeacao remota: se o app ja atribuiu um nome definitivo a este
    // MAC, adota agora (e cacheia na NVS) antes do primeiro envio.
    String moinhoRemoto, pontoRemoto;
    if (buscarNomeRemoto(moinhoRemoto, pontoRemoto)) {
      if (moinhoRemoto != configMoinho || pontoRemoto != configPonto) {
        configMoinho = moinhoRemoto;
        configPonto  = pontoRemoto;
        salvarConfiguracao(configMoinho, configPonto);
        Serial.printf("Nomeacao remota aplicada: %s / %s\n", configMoinho.c_str(), configPonto.c_str());
      }
    }

    // Sincroniza hora real via NTP (necessario para o campo "ts" bater
    // com o Date.now() do app do celular - sem isso, millis() geraria
    // timestamps de 1970 no historico compartilhado).
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.google.com"); // UTC-3 Brasil, sem horario de verao
    Serial.print("Sincronizando hora (NTP)");
    struct tm timeinfo;
    unsigned long ntpStart = millis();
    while (!getLocalTime(&timeinfo, 1000) && millis() - ntpStart < 10000) {
      Serial.print(".");
    }
    if (getLocalTime(&timeinfo, 100)) {
      Serial.println(" hora sincronizada.");
    } else {
      Serial.println(" falhou via NTP (provavel porta UDP 123 bloqueada pela rede). Tentando via HTTPS...");
      if (sincronizarHoraViaHTTP()) {
        Serial.println("Hora sincronizada via HTTPS (fallback).");
      } else {
        Serial.println("Falhou tambem via HTTPS. Timestamps podem ficar incorretos ate a proxima tentativa.");
      }
    }
  } else {
    Serial.println(" nao conectou em 20s. Seguindo sem Wi-Fi (leitura local segue normal, envio ao Firestore fica pendente ate a rede aparecer).");
  }
}

float calcularRMS(float* buffer, int n) {
  float somaQuadrados = 0;
  for (int i = 0; i < n; i++) somaQuadrados += buffer[i] * buffer[i];
  return sqrt(somaQuadrados / n);
}

String statusISO(float mms) {
  if (mms >= LIMIAR_ALARME_MMS) return "ALARME";
  if (mms >= LIMIAR_ALERTA_MMS) return "ALERTA";
  return "OK";
}

#if USAR_DISPLAY
void atualizarDisplay(float rmsH, float rmsV, float rmsA) {
  char linha1[24], linha2[24], linha3[24];
  snprintf(linha1, sizeof(linha1), "H:%.2f %s", rmsH, statusISO(rmsH).c_str());
  snprintf(linha2, sizeof(linha2), "V:%.2f %s", rmsV, statusISO(rmsV).c_str());
  snprintf(linha3, sizeof(linha3), "A:%.2f %s", rmsA, statusISO(rmsA).c_str());

  u8g2.clearBuffer();
  u8g2.drawStr(0, 14, linha1);
  u8g2.drawStr(0, 34, linha2);
  u8g2.drawStr(0, 54, linha3);
  u8g2.sendBuffer();
}
#endif

void imprimirSerial(float rmsH, float rmsV, float rmsA) {
  Serial.printf("%s | %s | H=%.2f mm/s (%s) | V=%.2f mm/s (%s) | A=%.2f mm/s (%s)\n",
                configMoinho.c_str(), configPonto.c_str(),
                rmsH, statusISO(rmsH).c_str(),
                rmsV, statusISO(rmsV).c_str(),
                rmsA, statusISO(rmsA).c_str());
}

// Retorna epoch em milissegundos (equivalente ao Date.now() do JS),
// valido somente apos configTime()/NTP ter sincronizado com sucesso.
long long obterEpochMillis() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000LL + (tv.tv_usec / 1000);
}

// Envia um unico documento na colecao "leituras", no mesmo formato
// usado pelo app do celular (funcao salvarRegistro/JS):
// { ts, moinho, ponto, eixo, valor, freqDominante }
// Campos "metodo", "usuario", "empresa" ficam de fora por decisao do
// usuario - nao se aplicam a uma leitura automatica do sensor fixo.
bool enviarDocumentoEixo(const char* eixoNome, float valor, long long tsMillis) {
  WiFiClientSecure client;
  client.setInsecure(); // sem validacao de certificado (ver nota em sincronizarHoraViaHTTP)
  HTTPClient http;
  http.begin(client, FIRESTORE_URL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"fields\":{";
  payload += "\"ts\":{\"integerValue\":\"" + String((unsigned long long)tsMillis) + "\"},";
  payload += "\"moinho\":{\"stringValue\":\"" + configMoinho + "\"},";
  payload += "\"ponto\":{\"stringValue\":\"" + configPonto + "\"},";
  payload += "\"eixo\":{\"stringValue\":\"" + String(eixoNome) + "\"},";
  payload += "\"valor\":{\"doubleValue\":" + String(valor, 3) + "},";
  payload += "\"freqDominante\":{\"nullValue\":null},";
  // macId identifica o sensor fisico de forma unica - usado pelo app pra
  // atribuir um nome definitivo (tela "Atribuir nome") mesmo enquanto
  // moinho/ponto ainda sao o placeholder baseado no MAC.
  payload += "\"macId\":{\"stringValue\":\"" + macCompleto + "\"}";
  payload += "}}";

  int httpCode = http.POST(payload);
  Serial.printf("Firestore POST [%s] -> %d\n", eixoNome, httpCode);
  http.end();

  return (httpCode >= 200 && httpCode < 300);
}

// Envia os 3 eixos como documentos separados. So retorna sucesso (true)
// se os 3 forem enviados com sucesso - senao o chamador mantem o
// acumulador de media intacto e tenta tudo de novo no proximo ciclo.
bool enviarFirestore(float rmsH, float rmsV, float rmsA) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Envio ao Firestore adiado: sem Wi-Fi. Media continua acumulando.");
    return false;
  }

  long long ts = obterEpochMillis();

  bool okH = enviarDocumentoEixo("Horizontal", rmsH, ts);
  bool okV = enviarDocumentoEixo("Vertical",   rmsV, ts);
  bool okA = enviarDocumentoEixo("Axial",      rmsA, ts);

  return okH && okV && okA;
}

// Reconexao periodica de Wi-Fi - sem isso, uma queda de rede depois do
// setup() deixava o sensor sem jamais tentar de novo (ver CORRECOES
// 2026-09-07 no cabecalho).
const unsigned long WIFI_CHECK_INTERVAL_MS = 30UL * 1000UL;
unsigned long ultimaChecagemWifi = 0;

// Rechecagem periodica de nomeacao remota - permite que um nome
// atribuido pelo app (tela "Atribuir nome" no index.html) seja adotado
// sem precisar reiniciar o sensor. 30 min e suficiente: o que importa e
// que o nome novo esteja em vigor antes do PROXIMO envio ao Firestore
// (que so acontece a cada 12h de maquina ligada).
const unsigned long NOME_REMOTO_CHECK_INTERVAL_MS = 30UL * 60UL * 1000UL;
unsigned long ultimaChecagemNomeRemoto = 0;

// Long-press do botao BOOT (3s) DURANTE a operacao normal, para abrir o
// portal de configuracao. Substitui o antigo metodo de segurar o botao
// na energizacao, que nao funciona nesta placa (ver CORRECOES
// 2026-09-07 no cabecalho).
const unsigned long BOTAO_LONG_PRESS_MS = 3000;
unsigned long botaoPressionadoDesde = 0;

void loop() {
  // ---- Checagem do botao BOOT (nao depende do gate de amostragem abaixo) ----
  if (digitalRead(BOTAO_BOOT_PIN) == LOW) {
    if (botaoPressionadoDesde == 0) {
      botaoPressionadoDesde = millis();
    } else if (millis() - botaoPressionadoDesde >= BOTAO_LONG_PRESS_MS) {
      Serial.println("Botao BOOT segurado por 3s - entrando no portal de configuracao.");
#if USAR_DISPLAY
      u8g2.clearBuffer();
      u8g2.drawStr(0, 14, "Modo config!");
      u8g2.drawStr(0, 34, "Rede: VibraSensor");
      u8g2.drawStr(0, 54, "192.168.4.1");
      u8g2.sendBuffer();
#endif
      iniciarPortalConfiguracao(); // nunca retorna (fica em loop proprio ate salvar+reiniciar)
    }
  } else {
    botaoPressionadoDesde = 0;
  }

  // ---- Rechecagem de nomeacao remota (app pode ter atribuido um nome) ----
  unsigned long agoraNome = millis();
  if (agoraNome - ultimaChecagemNomeRemoto >= NOME_REMOTO_CHECK_INTERVAL_MS) {
    ultimaChecagemNomeRemoto = agoraNome;
    String moinhoRemoto, pontoRemoto;
    if (buscarNomeRemoto(moinhoRemoto, pontoRemoto)) {
      if (moinhoRemoto != configMoinho || pontoRemoto != configPonto) {
        configMoinho = moinhoRemoto;
        configPonto  = pontoRemoto;
        salvarConfiguracao(configMoinho, configPonto);
        Serial.printf("Nomeacao remota atualizada: %s / %s\n", configMoinho.c_str(), configPonto.c_str());
      }
    }
  }

  // ---- Reconexao de Wi-Fi, se tiver caido ----
  unsigned long agoraCheck = millis();
  if (agoraCheck - ultimaChecagemWifi >= WIFI_CHECK_INTERVAL_MS) {
    ultimaChecagemWifi = agoraCheck;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wi-Fi desconectado - tentando reconectar...");
      WiFi.reconnect();
    }
  }

  unsigned long agora = millis();
  if (agora - lastSampleTime < (1000 / SAMPLE_RATE_HZ)) return;
  lastSampleTime = agora;

  sensors_event_t event;
  accel.getEvent(&event);

  // ---- MAPEAMENTO DE EIXOS (ver ASSUNCAO 1 no cabecalho) ----
  // Mesma convencao do firmware anterior: Z=Horizontal radial, Y=Vertical, X=Axial
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

  if (bufIndex >= WINDOW_SAMPLES) {
    float rmsH = calcularRMS(bufVelH, WINDOW_SAMPLES);
    float rmsV = calcularRMS(bufVelV, WINDOW_SAMPLES);
    float rmsA = calcularRMS(bufVelA, WINDOW_SAMPLES);

    imprimirSerial(rmsH, rmsV, rmsA);

#if USAR_DISPLAY
    atualizarDisplay(rmsH, rmsV, rmsA);
#endif

    // ---- Maquina ligada = os 3 eixos acima do limiar nesta janela ----
    bool maquinaLigada = (rmsH >= LIMIAR_MAQUINA_LIGADA_MMS) &&
                          (rmsV >= LIMIAR_MAQUINA_LIGADA_MMS) &&
                          (rmsA >= LIMIAR_MAQUINA_LIGADA_MMS);

    unsigned long agoraMs = millis();
    unsigned long deltaJanelaMs = (ultimaJanelaMillis == 0) ? 0 : (agoraMs - ultimaJanelaMillis);
    ultimaJanelaMillis = agoraMs;

    if (maquinaLigada) {
      // Soma tempo de maquina ligada. Se a maquina tiver ficado parada
      // (alguma leitura abaixo do limiar) a contagem so estava pausada -
      // aqui ela CONTINUA de onde parou, nao reinicia do zero.
      acumuladorLigadoMs += deltaJanelaMs;

      // So entra na media quem foi de fato lido com a maquina ligada
      somaH += rmsH;
      somaV += rmsV;
      somaA += rmsA;
      contagemAmostras++;
    }
    // Se a maquina estiver parada (algum eixo abaixo do limiar), a
    // contagem de tempo e a media simplesmente nao avancam nesta janela.

    if (acumuladorLigadoMs >= SEND_INTERVAL_MS &&
        (proximaTentativaMs == 0 || agoraMs >= proximaTentativaMs)) {
      if (contagemAmostras > 0) {
        float mediaH = somaH / contagemAmostras;
        float mediaV = somaV / contagemAmostras;
        float mediaA = somaA / contagemAmostras;
        bool enviado = enviarFirestore(mediaH, mediaV, mediaA);
        if (enviado) {
          somaH = 0;
          somaV = 0;
          somaA = 0;
          contagemAmostras = 0;
          acumuladorLigadoMs = 0;
          proximaTentativaMs = 0;
        } else {
          // Nao conseguiu enviar (sem Wi-Fi ou erro HTTP): mantem o
          // acumulador de tempo ligado E a media intactos, e so tenta
          // de novo em RETRY_INTERVAL_MS - sem perder o tempo ja
          // acumulado nem esperar mais 12h de maquina ligada.
          proximaTentativaMs = agoraMs + RETRY_INTERVAL_MS;
        }
      } else {
        // Bateu o tempo mas nao ha amostras validas (nao deveria
        // acontecer, ja que so somamos tempo junto com a amostra) -
        // por seguranca, zera para nao travar em loop de tentativa.
        acumuladorLigadoMs = 0;
      }
    }

    // Reinicia acumuladores de integracao para evitar crescimento sem limite
    velH = 0; velV = 0; velA = 0;
    bufIndex = 0;
  }
}
