// =====================================================================
//  FarmIO - main_cam.cpp
//  Firmware da ESP32-CAM (AI-Thinker). Faz duas coisas independentes:
//
//    pelo fio   responde ao vaso: "estou viva" e "tem planta na frente"
//    pelo radio serve o video MJPEG em http://<ip>:81/stream
//
//  AS DUAS NAO PODEM ATRAPALHAR UMA A OUTRA. O video roda no servidor
//  HTTP do IDF, que vive na propria tarefa do FreeRTOS; o enlace roda no
//  loop() do Arduino. Se o video estivesse no mesmo loop, um cliente
//  lento segurando o socket travaria a resposta ao vaso, o vaso veria
//  camera muda e pulsaria o reset - a camera reiniciaria por causa de um
//  navegador aberto. Tarefa separada e o que impede isso.
//
//  POR QUE JPEG E NAO RGB565 DIRETO DO SENSOR. O classificador quer
//  RGB565, mas o sensor so entrega um formato por vez, e RGB565 continuo
//  em QVGA nao cabe no barramento junto com o streaming. Entao o sensor
//  fica em JPEG - que e o que o video quer - e uma vez a cada dez
//  segundos um unico quadro e decodificado para RGB565 em 160x120, so
//  para classificar. Decodificar um quadro a cada dez custa menos que
//  transmitir todos em RGB565.
//
//  Gravar:  pio run -e cam -t upload
//  (com o adaptador USB-TTL no header de gravacao e GPIO0 no GND;
//   o enlace com o vaso usa GPIO14/15 e nao precisa ser desligado)
// =====================================================================
#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"

#include "config.h"
#include "farmio_enlace.h"
#include "farmio_visao.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef FARMIO_WIFI_SSID
#define FARMIO_WIFI_SSID ""
#define FARMIO_WIFI_PASS ""
#endif

// A camera nao tem AP proprio: sem credencial ela simplesmente nao serve
// video. O enlace com o vaso continua funcionando - a deteccao de planta
// nao depende de rede nenhuma, e essa e a razao de ela morar no fio.
static HardwareSerial Enl(1);
static Enlace::Receptor g_rec;

static bool g_cameraOk       = false;
static uint8_t* g_rgb        = nullptr;  // 160x120 RGB565 para o classificador
static uint32_t g_ultimoPing = 0;
static uint32_t g_quadros    = 0;
static Visao::Parametros g_par;

static const int RGB_LARG     = 160;
static const int RGB_ALT      = 120;
static const size_t RGB_BYTES = (size_t)RGB_LARG * RGB_ALT * 2;

// ---------------------------------------------------------------------
//  Camera
// ---------------------------------------------------------------------
static bool iniciaCamera() {
  camera_config_t c;
  memset(&c, 0, sizeof(c));
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0       = CAM_PIN_D0;
  c.pin_d1       = CAM_PIN_D1;
  c.pin_d2       = CAM_PIN_D2;
  c.pin_d3       = CAM_PIN_D3;
  c.pin_d4       = CAM_PIN_D4;
  c.pin_d5       = CAM_PIN_D5;
  c.pin_d6       = CAM_PIN_D6;
  c.pin_d7       = CAM_PIN_D7;
  c.pin_xclk     = CAM_PIN_XCLK;
  c.pin_pclk     = CAM_PIN_PCLK;
  c.pin_vsync    = CAM_PIN_VSYNC;
  c.pin_href     = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn     = CAM_PIN_PWDN;
  c.pin_reset    = CAM_PIN_RESET;

  // 20 MHz e o padrao da AI-Thinker. Baixar para 10 MHz reduz o consumo
  // e a temperatura, ao custo de metade da taxa de quadros - e o que se
  // faz quando o orcamento de USB aperta (docs/06-energia-usb.md).
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_QVGA;  // 320x240
  c.jpeg_quality = 12;              // 10..63; menor = melhor e maior
  c.grab_mode    = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.fb_count    = 2;
  } else {
    // Sem PSRAM nao ha dois buffers de QVGA. Continua funcionando, com
    // um buffer so e taxa menor - a deteccao de planta nao se importa.
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.fb_count    = 1;
    c.frame_size  = FRAMESIZE_QQVGA;
  }

  const esp_err_t e = esp_camera_init(&c);
  if (e != ESP_OK) {
    Serial.printf("[cam] esp_camera_init falhou: 0x%x\n", e);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    // A OV2640 sai de fabrica com a imagem espelhada e de cabeca para
    // baixo em relacao ao encaixe mecanico da AI-Thinker.
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    // Saturacao no zero de proposito: o classificador mede cromaticidade,
    // e realce de saturacao empurraria terra avermelhada para dentro da
    // faixa de "verde" tanto quanto folha.
    s->set_saturation(s, 0);
  }
  return true;
}

