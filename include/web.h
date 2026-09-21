// =====================================================================
//  FarmIO - web.h
//  O app do vaso: um servidor HTTP no ESP32-C3 e uma pagina so.
//
//    GET  /              a pagina
//    GET  /sensores      JSON com o estado inteiro, para qualquer cliente
//    POST /foto          pede uma foto a camera (volta na hora)
//    GET  /foto/estado   acompanha a foto enquanto ela chega pelo fio
//    GET  /foto.jpg      a ultima foto pronta
//    POST /bomba?acao=   ligar | manter | desligar
//
//  O CELULAR SO CONVERSA COM O VASO. Ate a v0.2 a pagina embutia o video
//  direto da camera, o que obrigava o celular a alcancar duas placas. Em
//  campo aberto, com o roteador do celular como unica rede, isso era uma
//  placa a mais para dar errado. Agora a foto vem pela UART e o vaso a
//  serve daqui: um endereco, uma placa na rede.
//
//  FOTO EM TRES PASSOS, E NAO NUMA REQUISICAO SO. Uma foto leva de 2 a
//  3 s no fio. Um GET que esperasse por ela seguraria o loop do vaso -
//  solo, bomba e intertravamentos - esse tempo todo. Entao o pedido volta
//  na hora, a pagina pergunta o progresso, e o JPEG so e servido depois
//  de inteiro na memoria.
//
//  ACOES SAO POST. Um GET que liga bomba seria acionado por qualquer
//  coisa que pre-carregue links - navegador, previa de mensagem, robo.
//
//  A pagina e servida da PROGMEM, sem CDN: em campo nao ha internet, e
//  uma pagina que depende dela para carregar CSS falha justo la.
// =====================================================================
#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "anel.h"
#include "bomba.h"
#include "camera.h"
#include "config.h"
#include "energia.h"
#include "farmio_visao.h"
#include "sensores.h"

