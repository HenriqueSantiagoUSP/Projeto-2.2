# CLAUDE.md

Projeto de faculdade (Escola Politécnica da USP, Projeto Integrativo 2 e 3): rede de sensores para
monitoramento não invasivo de idosos em **ILPIs** (Instituições de Longa Permanência; referência: ILPI
Butantã). Dois ESP32-S3:

- o **hub do quarto**: portas, presença, temperatura, regras de alerta, gateway CAN/MQTT;
- o **vestível** (pulseira com MPU-6050): detecção de queda.

A **central** guarda, mostra e notifica: Mosquitto, Grafana, Telegram, um banco relacional (PostgreSQL; protótipo em SQLite na atividade de Fundamentos de Software) e, no
futuro, nuvem.

## Onde está cada coisa

| Pasta | Conteúdo |
|---|---|
| `docs/protocolo.md` | **contrato** entre placas e central: tópicos MQTT, payloads, alertas, config, IDs CAN, mensagens ESP-NOW |
| `docs/arquitetura.md` | dispositivos, regras do hub, central, estado de cada parte |
| `docs/requisitos.md` | requisitos vigentes com metas mensuráveis e plano de avaliação |
| `docs/roadmap.md` | plano de implementação em fases, com critério de pronto, e o futuro |
| `docs/decisoes.md` | histórico das decisões, o que mudou entre relatórios e o que dá para melhorar |
| `docs/estado.md` | **em que etapa estamos**: fase atual, o que foi feito, como foi verificado, diário |
| `docs/pendencias.md` | **o que falta fazer**: lista única com caixas de marcar, por área, e o histórico do que foi concluído |
| `docs/coleta-de-dados.md` | protocolo de ensaios rotulados para o futuro modelo de queda |
| `firmware/room/` | hub do quarto (ESP-IDF) |
| `firmware/gadget/` | vestível (ESP-IDF) |
| `firmware/common/` | componentes das duas placas: `pcb` (mapa de pinos da PCB única), `protocol` (contrato em C), `rtos_util` (prioridades e núcleos), `sys_events` (event group), `net`, `mqtt_link`, `can`, `indicators` (LED; buzzer opcional, ausente na PCB atual) |
| `interface/` | central em Docker Compose (Mosquitto, Telegraf, InfluxDB, Grafana, notificador) + `simulador.py`; `legado/` = demonstração de 18/09, só histórico |
| `modelo/` | modelos de detecção de queda desenvolvidos no PC (Python 3.12 via `uv`): pipeline, atributos, AR, templates, DTW, classificadores, CNN, avaliação por pessoa; `resultados/resumo.md` tem os números |
| `dados/` | SisFall e ensaios próprios (fora do git); só `dados/README.md` é versionado e descreve o conjunto |
| `hardware/` | esquemáticos, PCB, mapa de pinos e conflitos conhecidos |
| `relatorios/` | enunciados e entregas por semestre/projeto; índice em `relatorios/README.md` |
| `aulas/` | slides das aulas por semestre; índice em `aulas/README.md` (assunto de cada slide e onde ele se aplica) |

## Regras

- **Mudou algo em relação ao plano** (roadmap, arquitetura, protocolo, requisitos, hardware)? Atualize na
  mesma tarefa todos os documentos afetados, registre a mudança e o porquê em `docs/decisoes.md` e ajuste este
  `CLAUDE.md` se ela mudar o contexto ou as regras. Nunca deixe o código à frente da documentação.
- **Registro de etapa:** `docs/estado.md` é o único lugar que diz em que fase o projeto está. Ao fim de cada
  tarefa, atualize a fase atual, o que foi feito e **como foi verificado** (build, testes no PC, bancada), com
  uma entrada datada no diário. Não declare como testado em placa o que só compilou.
- **Lista do que falta:** `docs/pendencias.md` é a única lista de pendências e próximos passos; não as repita
  em outro arquivo. Ao fim de toda tarefa, junto com o `estado.md`:
  - marque como `[x]` o que ficou pronto e mova para "Concluído" (no topo), com a data e como foi verificado;
  - acrescente na seção certa o que surgiu (defeito achado, decisão adiada, pedido novo);
  - ajuste os itens que mudaram de escopo.

  Um item só é concluído quando o critério dele foi cumprido: se ele pede bancada, compilar não basta. Quando
  o usuário perguntar o que falta, responda a partir dessa lista e, se ela estiver desatualizada, corrija-a
  antes.