// ---------------------------------------------------------------------
//  Servidor de video - tarefa propria do IDF, fora do loop()
// ---------------------------------------------------------------------
static const char* LIMITE = "farmioquadro";

static esp_err_t handlerStream(httpd_req_t* req) {
  char tipo[80];
  snprintf(tipo, sizeof(tipo), "multipart/x-mixed-replace;boundary=%s", LIMITE);
  if (httpd_resp_set_type(req, tipo) != ESP_OK) return ESP_FAIL;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char cab[96];
  for (;;) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return ESP_FAIL;

    const int n = snprintf(cab, sizeof(cab),
                           "\r\n--%s\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                           LIMITE, (unsigned)fb->len);
    esp_err_t r = httpd_resp_send_chunk(req, cab, n);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (r != ESP_OK) break;  // navegador fechou a aba
  }
  return ESP_OK;
}

static void iniciaServidorVideo() {
  httpd_config_t cfg   = HTTPD_DEFAULT_CONFIG();
  cfg.server_port      = 81;
  cfg.ctrl_port        = 32769;
  cfg.max_uri_handlers = 2;

  static httpd_handle_t srv = nullptr;
  if (httpd_start(&srv, &cfg) != ESP_OK) {
    Serial.println("[cam] servidor de video nao subiu");
    return;
  }
  httpd_uri_t u;
  memset(&u, 0, sizeof(u));
  u.uri     = "/stream";
  u.method  = HTTP_GET;
  u.handler = handlerStream;
  httpd_register_uri_handler(srv, &u);
  Serial.println("[cam] video em :81/stream");
}

// ---------------------------------------------------------------------
//  Classificacao de um quadro
// ---------------------------------------------------------------------
static bool classifica(Enlace::CargaVeredito& fora) {
  memset(&fora, 0, sizeof(fora));
  if (!g_cameraOk || !g_rgb) {
    fora.flags |= Enlace::FLAG_FALHA_CAM;
    return false;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    fora.flags |= Enlace::FLAG_FALHA_CAM;
    return false;
  }

  const uint32_t t0 = millis();
  // JPG_SCALE_2X leva a QVGA de 320x240 para 160x120, que e exatamente a
  // grade que o classificador quer com passo 2. Decodificar ja no
  // tamanho certo evita uma reamostragem depois.
  const bool ok = jpg2rgb565(fb->buf, fb->len, g_rgb, JPG_SCALE_2X);
  esp_camera_fb_return(fb);
  if (!ok) {
    fora.flags |= Enlace::FLAG_FALHA_CAM;
    return false;
  }

  Visao::Veredito v;
  if (!Visao::avalia(g_rgb, RGB_LARG, RGB_ALT, 2, VISAO_BYTE_ALTO_PRIMEIRO ? true : false, g_par,
                     Visao::PESOS_PADRAO, v)) {
    fora.flags |= Enlace::FLAG_FALHA_CAM;
    return false;
  }

  fora.probabilidade  = v.probabilidade;
  fora.cobertura      = v.c.cobertura;
  fora.exgMedio       = v.c.exgMedio;
  fora.maiorRegiaoPct = (uint8_t)(v.c.maiorRegiao / 10);
  fora.clusters       = v.c.clusters;
  fora.ms             = (uint16_t)(millis() - t0);
  fora.classe         = v.classe;
  fora.flags |= v.c.flags;
  g_quadros++;
  return true;
}

// ---------------------------------------------------------------------
//  Enlace
// ---------------------------------------------------------------------
static void envia(uint8_t tipo, const uint8_t* carga, uint8_t n) {
  uint8_t q[Enlace::QUADRO_MAX];
  const size_t t = Enlace::monta(tipo, carga, n, q, sizeof(q));
  if (t) Enl.write(q, t);
}

static void anunciaIp() {
  if (WiFi.status() != WL_CONNECTED) return;
  const String ip = WiFi.localIP().toString();
  envia(Enlace::TIPO_ANUNCIA_IP, (const uint8_t*)ip.c_str(), (uint8_t)ip.length());
}

