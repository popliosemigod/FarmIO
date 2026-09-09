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

// Lembrete de hardware: o radio do ESP32 classico e 2,4 GHz APENAS.
// Apontar para um SSID de 5 GHz devolve "rede nao encontrada", nao erro
// de senha - e o diagnostico se perde procurando no lugar errado.
#define FARMIO_WIFI_SSID "REDE_2G_AQUI"
#define FARMIO_WIFI_PASS "SENHA_AQUI"

// Senha do AP que o proprio vaso levanta quando nao ha credencial.
#define FARMIO_AP_PASS "farmio123"
