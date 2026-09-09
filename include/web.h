// =====================================================================
//  FarmIO - web.h
//  Servidor HTTP na ESP32 tradicional. Duas rotas que importam:
//
//    /            pagina unica com os valores e o video ao vivo
//    /sensores    JSON com a leitura atual, para qualquer cliente
//
//  A arquitetura vem da especificacao do SmartFarm e a razao dela e boa:
//  a ESP32-CAM sozinha nao aguenta servir pagina, sensores e streaming.
//  Entao a CAM faz SO o streaming, no endereco nativo dela
//  (http://<ip-da-cam>:81/stream), e esta ESP32 hospeda a pagina que
//  embute esse fluxo num <img>. O navegador do cliente monta as duas
//  coisas; nenhuma das placas fica sobrecarregada.
//
//  A pagina e servida da PROGMEM, sem CDN: a rede da bancada nem sempre
//  tem uplink, e uma pagina que depende de internet para carregar CSS
//  falha justamente no dia do ensaio.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "sensores.h"

namespace Web {

inline WebServer& servidor() {
  static WebServer instancia(80);
  return instancia;
}

// IP da ESP32-CAM. Configuravel em tempo de execucao pela rota /cam,
// porque o DHCP pode mudar o endereco dela entre um ensaio e outro.
inline String& ipDaCamera() {
  static String ip = "";
  return ip;
}

inline size_t jsonSensores(char* buf, size_t len) {
  const char* faixa = SOLO_NOME[L.soloFaixa <= SOLO_INVALIDO ? L.soloFaixa : SOLO_INVALIDO];
  char temp[12], umid[12];
  if (L.dhtOk && !isnan(L.temperaturaC)) {
    snprintf(temp, sizeof(temp), "%.1f", L.temperaturaC);
    snprintf(umid, sizeof(umid), "%.1f", L.umidadeArPct);
  } else {
    strcpy(temp, "null");
    strcpy(umid, "null");
  }

  return (size_t)snprintf(buf, len,
                          "{\"no\":\"%s\",\"fw\":\"%s\",\"uptime_s\":%lu,"
                          "\"temperatura_c\":%s,\"umidade_ar_pct\":%s,"
                          "\"solo_adc\":%u,\"solo_faixa\":\"%s\",\"solo_nivel\":%u,"
                          "\"tanque_pct\":%u,\"tanque_adc\":%u,"
                          "\"bomba_ligada\":%s,\"bomba_bloqueio\":\"%s\","
                          "\"bomba_pulsos\":%u,\"bomba_total_s\":%lu,"
                          "\"riscos\":%u,\"rssi\":%d,\"heap\":%lu}",
                          FARMIO_NOME, FARMIO_VERSAO, (unsigned long)(millis() / 1000UL), temp,
                          umid, L.soloAdc, faixa, L.soloFaixa, L.tanquePct, L.nivelAdc,
                          B.ligada ? "true" : "false", B.bloqueioAtual ? B.bloqueioAtual : "",
                          B.pulsos, (unsigned long)(B.tempoTotalMs / 1000UL), riscosAtivos,
                          WiFi.RSSI(), (unsigned long)ESP.getFreeHeap());
}

static const char PAGINA[] PROGMEM = R"HTML(<!doctype html><html lang=pt-BR><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>FarmIO</title><style>
:root{--verde:#3ddc84;--terra:#6b4f3a;--fundo:#10150f;--carta:#1a211a;--txt:#e8f0e4;--risco:#ff5252}
*{box-sizing:border-box}body{margin:0;background:var(--fundo);color:var(--txt);
font-family:ui-rounded,'Segoe UI',system-ui,sans-serif;padding:16px;max-width:560px;margin:0 auto}
h1{font-size:22px;margin:4px 0 2px;letter-spacing:.5px}h1 span{color:var(--verde)}
.sub{color:#8fa088;font-size:12px;margin-bottom:16px}
.grade{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.carta{background:var(--carta);border-radius:14px;padding:14px;border:1px solid #263026}
.rot{font-size:11px;color:#8fa088;text-transform:uppercase;letter-spacing:1px}
.val{font-size:26px;font-weight:600;margin-top:4px}.un{font-size:13px;color:#8fa088}
.larga{grid-column:1/-1}
.barra{height:10px;background:#0d120c;border-radius:6px;overflow:hidden;margin-top:8px}
.barra i{display:block;height:100%;background:linear-gradient(90deg,#2b7,#3ddc84);width:0}
.risco{background:var(--risco);color:#2a0000;font-weight:700;text-align:center;
padding:10px;border-radius:12px;margin-bottom:12px;display:none}
img.cam{width:100%;border-radius:14px;margin-top:12px;background:#000;min-height:120px}
.pe{color:#8fa088;font-size:11px;text-align:center;margin-top:16px}
.on{color:var(--verde)}
</style>
<h1>Farm<span>IO</span></h1><div class=sub id=sub>carregando...</div>
<div class=risco id=risco></div>
<div class=grade>
<div class=carta><div class=rot>Temperatura</div><div class=val><span id=t>--</span><span class=un> C</span></div></div>
<div class=carta><div class=rot>Umidade do ar</div><div class=val><span id=h>--</span><span class=un> %</span></div></div>
<div class="carta larga"><div class=rot>Umidade do solo</div><div class=val id=s>--</div></div>
<div class="carta larga"><div class=rot>Tanque</div><div class=val><span id=n>--</span><span class=un> %</span></div>
<div class=barra><i id=bar></i></div></div>
<div class="carta larga"><div class=rot>Irrigacao</div><div class=val id=b>--</div></div>
</div>
<img class=cam id=cam alt="video da ESP32-CAM">
<div class=pe id=pe></div>
<script>
const $=i=>document.getElementById(i);
async function tick(){
 try{
  const r=await fetch('/sensores'),d=await r.json();
  $('t').textContent=d.temperatura_c??'--';
  $('h').textContent=d.umidade_ar_pct??'--';
  $('s').textContent=d.solo_faixa;
  $('n').textContent=d.tanque_pct;
  $('bar').style.width=d.tanque_pct+'%';
  $('b').innerHTML=d.bomba_ligada?'<span class=on>irrigando</span>':(d.bomba_bloqueio||'parada');
  $('sub').textContent=d.no+' · '+d.fw+' · '+d.rssi+' dBm · '+d.uptime_s+' s no ar';
  $('pe').textContent=d.bomba_pulsos+' pulsos · '+d.bomba_total_s+' s de bomba desde o boot';
  const rs=$('risco');
  if(d.riscos){rs.style.display='block';rs.textContent=nomeRisco(d.riscos);}
  else rs.style.display='none';
 }catch(e){$('sub').textContent='sem resposta do no';}
}
function nomeRisco(m){
 const n=[];
 if(m&1)n.push('TEMPERATURA ALTA');
 if(m&2)n.push('SOLO MUITO SECO');
 if(m&4)n.push('TANQUE VAZIO');
 if(m&8)n.push('SOLO ENCHARCADO');
 if(m&16)n.push('SENSOR SEM RESPOSTA');
 return n.join(' · ');
}
fetch('/cam').then(r=>r.text()).then(ip=>{if(ip)$('cam').src='http://'+ip+':81/stream';});
tick();setInterval(tick,2000);
</script></html>)HTML";

inline void begin() {
  WebServer& s = servidor();

  s.on("/", HTTP_GET, []() { servidor().send_P(200, "text/html; charset=utf-8", PAGINA); });

  s.on("/sensores", HTTP_GET, []() {
    char buf[640];
    jsonSensores(buf, sizeof(buf));
    servidor().sendHeader("Access-Control-Allow-Origin", "*");
    servidor().send(200, "application/json", buf);
  });

  // GET devolve o IP da camera; POST/GET com ?ip= grava.
  s.on("/cam", HTTP_ANY, []() {
    if (servidor().hasArg("ip")) ipDaCamera() = servidor().arg("ip");
    servidor().send(200, "text/plain", ipDaCamera());
  });

  s.onNotFound([]() { servidor().send(404, "text/plain", "nao existe"); });
  s.begin();
}

inline void tick() {
  servidor().handleClient();
}

}  // namespace Web