- `docs/protocolo.md` é a fonte única da verdade. Qualquer mudança em tópico, payload, QoS, alerta, config,
  ID CAN ou mensagem ESP-NOW altera esse arquivo no mesmo commit que altera o código. Se código e documento
  divergirem, aponte a divergência em vez de escolher um lado em silêncio.
- **Processamento na ESP32** (diretriz da disciplina): a detecção de queda roda no vestível; as regras de
  tempo e a geração de alertas rodam no hub. Não mova essa lógica para a central. A central só armazena,
  mostra e notifica.
- A **detecção de queda** no vestível é o pipeline do `modelo/` (`firmware/gadget/components/fall_pipeline`,
  desde 2026-11-11): filtro → gatilho (picos de |a| ≥ 2,0 g) → janela → 23 atributos → classificador (Random
  Forest; regressão logística como opção).
  - O limiar é só o gatilho: quem decide é o classificador.
  - O modelo foi treinado na SisFall (cintura) e é **provisório até retreinar com os dados do pulso**. Não o
    apresente como final, e preserve o que alimenta o retreino: amostragem por hardware e coleta de dados
    brutos.
  - `fall_model.h`/`fall_model.c` e `test_host/vetores/` são **gerados** por
    `modelo/experimentos/09_exportar_vestivel.py`: retreine lá, nunca edite à mão.
  - Ao mudar o pipeline (C ou Python), mantenha a paridade com `modelo/queda/`:
    - mesmas definições de atributos e o mesmo gatilho (`gatilho_causal`);
    - rode os testes de host do `fall_pipeline`, que comparam com os vetores do Python.
- `interface/legado/` e o código do semestre 1 (listagens nos relatórios) são só referência. Não os trate como
  restrição de projeto.
- Lógica pura (regras, config, detector, notificador) tem testes que rodam no PC. Ao mudá-la, rode os testes
  e acrescente casos; não dependa de placa para verificar lógica.
- Metas de desempenho estão em `docs/requisitos.md`. Ao implementar, confira o requisito correspondente.
- Código C e comentários em inglês; documentação, painéis e mensagens em português.

## Instruções da disciplina

- Enunciados e entregas ficam em `relatorios/<Semestre>/<Projeto>/`. O índice `relatorios/README.md` resume o
  que cada enunciado pede, o que foi entregue e o que ainda vale. Antes de implementar algo de uma entrega,
  leia a seção correspondente.
- Relatórios antigos podem estar superados; o que vale hoje está em `docs/`, e `docs/decisoes.md` lista as
  inconsistências entre relatórios.
- PDF novo em `relatorios/`: extraia o texto (`pdftotext -enc UTF-8 -layout <arquivo> -`) e acrescente a
  seção no índice.
- **O firmware segue os slides de `aulas/` e a documentação oficial do ESP-IDF v6.0.1.** Antes de escrever
  ou revisar código de um periférico ou técnica (GPIO, timer, UART, RTOS, ADC, CAN, amostragem, atributos,
  FFT), leia o slide correspondente (índice em `aulas/README.md`) e siga o padrão dele: prioridades nomeadas,
  núcleo explícito, ISR mínima, filtro de glitch, calibração do ADC, recuperação de bus-off, ESP-DSP. Slide novo
  em `aulas/`: acrescente a linha no índice.
- Requisito de enunciado em conflito com `docs/` ou com o código: aponte o conflito antes de seguir.

## Firmware (ESP-IDF 6.0.1, alvo `esp32s3`)

ESP-IDF instalado em `C:/esp/v6.0.1/esp-idf`. Para ativar no PowerShell:
`. C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1`. Depois, na pasta da placa:

```
idf.py menuconfig            # menus comuns "Network (Wi-Fi)", "MQTT", "CAN bus (TWAI)"; da placa: "Room hub: ESP-NOW" / "Wearable"
idf.py build
idf.py -p COMx flash monitor
```

Testes no PC, sem placa (Git Bash, com o gcc do MSYS2 no PATH: `export PATH=/c/msys64/ucrt64/bin:$PATH`):