namespace Web {

// Um lugar so para o tamanho do JSON: a pagina e a serial usam o mesmo.
static const size_t JSON_MAX = 1400;

inline WebServer& servidor() {
  static WebServer instancia(80);
  return instancia;
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
  const Camera::Foto& f = Camera::foto();

  return (size_t)snprintf(
      buf, len,
      "{\"no\":\"%s\",\"fw\":\"%s\",\"uptime_s\":%lu,"
      "\"temperatura_c\":%s,\"umidade_ar_pct\":%s,"
      "\"solo_adc\":%u,\"solo_faixa\":\"%s\",\"solo_nivel\":%u,"
      "\"tanque_pct\":%u,\"tanque_adc\":%u,\"tanque_valido\":%s,"
      "\"bomba_ligada\":%s,\"bomba_bloqueio\":\"%s\","
      "\"bomba_pulsos\":%u,\"bomba_total_s\":%lu,"
      "\"bomba_manual\":%s,\"bomba_manual_restante_s\":%u,"
      "\"bomba_manual_total_s\":%lu,\"bomba_manual_acionamentos\":%u,"
      "\"riscos\":%u,\"rssi\":%d,\"heap\":%lu,"
      "\"cam_enlace\":%s,\"cam_quadros\":%u,"
      "\"cam_falhas\":%u,\"cam_resets\":%u,\"cam_ms\":%u,"
      "\"cam_bytes\":%lu,\"cam_eco\":%lu,\"cam_crc\":%lu,"
      "\"planta\":%s,\"planta_prob\":%u,\"planta_media\":%u,"
      "\"planta_classe\":%u,\"planta_cobertura\":%u,\"planta_flags\":%u,"
      "\"planta_calibrada\":%s,"
      "\"foto_estado\":\"%s\",\"foto_numero\":%lu,\"foto_reenvios\":%u,"
      "\"energia_teto_ma\":%u,\"energia_ma\":%u,\"anel_brilho\":%u}",
      FARMIO_NOME, FARMIO_VERSAO, (unsigned long)(millis() / 1000UL), temp, umid, L.soloAdc, faixa,
      L.soloFaixa, L.tanquePct, L.nivelAdc, L.nivelValido ? "true" : "false",
      B.ligada ? "true" : "false", B.bloqueioAtual ? B.bloqueioAtual : "", B.pulsos,
      (unsigned long)(B.tempoTotalMs / 1000UL), B.manual ? "true" : "false",
      Bomba::manualRestanteS(), (unsigned long)(B.manualTotalMs / 1000UL), B.manualAcionamentos,
      riscosAtivos, WiFi.RSSI(), (unsigned long)ESP.getFreeHeap(), V.enlaceOk ? "true" : "false",
      V.quadros, V.falhas, V.resets, V.msCamera, (unsigned long)Camera::fio().bytes,
      (unsigned long)Camera::fio().ecos, (unsigned long)Camera::receptor().contadores().crcErrado,
      V.temPlanta ? "true" : "false", V.probabilidade, V.mediaFiltrada, V.classe, V.cobertura,
      V.flags, VISAO_CALIBRADA ? "true" : "false", Camera::nomeEstadoFoto(f.estado),
      (unsigned long)f.numero, f.reenvios, Energia::teto(),
      Energia::estimativaMa(V.enlaceOk, Anel::brilhoAtual(), B.ligada), Anel::brilhoAtual());
}

inline size_t jsonFoto(char* buf, size_t len) {
  const Camera::Foto& f = Camera::foto();
  const uint32_t ms     = f.estado == Camera::FOTO_PRONTA ? f.duracaoMs
                          : Camera::fotoEmCurso()         ? (uint32_t)(millis() - f.pedidaEm)
                                                          : 0;
  return (size_t)snprintf(buf, len,
                          "{\"estado\":\"%s\",\"recebido\":%lu,\"total\":%lu,\"largura\":%u,"
                          "\"altura\":%u,\"ms\":%lu,\"numero\":%lu,\"erro\":\"%s\"}",
                          Camera::nomeEstadoFoto(f.estado), (unsigned long)f.recebido,
                          (unsigned long)f.total, f.largura, f.altura, (unsigned long)ms,
                          (unsigned long)f.numero, f.erro ? f.erro : "");
}

inline size_t jsonBomba(char* buf, size_t len, const char* erro) {
  return (size_t)snprintf(buf, len,
                          "{\"ok\":%s,\"ligada\":%s,\"manual\":%s,\"restante_s\":%u,"
                          "\"erro\":\"%s\"}",
                          erro ? "false" : "true", B.ligada ? "true" : "false",
                          B.manual ? "true" : "false", Bomba::manualRestanteS(), erro ? erro : "");
}

static const char PAGINA[] PROGMEM = R"HTML(<!doctype html><html lang=pt-BR><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>FarmIO</title><style>
:root{--verde:#3ddc84;--terra:#6b4f3a;--fundo:#10150f;--carta:#1a211a;--txt:#e8f0e4;--risco:#ff5252}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--fundo);color:var(--txt);
font-family:ui-rounded,'Segoe UI',system-ui,sans-serif;padding:16px;max-width:560px;margin:0 auto}
h1{font-size:22px;margin:4px 0 2px;letter-spacing:.5px}h1 span{color:var(--verde)}
.sub{color:#8fa088;font-size:12px;margin-bottom:16px}
.grade{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.carta{background:var(--carta);border-radius:14px;padding:14px;border:1px solid #263026}
.rot{font-size:11px;color:#8fa088;text-transform:uppercase;letter-spacing:1px}
.val{font-size:26px;font-weight:600;margin-top:4px}.un{font-size:13px;color:#8fa088}
.nota{font-size:13px;color:#b9c7b3;margin-top:6px;min-height:1em}
.larga{grid-column:1/-1}
.barra{height:10px;background:#0d120c;border-radius:6px;overflow:hidden;margin-top:8px}
.barra i{display:block;height:100%;background:linear-gradient(90deg,#2b7,#3ddc84);width:0}
.risco{background:var(--risco);color:#2a0000;font-weight:700;text-align:center;
padding:10px;border-radius:12px;margin-bottom:12px;display:none}
.bt{margin-top:12px;width:100%;padding:13px;border-radius:10px;border:1px solid #2f6a41;
background:#173a22;color:var(--txt);font:inherit;font-size:15px;font-weight:600;cursor:pointer}
.bt.parar{background:#4a1a1a;border-color:#8a2e2e}
.bt:disabled{opacity:.5}
img.foto{width:100%;border-radius:10px;margin-top:10px;display:block;background:#000}
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
<div class="carta larga"><div class=rot>Irrigacao</div><div class=val id=b>--</div>
<div class=nota id=bm></div>
<button class=bt id=btnb>Ligar bomba</button></div>
<div class="carta larga"><div class=rot>Camera</div><div class=val id=p>--</div>
<div class=barra><i id=pb></i></div><div class=rot id=pd></div>
<div class=nota id=pcal hidden>deteccao de planta ainda nao calibrada com foto real - confira pela foto</div>
<img class=foto id=foto alt="foto da camera do vaso" hidden>
<div class=barra id=fbar hidden><i id=fb></i></div>
<div class=nota id=fi>nenhuma foto ainda</div>
<button class=bt id=btnf>Tirar foto</button></div>
<div class="carta larga"><div class=rot>Energia</div><div class=val><span id=e>--</span><span class=un> mA estimados</span></div>
<div class=barra><i id=eb></i></div><div class=rot id=ed></div></div>
</div>
<div class=pe id=pe></div>
<script>
const $=i=>document.getElementById(i);
const post=u=>fetch(u,{method:'POST'}).then(r=>r.json());

// ---- Bomba -------------------------------------------------------------
// 'meu' diz se foi ESTA pagina que ligou. So quem ligou renova o prazo:
// se a aba fechar ou o roteador do celular cair, a renovacao para e a
// bomba desliga sozinha em 6 s no vaso. Uma pagina aberta em outro
// celular pode desligar, mas nao mantem ligada o que nao ligou.
let meu=false,renova=null,manualNoVaso=false;
function paraDeRenovar(){meu=false;if(renova){clearInterval(renova);renova=null;}pintaBomba();}
function pintaBomba(){
 const ligada=meu||manualNoVaso,b=$('btnb');
 b.textContent=ligada?'Desligar bomba':'Ligar bomba';b.classList.toggle('parar',ligada);
}
$('btnb').onclick=async()=>{
 const b=$('btnb');b.disabled=true;
 try{
  if(meu||manualNoVaso){await post('/bomba?acao=desligar');manualNoVaso=false;paraDeRenovar();$('bm').textContent='';}
  else{
   const d=await post('/bomba?acao=ligar');
   if(d.ok){
    meu=true;manualNoVaso=true;
    renova=setInterval(()=>post('/bomba?acao=manter').then(x=>{if(!x.manual)paraDeRenovar();}).catch(()=>{}),2000);
   }else $('bm').textContent='nao liguei: '+d.erro;
  }
 }catch(e){$('bm').textContent='o vaso nao respondeu';}
 b.disabled=false;pintaBomba();
};

// ---- Foto --------------------------------------------------------------
function mostraFoto(d){
 const im=$('foto');im.src='/foto.jpg?n='+d.numero;im.hidden=false;
 $('fi').textContent=d.largura+'x'+d.altura+' · '+(d.total/1024).toFixed(1)+' kB · '+
  (d.ms/1000).toFixed(1)+' s pelo fio';
}
async function acompanhaFoto(){
 try{
  const d=await (await fetch('/foto/estado')).json();
  if(d.estado==='pedida'||d.estado==='recebendo'){
   $('fbar').hidden=false;
   $('fb').style.width=(d.total?Math.round(d.recebido*100/d.total):0)+'%';
   $('fi').textContent=d.estado==='pedida'?'a camera esta capturando...':
    'recebendo '+(d.recebido/1024).toFixed(1)+' de '+(d.total/1024).toFixed(1)+' kB';
   setTimeout(acompanhaFoto,300);return;
  }
  $('fbar').hidden=true;$('btnf').disabled=false;
  if(d.estado==='pronta')mostraFoto(d);
  else if(d.estado==='erro')$('fi').textContent='a foto falhou: '+d.erro;
 }catch(e){setTimeout(acompanhaFoto,800);}
}
$('btnf').onclick=async()=>{
 const b=$('btnf');b.disabled=true;$('fi').textContent='pedindo a foto...';
 try{
  const d=await post('/foto');
  if(!d.ok){$('fi').textContent='nao tirei: '+d.erro;b.disabled=false;return;}
  acompanhaFoto();
 }catch(e){$('fi').textContent='o vaso nao respondeu';b.disabled=false;}
};

// ---- Leitura periodica -------------------------------------------------
async function tick(){
 try{
  const r=await fetch('/sensores'),d=await r.json();
  $('t').textContent=d.temperatura_c??'--';
  $('h').textContent=d.umidade_ar_pct??'--';
  $('s').textContent=d.solo_faixa;
  $('n').textContent=d.tanque_valido?d.tanque_pct:'--';
  $('bar').style.width=(d.tanque_valido?d.tanque_pct:0)+'%';
  $('b').innerHTML=d.bomba_ligada?'<span class=on>irrigando</span>':(d.bomba_bloqueio||'parada');
  manualNoVaso=d.bomba_manual;
  if(d.bomba_manual)$('bm').textContent='ligada pelo app · desliga sozinha em '+d.bomba_manual_restante_s+' s';
  else if(meu){paraDeRenovar();$('bm').textContent='';}
  pintaBomba();
  $('sub').textContent=d.no+' · '+d.fw+' · '+d.rssi+' dBm · '+d.uptime_s+' s no ar';
  $('pe').textContent=d.bomba_pulsos+' pulsos automaticos · '+d.bomba_total_s+' s · '+
   d.bomba_manual_acionamentos+' pelo app · '+d.bomba_manual_total_s+' s';
  const rs=$('risco');
  if(d.riscos){rs.style.display='block';rs.textContent=nomeRisco(d.riscos);}
  else rs.style.display='none';

  // Camera. A pagina distingue os tres casos que importam: fio caido,
  // camera viva sem planta, camera viva com planta. Mostrar so
  // "sem planta" nos tres seria mentir em dois deles.
  const cl=['sem planta','provavel','planta'];
  if(!d.cam_enlace){$('p').textContent='sem enlace';$('pb').style.width='0';}
  else{
   $('p').innerHTML=(d.planta?'<span class=on>planta a vista</span>':cl[d.planta_classe]||'--');
   $('pb').style.width=(d.planta_media/10)+'%';
  }
  $('pcal').hidden=d.planta_calibrada;
  $('pd').textContent=(d.planta_prob/10).toFixed(0)+'% neste quadro · '+
   (d.planta_cobertura/10).toFixed(0)+'% de verde · '+d.cam_quadros+' quadros · '+
   d.cam_falhas+' falhas · '+d.cam_resets+' resets'+
   ((d.planta_flags&1)?' · LUZ BAIXA':'');

  // Sem enlace, a linha de detalhe vira diagnostico do fio. Em campo nao ha
  // monitor serial: e esta linha que diz se o defeito e o curto TX-RX, a
  // camera sem energia ou um fio ruim - tres maos diferentes na bancada.
  if(!d.cam_enlace){
   $('pd').textContent=d.cam_eco>0?'fio em curto: o vaso ouve o proprio sinal (TX e RX ligados um no outro)'
    :d.cam_bytes==0?'nada chega pelo fio: camera sem energia, ou D0 fora do GPIO20'
    :d.cam_crc>0?'chega sinal, mas com erro: fio ruim':'a camera nao responde';
  }

  // Energia: a barra e o quanto do orcamento da porta ja esta gasto.
  $('e').textContent=d.energia_ma;
  $('eb').style.width=Math.min(100,d.energia_ma*100/d.energia_teto_ma)+'%';
  $('ed').textContent='teto '+d.energia_teto_ma+' mA · anel em '+d.anel_brilho+'/255';
 }catch(e){$('sub').textContent='sem resposta do vaso';}
}
function nomeRisco(m){
 const n=[];
 if(m&1)n.push('TEMPERATURA ALTA');
 if(m&2)n.push('SOLO MUITO SECO');
 if(m&4)n.push('TANQUE VAZIO');
 if(m&8)n.push('SOLO ENCHARCADO');
 if(m&16)n.push('SENSOR SEM RESPOSTA');
 if(m&32)n.push('CAMERA SEM RESPOSTA');
 if(m&64)n.push('NENHUMA PLANTA A VISTA');
 return n.join(' · ');
}
// Uma foto tirada antes de a pagina abrir continua valendo: mostra.
fetch('/foto/estado').then(r=>r.json()).then(d=>{if(d.estado==='pronta')mostraFoto(d);}).catch(()=>{});
tick();setInterval(tick,2000);
</script></html>)HTML";

inline void begin() {
  WebServer& s = servidor();

  s.on("/", HTTP_GET, []() { servidor().send_P(200, "text/html; charset=utf-8", PAGINA); });

  s.on("/sensores", HTTP_GET, []() {
    char buf[JSON_MAX];
    jsonSensores(buf, sizeof(buf));
    servidor().sendHeader("Access-Control-Allow-Origin", "*");
    servidor().send(200, "application/json", buf);
  });

  // ---- Foto -----------------------------------------------------------
  s.on("/foto", HTTP_POST, []() {
    const char* erro = Camera::pedeFoto(B.ligada);
    char buf[200];
    if (erro) {
      snprintf(buf, sizeof(buf), "{\"ok\":false,\"erro\":\"%s\"}", erro);
      servidor().send(409, "application/json", buf);
    } else {
      servidor().send(202, "application/json", "{\"ok\":true}");
    }
  });

  s.on("/foto/estado", HTTP_GET, []() {
    char buf[300];
    jsonFoto(buf, sizeof(buf));
    servidor().sendHeader("Cache-Control", "no-store");
    servidor().send(200, "application/json", buf);
  });

  // Binario direto do buffer: sem copiar 25 kB para uma String, que
  // dobraria o pico de memoria justo quando a foto acabou de chegar.
  s.on("/foto.jpg", HTTP_GET, []() {
    const Camera::Foto& f = Camera::foto();
    WebServer& sv         = servidor();
    if (f.estado != Camera::FOTO_PRONTA || !f.buf) {
      sv.send(404, "text/plain", "nenhuma foto pronta");
      return;
    }
    sv.sendHeader("Cache-Control", "no-store");
    sv.setContentLength(f.total);
    sv.send(200, "image/jpeg", "");
    sv.client().write(f.buf, f.total);
  });

  // ---- Bomba ----------------------------------------------------------
  s.on("/bomba", HTTP_POST, []() {
    const String acao = servidor().arg("acao");
    const char* erro  = nullptr;
    if (acao == "ligar") {
      erro = Bomba::ligaManual();
    } else if (acao == "manter") {
      if (!Bomba::renovaManual()) erro = "a bomba nao esta ligada pelo app";
    } else if (acao == "desligar") {
      Bomba::desligaManual("manual: desligada pelo app");
    } else {
      erro = "acao desconhecida - use ligar, manter ou desligar";
    }
    char buf[200];
    jsonBomba(buf, sizeof(buf), erro);
    servidor().send(erro ? 409 : 200, "application/json", buf);
  });

  s.onNotFound([]() { servidor().send(404, "text/plain", "nao existe"); });
  s.begin();
}

inline void tick() {
  servidor().handleClient();
}

}  // namespace Web
