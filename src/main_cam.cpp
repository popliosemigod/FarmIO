// =====================================================================
//  FarmIO - main_cam.cpp
//  Firmware da ESP32-CAM (AI-Thinker). Tudo pelo fio, nada pelo radio:
//
//    veredito  "tem planta na frente?" - doze bytes, a cada 10 s
//    foto      um JPEG inteiro, so quando o app pede
//
//  POR QUE A CAMERA NAO TEM MAIS WI-FI. Ate a versao 0.2 ela servia
//  video MJPEG direto para o navegador. Em campo aberto isso deixou de
//  fazer sentido, por tres razoes:
//
//    1. O unico acesso e o roteador do celular. Cada placa a mais na
//       rede e mais uma que precisa acha-lo, pegar IP e sobreviver as
//       quedas dele - e o celular teria de alcancar as duas.
//    2. Video continuo mantem sensor e radio acesos o tempo todo, e o
//       projeto vive numa porta USB.
//    3. Ninguem assiste um vaso. O que se quer e uma foto na hora de
//       conferir - e uma foto cabe no fio.
//
//  Sem radio, a camera tambem nao precisa de credencial nenhuma: o
//  secrets.h deixou de ser assunto dela.
//
//  POR QUE VGA E ESCALA 1/4. O classificador quer 160x120. Com PSRAM, o
//  sensor captura VGA (640x480), que e o tamanho bom para a foto, e o
//  decodificador JPEG entrega 1/4 disso direto: exatamente 160x120. Um
//  tamanho so de quadro serve os dois usos, sem trocar a configuracao do
//  sensor no meio do caminho.
//
//  Gravar:  pio run -e cam -t upload
//  (adaptador USB-TTL no header de gravacao - GPIO1/GPIO3 -, GPIO0 no
//   GND durante o reset. O enlace usa GPIO14/15 e nao precisa sair.)
// =====================================================================
#include <Arduino.h>
#include "esp_camera.h"
#include "img_converters.h"

#include "config.h"
#include "farmio_enlace.h"
#include "farmio_visao.h"

static HardwareSerial Enl(1);
static Enlace::Receptor g_rec;