```
sh firmware/room/components/rules/test_host/run_tests.sh         # regras do hub
sh firmware/room/components/hub_config/test_host/run_tests.sh    # JSON da config (precisa de 1 build do room)
sh firmware/gadget/components/fall_pipeline/test_host/run_tests.sh # pipeline de queda: gatilho, filtro, paridade com o Python
sh firmware/gadget/components/signal_proc/test_host/run_tests.sh # buffer circular, atributos, atividade
```

Para compilar os caminhos opcionais do vestível sem mexer no `sdkconfig` dele, use um build de teste com outro
`sdkconfig` (no scratchpad):
`idf.py -B <dir>/build -D SDKCONFIG=<dir>/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;<dir>/extra.defaults" build`,
com `CONFIG_GADGET_MQTT=y`, `CONFIG_GADGET_CAN=y` etc. no `extra.defaults`.

Demonstração com uma placa só (vestível sem hub, perfil `firmware/gadget/sdkconfig.demo.defaults`; no PC,
`interface/demo_vestivel.py`):
`idf.py -B build/demo -D SDKCONFIG=build/demo/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.demo.defaults" build flash`.

Pegadinhas dos firmwares:

- `MINIMAL_BUILD` está ligado: todo componente usado por `main` precisa estar em `PRIV_REQUIRES` de
  `main/CMakeLists.txt`.
- Os dois projetos incluem `firmware/common/` por `EXTRA_COMPONENT_DIRS` no `CMakeLists.txt` raiz.
- O que as duas placas usam fica em `firmware/common/`, nunca copiado numa placa; pinos e tópicos entram por
  struct de configuração (`can_config_t`, `net_config_t`, `mqtt_link_config_t`).
- O componente MQTT se chama `mqtt_link`, não `mqtt`: um componente local chamado `mqtt` sobrescreveria o
  `espressif/mqtt`. cJSON, MQTT e `led_strip` vêm do armazenamento local do EIM; `espressif/esp-dsp` (vestível)
  não está lá, então o primeiro build do vestível precisa de internet.
- **Tasks:** sempre `xTaskCreatePinnedToCore` com `PRIO_*` e `CORE_*` de `task_config.h` (nunca acima de
  `PRIO_CRITICA` = 20; núcleo 1 para tempo real, núcleo 0 para rádio e protocolos).
- **Recurso compartilhado:** fila por valor entre tasks; estado do sistema em `sys_events` (um escritor por
  bit); mutex só por pouco tempo; `portMUX` para poucas instruções. Nenhuma task segura duas travas, e as tasks
  vigiadas pelo watchdog só esperam com timeout. ISR e callbacks de driver só enfileiram ou notificam.
- **CAN:** qualquer task chama `can_transmit()`, que só enfileira; só a `can_tx` toca o driver. Handlers de
  recepção são registrados antes de `can_init()`. IDs e payloads do grupo `0xF` só em `protocol_can.h`.
- **Pinos:** a PCB é uma só para as duas placas, e o mapa fica só em `firmware/common/pcb/include/pcb_pins.h`.
  O `components/board/include/board_pins.h` de cada placa só escolhe e renomeia (`PIN_*`), e as portas do hub
  ficam em `BOARD_DOORS`. Nunca escreva `GPIO_NUM_<n>` fora do `pcb_pins.h`. O build falha com pino repetido,
  proibido ou reservado:
  - proibidos: *strapping* 0/3/45/46, USB 19/20, console 43/44, flash/PSRAM 26–37, LED/`VDD_SPI` 38/47/48;
  - reservados: cristal de 32 kHz 15/16; 39–42 (JTAG) só como expansão;
  - analógico só no ADC1 (1–10); I²C fixo em 1 (SDA) / 2 (SCL), o par do ULP (se as trilhas cruzarem, gire o
    módulo no layout); entradas que acordam em RTC GPIO (0–21).

  O mapa é proposta, a confirmar no layout ([`hardware/README.md`](hardware/README.md)).
- `sensor_events.h` não inclui headers de sensores (evita ciclo); só o hub (`main.c`) liga os sensores ao CAN,
  ao MQTT e às regras.
- `hub_task` é a única task que chama o motor de regras e publica; os demais só enfileiram eventos.
- Autoteste do CAN: `menuconfig` → CAN bus (TWAI) (ligado por padrão; desligar no barramento real).
- No vestível, MQTT (`GADGET_MQTT`) e CAN (`GADGET_CAN`) são opcionais e desligados por padrão; o CAN dele é só
  de bancada (requisito da disciplina e testes), a comunicação real é ESP-NOW. **MAC do hub vazio = sem hub**:
  o ESP-NOW não é ligado (bancada, demonstração), e o compilador então descarta o Wi-Fi do binário. Para
  compilar o caminho do ESP-NOW, use um build de teste com um MAC.
