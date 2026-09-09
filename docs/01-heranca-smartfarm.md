# De onde o FarmIO veio: o SmartFarm

O FarmIO não começa do zero. Ele é a segunda geração de um projeto anterior
chamado **SmartFarm**, cuja especificação foi escrita antes deste repositório
existir e ficou guardada no Google Drive do laboratório.

Este documento preserva aquela especificação e registra, ao lado, o que muda.
A regra do laboratório é que decisão registrada não se perde — e a especificação
do SmartFarm é boa demais para virar arquivo esquecido numa pasta.

> **Original:** `smartfarm.docx`, no acervo do repositório
> [Jaspy](https://github.com/popliosemigod/Jaspy) em
> `acervo/documentos/smartfarm.md`, com o `.docx` catalogado e rastreável pelo
> `manifesto.json`.

---

## A especificação do SmartFarm, como foi escrita

### Componentes previstos

- ESP32-CAM
- Display OLED
- DHT22
- Sensor de nível de água Funduino
- Sensor de umidade de solo
- Motor bomba d'água
- LED RGB 16 bits 5050 (saídas d1, gnd, 5v, d0)

### Contexto

> A smartfarm vai funcionar de forma totalmente automática, sem que nosso cliente
> precise ficar vigiando o sistema toda hora. Quando a Smartfarm inicia o sistema
> na alimentação, os sensores já vão mostrar todas as suas leituras no display,
> que é um dos meios que nosso cliente vai conseguir acessar informações da
> planta. Outro jeito de acessar vai ser por meio do webserver da smartfarm
> gerada pela ESP.
>
> Tudo que o nosso cliente vai precisar fazer, será abastecer o tanque
> reservatório da smartfarm para que a planta tenha como fazer seu processo
> automático de irrigação.

### Lógica de cada componente

**DHT22** — leitura de temperatura ambiente e umidade do ar, com casa decimal em
graus Celsius. Acima de **30 °C** entra em SITUAÇÃO DE RISCO, com alerta breve,
apenas para avisar.

**Nível d'água** — leitura em porcentagem, 100% tanque cheio e 0% tanque vazio.
Com o tanque vazio a bomba **não pode** ser acionada, e o sistema entra em
SITUAÇÃO DE RISCO avisando que precisa reabastecer.

**Umidade de solo** — cinco faixas: extremamente baixa, baixa, estável, alta e
extremamente alta.

- extremamente **alta** → risco de afogamento, bomba bloqueada;
- extremamente **baixa** → irriga aos poucos até sair dessa faixa;
- baixa, estável ou alta → nada acontece com a bomba.

**LED RGB de 16 bits** — verde em animação circular ao ligar; branco nos 16
pixels no funcionamento padrão; vermelho nos 16 durante qualquer SITUAÇÃO DE
RISCO; ao resolver, animação circular passando de vermelho para branco.
Transições e animações **o mais lentas possível**, para não gerar desconforto
visual.

**Display OLED** — duas telas:

- **TELA INICIAL**: temperatura, umidade do ar, umidade do solo (por faixa) e
  porcentagem do tanque;
- **TELA DE SITUAÇÃO DE RISCO**: interrompe brevemente para alertar temperatura
  alta, umidade extremamente baixa ou tanque vazio.

### Detalhes de lógica, na palavra do autor

- a tela de risco **não pode ficar alertando o tempo todo** — deve aparecer
  brevemente e voltar à tela inicial, para o cliente identificar o diagnóstico;
- informação na tela limpa, centralizada, legível, ocupando o máximo do display;
- a intenção do LED não é gerar desconforto visual;
- é preciso ter todas as informações de conexão de hardware para fabricação;
- o webserver deve mostrar o mesmo que o display, simples e centralizado, com a
  tela de streaming de vídeo;
- **a ESP32 tradicional é o servidor central**, com todos os sensores ligados
  nela e uma rota JSON (`/sensores`); a **ESP32-CAM cuida apenas do streaming**,
  cujo fluxo nativo (`http://IP_DA_ESP32CAM:81/stream`) é embutido no HTML
  hospedado pela ESP32 tradicional, para que o navegador junte as duas coisas
  sem sobrecarregar a CAM;
- as mesmas informações devem passar no monitor serial;
- respeitar o uso adequado das portas da ESP32, para não haver conflito.

---

## O que o FarmIO mantém

Praticamente tudo. A especificação acima é boa: ela separa responsabilidade
entre as duas placas pelo motivo certo, define faixas em vez de limiar único, e
já tinha percebido que alerta permanente vira paisagem.

Estão implementados na v0.1, exatamente como descritos: as cinco faixas de solo,
os intertravamentos da bomba, as duas telas, a linguagem de cor do anel, a rota
`/sensores`, o vídeo embutido e o eco pela serial.

## O que o FarmIO muda, e por quê

| Mudança | Por quê |
| --- | --- |
| **Irrigação pulsada** (4 s de bomba, 20 s de espera) em vez de bomba contínua até a leitura mudar | A água leva dezenas de segundos para percolar do dreno até o sensor. Em malha contínua, a bomba despeja o tanque inteiro antes de a leitura reagir — e afoga a planta que deveria salvar |
| **Teto absoluto de bomba por ciclo** (120 s) | Sensor de solo com mau contato lê "seco" para sempre. Sem teto, a bomba ficaria ligada até acabar a água ou queimar |
| **Solo encharcado vira risco explícito**, com nome próprio | Na especificação original ele bloqueava a bomba mas não aparecia como alerta. Bloqueio silencioso é o pior tipo: parece defeito |
| **Sensor mudo vira risco** | O DHT22 falha de vez em quando. Quatro falhas seguidas passam a ser diagnóstico, não silêncio |
| **Nada bloqueia o loop** | Padrão de firmware do laboratório. Um vaso que trava esperando Wi-Fi deixa a planta secar em silêncio |
| **Página web sem CDN** | A rede da bancada nem sempre tem uplink. Página que depende de internet para carregar CSS falha justamente no dia do ensaio |
| Nome do projeto | O FarmIO é **acoplável**: vasos se encaixam e cuidam de um conjunto. O diagnóstico de qualidade foliar (via câmera) é o passo seguinte, que o SmartFarm já preparava ao incluir a ESP32-CAM |

## O que ainda não existe

- **Diagnóstico de qualidade foliar.** A câmera hoje só transmite vídeo. A
  análise de imagem é o diferencial do FarmIO sobre o SmartFarm e ainda não foi
  escrita.
- **Acoplamento entre vasos.** Nenhum protocolo entre nós foi definido.
- **Calibração real dos sensores.** Os limiares em `include/config.h` são ponto
  de partida, não medida — ver o aviso lá e o [diário](../diario.md).
