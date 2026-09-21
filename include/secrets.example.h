// =====================================================================
//  secrets.example.h - MODELO. Copie para secrets.h e preencha.
//
//      Copy-Item include\secrets.example.h include\secrets.h
//
//  include/secrets.h esta no .gitignore e NUNCA vai para o repositorio.
//  Este arquivo, sim - e por isso ele so contem valor neutro.
//
//  Sem secrets.h o firmware compila e roda: sobe o proprio ponto de
//  acesso e espera configuracao. E o que permite o CI compilar sem
//  nenhuma senha.
// =====================================================================
#pragma once

// ---- Roteador ------------------------------------------------------
//  Em campo aberto a unica rede e o roteador do celular. Tres ajustes no
//  celular decidem se as placas o enxergam:
//
//    BANDA 2,4 GHz. O radio do ESP32 e 2,4 GHz APENAS. Um roteador em
//      5 GHz devolve "rede nao encontrada", nao erro de senha - e o
//      diagnostico se perde procurando no lugar errado. Nos Samsung:
//      Roteador Wi-Fi > Configurar > Banda.
//    SEGURANCA WPA2. WPA3-somente falha em parte dos cores do ESP32.
//      WPA2 ou WPA2/WPA3 funcionam.
//    SENHA COM 8+ CARACTERES. Abaixo disso o WPA2 recusa.
#define FARMIO_WIFI_SSID "REDE_2G_AQUI"
#define FARMIO_WIFI_PASS "SENHA_AQUI"

// ---- Rede propria do vaso ------------------------------------------
//  O vaso sempre levanta a rede 'farmio-01', com ou sem roteador. Desde
//  que o app ganhou o botao da bomba, esta senha protege um atuador:
//  quem entra na rede consegue ligar a bomba. O valor abaixo e PUBLICO
//  - esta num repositorio aberto - e serve so para compilar. Troque no
//  seu secrets.h.
#define FARMIO_AP_PASS "farmio123"