static bool g_cameraOk       = false;
static uint8_t* g_rgb        = nullptr;  // 160x120 RGB565 para o classificador
static uint32_t g_ultimoPing = 0;
static uint32_t g_quadros    = 0;
static uint32_t g_fotos      = 0;
static Visao::Parametros g_par;
static jpg_scale_t g_escala = JPG_SCALE_4X;
static uint16_t g_largura   = 640;
static uint16_t g_altura    = 480;

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
  c.grab_mode    = CAMERA_GRAB_LATEST;

  // Qualidade 14 (10..63, menor = melhor e maior): uma VGA fica em 20 a
  // 30 kB. E o numero que decide quanto a foto demora no fio - cada 11 kB
  // e um segundo a 115200 bps.
  c.jpeg_quality = 14;

  if (psramFound()) {
    c.frame_size  = FRAMESIZE_VGA;  // 640x480
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.fb_count    = 2;
    g_escala      = JPG_SCALE_4X;  // 640x480 / 4 = 160x120
    g_largura     = 640;
    g_altura      = 480;
  } else {
    // Sem PSRAM nao cabe VGA. QVGA com escala 1/2 tambem da 160x120.
    // Ate a v0.2 este ramo usava QQVGA com escala 1/2, o que dava 80x60
    // num buffer que o classificador le como 160x120 - nunca mordeu
    // porque a AI-Thinker tem PSRAM, mas era leitura fora do quadro.
    c.frame_size  = FRAMESIZE_QVGA;  // 320x240
    c.fb_location = CAMERA_FB_IN_DRAM;
    c.fb_count    = 1;
    g_escala      = JPG_SCALE_2X;  // 320x240 / 2 = 160x120
    g_largura     = 320;
    g_altura      = 240;
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
//  Enlace
// ---------------------------------------------------------------------
static void envia(uint8_t tipo, const uint8_t* carga, uint8_t n) {
  uint8_t q[Enlace::QUADRO_MAX];
  const size_t t = Enlace::monta(tipo, carga, n, q, sizeof(q));
  if (t) Enl.write(q, t);
}

static void enviaErroFoto(uint8_t codigo) {
  envia(Enlace::TIPO_FOTO_ERRO, &codigo, 1);
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
  // Decodificar ja no tamanho que o classificador quer evita uma
  // reamostragem depois - ver POR QUE VGA E ESCALA 1/4, no topo.
  const bool ok = jpg2rgb565(fb->buf, fb->len, g_rgb, g_escala);
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
//  Foto: um JPEG inteiro, em pedacos, pelo fio
//
//  Esta funcao bloqueia o loop da camera por 2 a 3 s, e isso e de
//  proposito: a camera nao tem mais nada a fazer enquanto transmite, e o
//  vaso sabe que ela esta ocupada - ele mesmo pediu, e para de perguntar
//  ate a foto terminar.
// ---------------------------------------------------------------------
static void enviaFoto() {
  if (!g_cameraOk) {
    enviaErroFoto(Enlace::FOTO_ERRO_SEM_CAMERA);
    return;
  }

  // O primeiro quadro da fila pode ser velho: com dois buffers, o driver
  // entrega o ultimo quadro COMPLETO, que pode ter sido capturado antes
  // de alguem mexer na cena. Um descartado custa ~70 ms e garante que a
  // foto e do instante do pedido.
  camera_fb_t* velho = esp_camera_fb_get();
  if (velho) esp_camera_fb_return(velho);

  const uint32_t t0 = millis();
  camera_fb_t* fb   = esp_camera_fb_get();
  if (!fb) {
    enviaErroFoto(Enlace::FOTO_ERRO_CAPTURA);
    return;
  }
  if (fb->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(fb);
    enviaErroFoto(Enlace::FOTO_ERRO_FORMATO);
    return;
  }
  const uint16_t msCaptura = (uint16_t)(millis() - t0);

  uint8_t c[Enlace::CARGA_MAX];
  Enlace::poe32(c, (uint32_t)fb->len);
  Enlace::poe16(c + 4, (uint16_t)fb->width);
  Enlace::poe16(c + 6, (uint16_t)fb->height);
  Enlace::poe16(c + 8, msCaptura);
  envia(Enlace::TIPO_FOTO_INICIO, c, Enlace::FOTO_INICIO_BYTES);

  for (uint32_t desloc = 0; desloc < fb->len; desloc += Enlace::FOTO_PEDACO_MAX) {
    uint32_t n = fb->len - desloc;
    if (n > Enlace::FOTO_PEDACO_MAX) n = Enlace::FOTO_PEDACO_MAX;
    Enlace::poe32(c, desloc);
    memcpy(c + 4, fb->buf + desloc, n);
    envia(Enlace::TIPO_FOTO_PEDACO, c, (uint8_t)(4 + n));
  }

  Enlace::poe32(c, (uint32_t)fb->len);
  Enlace::poe16(c + 4, Enlace::crc16(fb->buf, fb->len));
  envia(Enlace::TIPO_FOTO_FIM, c, Enlace::FOTO_FIM_BYTES);

  Serial.printf("[cam] foto %lu: %ux%u, %u B, captura em %u ms\n", (unsigned long)++g_fotos,
                fb->width, fb->height, (unsigned)fb->len, msCaptura);
  esp_camera_fb_return(fb);
}

static void trata(const Enlace::Quadro& q) {
  g_ultimoPing = millis();

  switch (q.tipo) {
    case Enlace::TIPO_PING: {
      Enlace::CargaPong p;
      p.major    = 0;
      p.minor    = 3;
      p.uptimeS  = millis() / 1000UL;
      p.largura  = g_largura;
      p.altura   = g_altura;
      p.temPsram = psramFound() ? 1 : 0;
      p.fps      = 0;
      uint8_t c[Enlace::PONG_BYTES];
      Enlace::serializa(p, c);
      envia(Enlace::TIPO_PONG, c, Enlace::PONG_BYTES);
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

    case Enlace::TIPO_PEDE_FOTO: enviaFoto(); break;

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

  // A camera so recebe pedidos curtos, entao a fila de recepcao padrao
  // basta. A de TRANSMISSAO e que importa, e o write() bloqueia quando
  // ela enche - o que, durante uma foto, e exatamente o comportamento
  // certo: a camera anda no ritmo do fio.
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

  Serial.printf("[cam] camera %s | psram %s | %ux%u | buffer %s | sem radio\n",
                g_cameraOk ? "ok" : "FALHOU", psramFound() ? "sim" : "nao", g_largura, g_altura,
                g_rgb ? "ok" : "FALHOU");
}

void loop() {
  // ---- Enlace: e a unica obrigacao de tempo do loop -------------------
  while (Enl.available()) g_rec.empurra((uint8_t)Enl.read());
  Enlace::Quadro q;
  while (g_rec.proximo(q)) trata(q);

  // ---- Camera que nao subiu: tenta de novo, sem travar o loop ---------
  static uint32_t proximaTentativa = 0;
  if (!g_cameraOk && (int32_t)(millis() - proximaTentativa) >= 0) {
    proximaTentativa = millis() + 10000;
    g_cameraOk       = iniciaCamera();
  }

  // ---- LED vermelho: aceso quando o vaso esta falando com ela ---------
  const bool enlaceVivo = g_ultimoPing && (millis() - g_ultimoPing) < 30000;
  digitalWrite(CAM_PIN_LED_VERMELHO, enlaceVivo ? LOW : HIGH);

  delay(1);  // devolve a CPU para a tarefa ociosa, que alimenta o watchdog
}