- **Peça ausente = um aviso, não erro:** sensor, CAN, Wi-Fi ou broker que faltam dão **um** `ESP_LOGW` quando
  percebidos e um `ESP_LOGI` quando voltam; as novas tentativas ficam em `ESP_LOGD`, com espera crescente. `E`
  só para falha de verdade (configuração recusada, queda perdida). Nada opcional pode derrubar a placa: nada de
  `ESP_ERROR_CHECK` no que pode faltar. No hub, PIR, LM35 e portas não instalados são declarados em "Room hub:
  sensors", porque soltos geram alertas falsos.
- `sdkconfig` e `build/` são ignorados pelo git; o que precisa valer para todos vai em `sdkconfig.defaults`
  (watchdog de 30 s com pânico, partição de app grande, usuário MQTT `vestivel` no vestível). O
  `sdkconfig.defaults` vale para opções que o `sdkconfig` ainda não tem; uma opção que já está nele não muda.

## Central

```
cd interface
copy .env.example .env       # e troque as senhas
docker compose up -d         # Mosquitto, Telegraf, InfluxDB, Grafana (:3000), notificador
python simulador.py --host <IP da máquina> --senha <MQTT_HUB_PASSWORD>
python -m unittest discover -s notificador
python -m unittest test_demo_vestivel       # demo_vestivel.py (requirements-demo.txt para rodar a janela)
```

Usuários do broker: `hub`, `vestivel` (MQTT opcional da pulseira), `telegraf`, `notificador`, `painel`; a ACL
fica em `interface/mosquitto/acl` e as senhas no `.env`.

**Atenção:** nesta máquina há um serviço Mosquitto do Windows (início automático) em `127.0.0.1:1883`, e ele
captura as conexões a `localhost`. Para falar com a central em Docker, use o IP da máquina ou pare o serviço
(`net stop mosquitto`, como administrador).

## Modelos de queda (PC)

Em `modelo/`, com o `uv`. Ele instala o Python 3.12 em `modelo/.venv`, porque o 3.15 da máquina não tem o
PyTorch. Ordem e detalhes em `modelo/README.md`:

```
cd modelo
uv sync                                                   # numpy, scikit-learn, statsmodels, torch (CPU)...
uv run pytest                                             # blocos contra os exemplos dos slides
uv run python experimentos/02_gatilho.py                  # escolhe o L_on (os outros leem)
uv run python experimentos/03_comparar.py --cenario A     # ±8 g; --cenario B = ±2 g (dados próprios)
uv run python experimentos/04_dados_proprios.py
uv run python experimentos/06_resumo.py                   # resultados/resumo.md, de onde saem os números
uv run python experimentos/07_figuras_explicativas.py     # figuras que explicam os resultados (lê os escores do 03)
uv run python experimentos/08_relatorio_latex.py          # figuras e tabelas do relatório LaTeX do Q1 (relatorios/.../Q1/latex)
uv run python experimentos/09_exportar_vestivel.py        # modelo do vestível (fall_model.c) + vetores de paridade
uv run python experimentos/10_figuras_apresentacao.py     # figuras, tabelas e números da apresentação Beamer do Q1
```

Os dados ficam em `dados/` (fora do git): a SisFall em `dados/publicos/` e os ensaios próprios em
`dados/proprios/`. O cache fica em `dados/cache/`.

## Ambiente

Windows 11. Docker Desktop instalado. Mosquitto do Windows em `C:\Program Files\mosquitto` (fora do PATH, e
com o serviço acima). O venv de `interface/` tem só `paho-mqtt`. `pdftotext` está disponível no Git Bash; o gcc
do MSYS2 em `C:\msys64\ucrt64\bin`.

Os arquivos do repositório usam fim de linha LF. Ao editar por script, preserve-o (em Python,
`open(..., newline="")`; `Path.write_text` grava CRLF no Windows). Um `entrypoint.sh` com CRLF derruba o
Mosquitto. O `grep` do Git Bash não mostra o `\r`; confira com `git ls-files --eol`.