static void trata(const Enlace::Quadro& q) {
  g_ultimoPing = millis();

  switch (q.tipo) {
    case Enlace::TIPO_PING: {
      Enlace::CargaPong p;
      p.major    = 0;
      p.minor    = 2;
      p.uptimeS  = millis() / 1000UL;
      p.largura  = RGB_LARG;
      p.altura   = RGB_ALT;
      p.temPsram = psramFound() ? 1 : 0;
      p.fps      = 0;
      uint8_t c[Enlace::PONG_BYTES];
      Enlace::serializa(p, c);
      envia(Enlace::TIPO_PONG, c, Enlace::PONG_BYTES);
      anunciaIp();
      break;
    }

    case Enlace::TIPO_PEDE_VEREDITO: {
      Enlace::CargaVeredito v;
      classifica(v);  // em falha, v ja vem com FLAG_FALHA_CAM
      uint8_t c[Enlace::VEREDITO_BYTES];
      Enlace::serializa(v, c);
      envia(Enlace::TIPO_VEREDITO, c, Enlace::VEREDITO_BYTES);
      break;
    }

    case Enlace::TIPO_CONFIG: {
      // O vaso e o dono dos limiares: eles vivem no config.h dele e sao
      // empurrados para ca a cada ping perdido e reencontrado. Assim nao
      // existe o caso de a camera estar decidindo com limiar velho.
      if (q.n >= 6) {
        g_par.pisoExg       = q.carga[0];
        g_par.blocoVerdePct = q.carga[1];
        g_par.limiarPlanta  = Enlace::pega16(q.carga + 2);
        g_par.limiarDuvida  = Enlace::pega16(q.carga + 4);
      }
      break;
    }

    default: break;
  }
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // O LED de flash de 1 W fica desligado, e explicitamente. Ele sozinho
  // puxa mais corrente que a placa inteira e estoura o orcamento de USB
  // do projeto - ver docs/06-energia-usb.md.
  pinMode(CAM_PIN_LED_FLASH, OUTPUT);
  digitalWrite(CAM_PIN_LED_FLASH, LOW);
  pinMode(CAM_PIN_LED_VERMELHO, OUTPUT);
  digitalWrite(CAM_PIN_LED_VERMELHO, HIGH);  // ativo em nivel baixo: apagado

  Enl.begin(ENLACE_BAUD, SERIAL_8N1, CAM_PIN_ENLACE_RX, CAM_PIN_ENLACE_TX);

  g_par               = Visao::Parametros::padrao();
  g_par.pisoExg       = VISAO_PISO_EXG;
  g_par.brilhoMinimo  = VISAO_BRILHO_MINIMO;
  g_par.brilhoMaximo  = VISAO_BRILHO_MAXIMO;
  g_par.blocoVerdePct = VISAO_BLOCO_VERDE_PCT;
  g_par.limiarPlanta  = VISAO_LIMIAR_PLANTA;
  g_par.limiarDuvida  = VISAO_LIMIAR_DUVIDA;

  g_cameraOk = iniciaCamera();
  g_rgb      = (uint8_t*)(psramFound() ? ps_malloc(RGB_BYTES) : malloc(RGB_BYTES));

  Serial.printf("[cam] camera %s | psram %s | buffer %s\n", g_cameraOk ? "ok" : "FALHOU",
                psramFound() ? "sim" : "nao", g_rgb ? "ok" : "FALHOU");

  if (strlen(FARMIO_WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("farmio-cam");
    WiFi.begin(FARMIO_WIFI_SSID, FARMIO_WIFI_PASS);
  }
}

void loop() {
  // ---- Enlace: e a unica obrigacao de tempo do loop -------------------
  while (Enl.available()) g_rec.empurra((uint8_t)Enl.read());
  Enlace::Quadro q;
  while (g_rec.proximo(q)) trata(q);

  // ---- Rede: maquina de estado, nunca laco de espera ------------------
  static bool online    = false;
  static bool videoNoAr = false;
  const bool conectado  = (WiFi.status() == WL_CONNECTED);
  if (conectado && !online) {
    online = true;
    Serial.printf("[cam] http://%s:81/stream\n", WiFi.localIP().toString().c_str());
    if (!videoNoAr) {
      iniciaServidorVideo();
      videoNoAr = true;
    }
    anunciaIp();
  } else if (!conectado && online) {
    online = false;
  }

  // ---- Camera que nao subiu: tenta de novo, sem travar o loop ---------
  static uint32_t proximaTentativa = 0;
  if (!g_cameraOk && (int32_t)(millis() - proximaTentativa) >= 0) {
    proximaTentativa = millis() + 10000;
    g_cameraOk       = iniciaCamera();
  }

  // ---- LED vermelho: aceso quando o vaso esta falando com ela ---------
  const bool enlaceVivo = g_ultimoPing && (millis() - g_ultimoPing) < 30000;
  digitalWrite(CAM_PIN_LED_VERMELHO, enlaceVivo ? LOW : HIGH);

  delay(2);  // devolve a CPU para a tarefa do video
}
