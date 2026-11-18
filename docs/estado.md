# Estado do projeto

**O único lugar que diz em que etapa estamos.** O plano está em [`roadmap.md`](roadmap.md); aqui fica o que já
foi feito e **como foi verificado**. Atualize ao fim de cada tarefa, com uma entrada no diário.

Níveis de verificação usados abaixo:

| Nível | Significa |
|---|---|
| **build** | compila no ESP-IDF 6.0.1 sem erros nem avisos |
| **testes no PC** | testes automáticos rodando no PC (lógica pura, sem placa) |
| **ponta a ponta (sem placa)** | central real em Docker + simulador no lugar do hub |
| **bancada** | rodando na placa, com os sensores |

## Etapa atual

**Fases 1 a 7 com código pronto, revisado nos moldes das aulas; nada verificado em bancada ainda.** O firmware
das duas placas agora segue os slides de [`aulas/`](../aulas/README.md) e o ESP-IDF v6.0.1:
- componentes comuns em `firmware/common/`;
- tasks com prioridade nomeada e núcleo fixo;
- event group no lugar de variáveis compartilhadas;
- CAN e MQTT nas duas placas (na pulseira, como opção);
- ESP-DSP na pulseira.

O próximo passo é gravar as placas e verificar em bancada (ver [`pendencias.md`](pendencias.md)). O mapa de pinos é o da
**PCB única** das duas placas ([`hardware/README.md`](../hardware/README.md)). A **PCB v1** está desenhada,
conferida (ERC e DRC sem erros) e com os arquivos de fabricação em
[`hardware/placas/v1/`](../hardware/placas/v1/README.md). Ela já foi fabricada e está em mãos; falta montar
e testar.

**S2 P3:** o Q1 (modelos) foi entregue e apresentado. Os próximos são o Q2, um relatório para 17/11, e o Q3, a
comunicação sem fio funcionando com os sensores, para 24/11.

**Modelo de queda:**
- **2026-11-09 (S2 P3 Q1):** o pipeline e 22 métodos foram desenvolvidos e comparados **no PC**
  ([`modelo/`](../modelo/README.md)), com a SisFall e 18 ensaios próprios, validação cruzada por pessoa.
- **2026-11-11:** o pipeline foi para a pulseira, com o Random Forest (`fall_pipeline`, no lugar do detector
  provisório). Foi verificado nos testes de host, com paridade C × Python, e no build; ainda não rodou em placa.
- O modelo foi treinado na SisFall (cintura) e precisa ser retreinado com os dados do pulso.

## Situação por fase

| Fase | O quê | Situação | Verificado por |
|---|---|---|---|
| 0 | Estrutura do repositório e documentação | feito | — |
| 1 | Contrato em código (`firmware/common/protocol`, agora com `protocol_can.h`) e componentes comuns (`rtos_util`, `sys_events`, `net`, `mqtt_link`, `can`, `indicators`) | feito | build (hub e pulseira) |
| 2 | Hub: debounce corrigido, portas modulares, LM35 com `adc_cali` + curva MMQ (a cada 60 s, publicada só quando muda, desde 2026-11-17), aquecimento do PIR | código pronto | build; envio por mudança em testes no PC (`report`, 23 verificações) |
| 3 | Hub: Wi-Fi STA + `WIFI_PS_NONE` + backoff, SNTP, MQTT 3.1.1 com senha, Last Will, outbox, config em NVS, watchdog de 30 s | código pronto | build; validação da config em testes no PC (60 verificações) |
| 4 | Hub: motor de regras, `alerta`, buzzer, LED de status | código pronto | build; regras em testes no PC (22 cenários, 82 verificações) |
| 5 | Central: Mosquitto com ACL (agora com o usuário `pulseira`), Telegraf, InfluxDB, Grafana, notificador, simulador novo | feito | ponta a ponta (sem placa); notificador em testes no PC (22 testes) |
| 6 | Pulseira: MPU-6050 a 200 Hz pelo FIFO, modo de coleta, ESP-NOW com varredura e heartbeat; MQTT e CAN opcionais | código pronto | build (padrão e com MQTT + CAN + coleta ligados) |
| 7 | Pulseira: pipeline de queda (filtro, gatilho de 2,0 g, 23 atributos, Random Forest ou regressão logística) + cooldown, botão (dois toques = "estou bem"; segurar 3 s = pedido de ajuda, desde 2026-11-17), bateria. Até 2026-11-10 era o detector provisório com histerese | código pronto, **sem light sleep**; modelo provisório (SisFall, cintura) | build; `fall_pipeline` (885 verificações, com paridade C × Python em 26 janelas), `signal_proc` (25) e `button_gesture` (42) no PC |
| 8 | Coleta de dados | protocolo escrito ([`coleta-de-dados.md`](coleta-de-dados.md)); SisFall e 18 ensaios próprios (cintura, ±2 g, fora do protocolo) descritos em [`dados/README.md`](../dados/README.md); falta a coleta no pulso | leitura e contagem pelo `modelo/` |
| 9 | Avaliação | não iniciada | — |

### O que a verificação "ponta a ponta (sem placa)" cobriu (2026-10-20)

- `docker compose up` sobe os 5 serviços.
- O broker recusa conexão anônima e senha errada; a ACL impede o `notificador` de ler portas.
- O simulador publicou estados, heartbeats e episódios de alerta:
  - o notificador formatou cada episódio;
  - o InfluxDB recebeu as 6 medidas, com as tags `site`, `quarto` e `id`;
  - as 12 consultas do painel Grafana rodaram sem erro e devolveram dados.
- `config/desejada` publicada pelo usuário `painel` voltou em `config/aplicada`.
- Não coberto: o envio real ao Telegram (falta um bot) e a aparência do painel no navegador.

## O que falta

A lista de pendências e próximos passos fica em [`pendencias.md`](pendencias.md), com caixas de marcar.
Ela é atualizada ao fim de toda tarefa, junto com este arquivo.

## Diário

### 2026-11-18 — revisão do relatório do Q2 e correções

- **Pedido:** revisar o relatório inteiro (melhorias, o que falta, o que está bom, se as partes se conectam) e
  dar uma nota; depois, corrigir os itens apontados. Ficaram como estão, por decisão do usuário:
  - "relatório final" na capa (é a convenção da sala: primeira versão do último relatório);
  - as limitações (o que ainda não rodou em placa entra até a última iteração);
  - os itens de prioridade baixa.
- **Revisão:** nota 8,5. Cobertura do enunciado e profundidade técnica fortes. Os descontos foram:
  - a coerência, com trechos desatualizados pelas mudanças recentes;
  - a tipografia dos decimais em fórmula;
  - a evidência medida no hardware novo, que é da entrega final.
- **Feito:**
  - **Resumo:** "0,55 alarme por hora, com 94,3 % das quedas" (o mesmo ponto de operação; antes juntava o
    0,55 com os 95,1 % do critério de no máximo 1 alarme por hora);
  - **decimais em fórmula:**
    - `\usepackage{icomma}`: "144,739" em vez de "144, 739" (Resumo, equações 1, 2, 4 e 5);
    - os coeficientes do filtro: o `\bqCoefs` tinha um `$-$` que, dentro da fórmula, saía como hífen. Agora
      vai com ";" como separador, já que a vírgula é a decimal;
  - **desatualizados:**
    - 5.4: o intervalo entre medições passou a 60 s;
    - 7.3: a temperatura sai quando muda;
    - Tabelas 15 e 27: regras 87, `report` 28, demo 26;
    - Apêndice D: os testes de `button_gesture` e `report`;
    - a temperatura que cruza um limite em RF10, 8.3, 9.3 e na Tabela 22;
    - Tabela 16: medida de novo com `idf.py size`. Hub 994 528 B e DIRAM 32,9 %; pulseira com ESP-NOW (build
      de teste só com o MAC) 962 656 B e 38,2 %; sem hub 385 136 B e 25,2 %;
  - **RNF12** (proteção física, IP42) de volta à Tabela 2: a seção de custo o citava;
  - **12.2:** o ensaio de 17/11, com a placa solta numa almofada. O pipeline rodou na placa, as linhas
    cortadas foram corrigidas, e classificar uma janela levou de 1,5 a 2,5 ms. A linha "Pulseira na placa"
    da Tabela 27 também;
  - **11.3:** o Telegram "com o bot configurado";
  - **Figura 34:** a captura refeita pelo usuário entrou com o mesmo recorte.
- **Verificado:**
  - o script 12 inteiro, com só o `\bqCoefs` mudando, e os renders da PCB restaurados;
  - `pdflatex` sem erros, sem referência indefinida, sem caractere faltando e sem *overfull* (72 páginas);
  - na imagem: o Resumo, as equações, os coeficientes, a Tabela 2, a 12.2 e a Figura 34;
  - no texto do PDF, nenhum decimal com espaço (só listas e referências).

### 2026-11-18 — revisão de código da leva do Q2 e correções

- **Pedido:** revisar (`/code-review`) as mudanças da leva (`4262bf5..HEAD`) contra o que está decidido e procurar
  defeitos parecidos com o da linha FEAT cortada, que dependem de tempo entre tasks.
- **Revisão:** 15 achados, conferidos no código. Três ficaram sem mudança:
  - a versão do ESP-NOW, que não mudou com o `HELP` de propósito; mudá-la nem resolveria, porque o ACK é da
    camada de rádio;
  - o episódio de queda, que só fecha com cancelamento (decisão registrada);
  - o reenvio de todos os pendentes por rodada, que deixa de importar com as correções de tempo abaixo.
- **Feito:**
  - **pulseira:**
    - **gesto com a fila cheia:** guardava só o tipo, numa vaga só, e era julgado pela hora em que o `comm_task`
      o lia. Agora há uma vaga por gesto, com a hora, sob `portMUX`. Ele é tratado com a fila vazia, depois do
      que veio antes dele (a queda que os dois toques cancelam);
    - **a janela da queda conta da detecção:** o `COMM_EV_FALL` leva a hora em que a `classify_window` decidiu;
      antes, era a hora em que o `comm_task` tirava a queda da fila;
    - **sem hub e com MQTT:** um alerta ou um cancelamento com o broker fora ia só para o log. Agora fica
      guardado e é reenviado quando o broker volta, contra a regra de que uma queda que não chegou a lugar
      nenhum é reenviada. A volta do broker adianta o reenvio;
    - **FEAT com `acima`:** a decisão do classificador antes da espera. A demo usa esse campo em vez do limiar
      do log, que vem arredondado (com a regressão logística, 0,96356 virava 0,96);
  - **hub:**
    - **buzzer:** a nova `rules_resident_alert_active()` diz se resta queda ou ajuda aberta. O `emit()` já
      atualiza o episódio antes do callback. Antes, cancelar uma queda calava a ajuda ainda aberta;
    - **volta de offline:** com uma mensagem que não é heartbeat, `pulseira/<id>` sai só com estado e rssi, sem
      a atividade e a bateria de antes do silêncio;
    - **temperatura:** sai na hora quando cruza `temp_min_c` ou `temp_max_c` em relação ao último valor enviado
      (`protocolo.md` §1 e `decisoes.md`);
  - **comum:** `REPORT_MAX_MS_FOR_PERIOD` dá ao sinal de vida meio período de folga, nos três usos. Com 300 s
    exatos, alguns ms de atraso adiavam o envio para a leitura seguinte, e o "5 min no máximo" virava 6;
  - **demo:**
    - `Ponte.cancelar(tipo)` só cancela se o aberto for do tipo que a placa cancelou;
    - `alerta_pulseira()` substitui as duas funções de alerta;
  - **relatório:** a data dos preços (`custoData`) vem da maior `consultado_em` da planilha. O README dos
    relatórios aponta para `pendencias.md`, em vez de repetir a lista das fotos que faltam.
- **Verificado:**
  - **testes de host:** regras 87 (5 novos), `report` 28 (5 novos), cada um com um erro plantado numa cópia
    que o teste pegou. Os outros sem mudança: `hub_config` 60, `fall_pipeline` 885, `signal_proc` 25,
    `button_gesture` 42;
  - **Python:** demo 26 testes (5 novos) e notificador;
  - **builds sem avisos:** hub; pulseira padrão e demonstração; e dois builds de teste, um sem hub com MQTT e
    outro com hub, MQTT, queda simulada e modo de coleta;
  - **script 12:** rodou inteiro, sem diferença nas tabelas.
  - **Falta:** a placa não estava ligada, então o perfil de demonstração novo não foi gravado. As correções de
    tempo e de buzzer precisam da bancada (`pendencias.md`).

### 2026-11-17 — relatório do Q2 sem os quadros de foto vazios; link do GitHub nos apêndices

- **Pedido:**
  - tirar do relatório os quadros das fotos que não entraram (ESP-NOW e Telegram, adiadas para a entrega
    final) e ajustar o texto para fazer sentido sem elas;
  - pôr no Apêndice C o link do repositório no GitHub.
- **Feito:**
  - **as duas figuras** ficaram comentadas no código, com o motivo e as instruções para voltarem (topo do
    `main.tex`):
    - **ESP-NOW (`09_sem_fio.tex`):** estava solta no fim de "Segurança", e nenhum texto a citava; o texto
      ficou como estava;
    - **Telegram (`11_central.tex`):** no lugar da foto entrou a mensagem que o notificador montou para a
      última queda da rodada com o simulador (a das 21:42:17, a mesma do topo da tabela da Figura 33). Uma
      frase diz que, sem o token do bot, ela só vai para o log, e que com o bot é ela que chega ao celular. Para
      o "²" da mensagem, a regra de acentos das listagens ganhou o `²`;
  - **link do GitHub:**
    - **Apêndice C:** está na frase que diz onde ficam os links dos preços, junto com o projeto do KiCad, a
      lista de materiais e os Gerbers. Um parágrafo próprio antes do "Custo item por item" empurrava a tabela
      de custo para a página seguinte e deixava meia página em branco (73 páginas);
    - **Apêndice D:** "Tudo está no repositório do projeto" passou a mostrar o link, centralizado.
  - **pendências:** o repositório local não tem remoto. O item "Cópia fora deste PC" agora diz que os caminhos
    citados têm de estar no GitHub antes da entrega.
- **Verificado:** `pdflatex` no lugar sem erros, sem referência indefinida, sem caractere faltando e sem
  *overfull* (72 páginas); nenhum "Inserir foto" no PDF; o link aparece nos dois apêndices; as páginas do
  notificador e dos Apêndices C e D conferidas na imagem.

### 2026-11-17 — a métrica de alarmes falsos por hora explicada no relatório do Q2; fotos adiadas

- **Pedido:**
  - explicar melhor, na parte de processamento, a métrica de alarmes falsos por hora de atividade: quais são
    essas horas, como se chega às contas e por que é uma boa métrica;
  - deixar para a entrega final as fotos do ESP-NOW (só uma placa em mãos) e do Telegram (o bot ainda não
    existe), porque esta entrega é provisória.
- **Feito:**
  - **relatório:** a nova Seção 10.4, "A métrica: alarmes falsos por hora de atividade", entre o pipeline e a
    comparação. Ela tem:
    - **as horas:** os 2707 ensaios sem queda da SisFall somam 14,5 h, e a Tabela 23 mostra de quais grupos
      de atividade elas vêm (andar e correr são 29 %). São horas só de movimento, escolhido para parecer queda;
    - **a conta do Random Forest:** 10 939 janelas de atividade (753/h); limiar no ponto que deixa 95 % das
      1784 janelas de impacto acima dele; 8 alarmes ÷ 14,5 h ≈ 0,55/h, com 94,3 % das quedas; e a coluna
      "≤ 1 alarme/h" (14 alarmes admitidos → 95,1 %);
    - **por que essa métrica:** é o que custa na ILPI; a especificidade de 99,93 % por janela engana, porque
      depende de quantas janelas o gatilho abre; compara métodos com janelas diferentes; vem sempre com a
      fração das quedas; e liga ao RNF08 (≤ 1/dia ⇒ até 1,8 h/dia de movimento como o da SisFall);
    - **os limites:** ensaios encenados, sensor na cintura, e o pulso tem movimentos que a cintura não vê.

    A Comparação e a introdução da seção apontam para ela, e o item "Dados" passou a usar os números gerados;
  - **script** `12_relatorio_q2.py`: a nova `tabela_horas_atividade()` lê a SisFall e gera
    `tabelas/horas_atividade.tex` e os números da conta (`\adlHoras`, `\adlJanelas`, `\rfAlarmesN`,
    `\rfSensEnsaio`, `\rfEspecJanela`, `\alarmesPermitidos`, `\rnfHorasDia` e outros). Ela confere que as
    horas batem com os disparos por hora do `09` e que os alarmes dão um número inteiro de janelas. As partes
    da tabela fecham em 100 % (maiores restos);
  - **tipografia:** os números com vírgula dentro de fórmula (o limiar e o gatilho, também no RF02) passaram a
    `\text{}`, para não sair "0, 73";
  - **pendências:** as fotos do ESP-NOW e do Telegram foram para a entrega final, ligadas aos itens de
    bancada de que dependem.
- **Verificado:** o script inteiro rodou (68 números), e os renders da PCB que ele refez foram restaurados do
  commit; `pdflatex` sem erros, sem referência indefinida e sem *overfull* (72 páginas); as páginas 49 a 51
  conferidas na imagem.

### 2026-11-17 — quedas "não acionadas" na demo: linha FEAT cortada e espera depois de uma queda

- **Pedido:** na demo, um disparo com P = 0,88 saiu como "não é queda" (o limiar é 0,73), picos acima do
  gatilho não apareceram, e "diversas quedas não foram acionadas".
- **Investigado:** sessão gravada com `--gravar` (11 disparos em ~25 s). Pelo log da placa:
  - **4 quedas avisadas:** P de 0,80 a 0,99;
  - **5 com P de 0,76 a 0,94 na espera:** caíram menos de 5 s depois de uma queda avisada
    (`CONFIG_GADGET_FALL_COOLDOWN_S`, 5 s no perfil de demonstração), e por isso não foram avisadas. É o
    funcionamento previsto, mas a demo as mostrava como "não";
  - **2 abaixo do limiar:** P de 0,63 e 0,22;
  - **4 das 11 linhas FEAT cortadas** por linhas CSV e descartadas pela demo, entre elas uma queda avisada
    (P = 0,97) que nunca apareceu.

  O `stdout` da picolibc (ESP-IDF 6) é um só, com *buffer* por linha, e cada `printf` segura a trava só
  durante a chamada. A FEAT saía em ~26 `printf`, e o CSV da `sampling_task`, no outro núcleo, entrava entre
  eles. Os 6 cortes caíram todos entre chamadas, e nenhuma das 10 799 linhas CSV chegou quebrada: um `printf`
  só sai inteiro.
- **Feito:**
  - **firmware da pulseira:** a `log_window` monta a FEAT num *buffer* estático de 1024 caracteres (a maior real
    tem 480) e a imprime num `printf` só. A `sampling_task` lê o FIFO do MPU em lotes de ~50 ms, então os
    ~7 ms com a trava presa não perdem amostras;
  - **demo:** a nova `rotulo_disparo` separa "QUEDA", "já avisada" (P acima do limiar, na espera) e "não". O
    disparo já avisado aparece como triângulo laranja vazado, e a lista e a decisão dizem "já avisada". O README
    da central explica as cores e a espera.
- **Verificado:**
  - **firmware:** os builds da pulseira, padrão e demonstração, sem avisos; o de demonstração gravado na placa
    (COM3), com o boot e o fluxo CSV conferidos (~200 linhas/s);
  - **demo:** 24 testes, com 5 novos (o rótulo e a FEAT cortada); um quadro da sessão gravada conferido na
    imagem.
  - **bancada:** uma sessão nova gravada na placa, com o usuário soltando a placa. Os 13 disparos do log
    (`trigger at sample`) chegaram com a FEAT inteira, nenhum texto emendado e nenhuma das 10 239 linhas CSV
    quebrada. O leitor da demo aceitou as 13 linhas: 5 quedas, 4 já avisadas e 4 abaixo do limiar. Uma das
    que ficaram abaixo foi um pico de 10,2 g com P = 0,69, decisão do modelo, que ainda é provisório.

### 2026-11-17 — janela da demo no relatório do Q2

- **Pedido:** compilar o relatório com a captura da janela do `demo_pulseira.py` que o usuário tirou
  (`imagens/demo_pulseira.png`, 1917 × 1197 px a 144 dpi).
- **Feito:** a Figura 36 (seção 11.3) usa a captura direto, na largura do texto, com um corte no próprio LaTeX
  (`trim`, sem mexer no arquivo). O corte tira a barra de título da janela, em cima, e os avisos da placa e a
  barra de ferramentas do matplotlib, embaixo. A legenda cita a queda detectada (P = 0,99). A demo saiu da
  lista de fotos pendentes.
- **Verificado:** `pdflatex` sem erros, sem referência indefinida e sem *overfull* (71 páginas); a página da
  figura conferida na imagem.

### 2026-11-17 — central em Docker com o simulador e captura do Grafana para o relatório

- **Pedido:** subir a central, rodar o simulador e tirar a captura do painel "Quartos" para o relatório do Q2.
- **Feito:**
  - **rodada:**
    - central em Docker (`docker compose up -d`; o notificador foi reconstruído para ter o alerta `ajuda`);
    - o simulador por ~15 min, com temperatura a cada 5 s e resumo da pulseira a cada 60 s;
    - um roteiro de alertas: queda, cancelamento, saída noturna, ajuda, temperatura, resolução, queda.

    O Telegram está vazio no `.env`, então o notificador só imprimiu;
  - **defeitos achados e corrigidos:**
    - **pulseira fora do InfluxDB:** o consumidor `telegraf-ids` usa sessão persistente, e com ela o Telegraf
      não assina tópicos novos enquanto o broker guarda a sessão. Ele ficou assinando `vestivel/+` desde a troca
      de nomes (commit 9a17edd). Corrigido com o `client_id` `telegraf-ids-2`, comentado no `telegraf.conf`, e
      a sessão velha apagada no broker. A pegadinha entrou no README da central e no `CLAUDE.md`;
    - **tabela de alertas:** erro do Flux (`schema collision`), por juntar campos de texto com o `detalhe_prob`
      numérico antes do `pivot`. Agora é `pivot` e depois `group()`;
    - **quadros de estado** (hub, presença, portas, pulseira): mostravam a hora como um segundo valor, porque a
      consulta devolvia o `_time`. Agora devolvem só o valor (e o `id`);
    - **"Portas e presença":** a presença aparecia como duas linhas, `_value` e `id`, porque o `id` criado pelo
      `map` ficava fora do agrupamento. Agora há um `group(columns: ["id"])`;
  - **captura:** feita pelo Edge sem janela, comandado por um script (DevTools) que entra com o usuário do
    `.env` e salva o PNG (3000 × 2300, tema claro, intervalo fixo da rodada). Entrou como
    `imagens/grafana_quartos.png` (Figura 34, seção 11.1), com a legenda dizendo que os dados são do
    simulador.
- **Verificado:**
  - **InfluxDB:** gravou, nos 14 min, 6 pontos de temperatura (só as mudanças de 0,2 °C), 15 resumos da
    pulseira e 8 alertas;
  - **notificador:** formatou todos os alertas, inclusive "Pedido de ajuda (botão da pulseira)" e o
    cancelamento;
  - **relatório:** compilado numa cópia sem erros, sem referência indefinida e sem *overfull* (71 páginas), e
    a página da figura conferida na imagem. O `main.pdf` da pasta estava aberto no Acrobat e só foi
    recompilado na entrada seguinte.

### 2026-11-17 — custo estimado de um quarto no relatório do Q2

- **Pedido:** na lista de materiais, o preço aproximado de cada item e o custo de produção do produto.
- **Decidido com o usuário:** o kit de um quarto (hub + pulseira, cada PCB só com as peças da sua função), com
  preços do varejo brasileiro em R$, alimentação (fonte do hub, power bank da pulseira) e caixas; sem a central
  e sem montagem. A PCB entra como estimativa, porque não se sabe o total pago.
- **Feito:**
  - **preços** pesquisados nos sites das lojas (Eletrogate, AutoCore Robótica, Usinainfo, Curto Circuito,
    Smart Kits, KaBuM!) em 17/11/2026, em `hardware/placas/v1/custo-estimado.csv`, com o link de cada um.
    Ficaram como estimativa a PCB (US$ 2 pelas 5 + frete de ~US$ 15 + 60 % de imposto de importação e 20 % de
    ICMS, a R$ 5,15/US$) e o capacitor de poliéster de 1 µF;
  - **script** `12_relatorio_q2.py`: `tabela_custo()` gera `tabelas/custo.tex` (item por item) e
    `tabelas/custo_resumo.tex` (por grupo), e os números `\custoHub`, `\custoPulseira`, `\custoKit`,
    `\custoDevkitPct`, `\custoPcbPct` e `\custoAlimCaixasPct`;
  - **relatório:** a pedido do usuário, o custo virou uma seção própria antes das conclusões, a 13, "Custo do
    sistema". Ela tem o resumo por grupo, o critério, o que pesa (as DevKits são 33 %, a alimentação e as
    caixas 36 %, os sensores e componentes menos de um quinto) e o que mudaria em série. A tabela item por
    item ficou no Apêndice C, "Lista de materiais e custo". O resumo, a introdução (organização), a seção 6.7 e
    as conclusões citam o total. O arquivo das conclusões passou a `14_conclusoes.tex`;
  - **resultado:** hub R$ 288,92, pulseira R$ 274,92, kit R$ 563,84; as duas DevKits são 33 % do total.
- **Verificado:** os totais do script batem com a soma à mão; `pdflatex` sem erros, sem referência
  indefinida e sem *overfull* (71 páginas); a seção 13, o apêndice e o resumo conferidos na imagem. Os
  renders da PCB que o script refez (só os bytes mudaram) foram restaurados do commit.

### 2026-11-17 — fotos da PCB fabricada no relatório do Q2

- **Pedido:** pôr as fotos da PCB v1 (frente e costas) nos quadros reservados do relatório.
- **Feito:**
  - as fotos do usuário viraram `imagens/pcb_fabricada_cima.jpg` e `pcb_fabricada_baixo.jpg`, reduzidas a
    2000 px de largura e sem os metadados da câmera (EXIF);
  - a Figura 17 (seção 6.7) passou de duas fotos lado a lado para uma embaixo da outra, na largura do texto e
    com legenda própria, porque fotos deitadas pela metade da largura não deixavam ler a serigrafia. Ela
    flutua (`[tbp]`) para não deixar meia página em branco, e o texto passou a citá-la;
  - saíram da lista de fotos pendentes (`main.tex`, `relatorios/README.md`, `pendencias.md`);
  - a Figura 18 (a PCB do S1) estava pequena: a foto vertical tem mesa em cima e embaixo. Ela é recortada no
    próprio LaTeX (`trim` e `clip`, sem mexer no arquivo), fica quase quadrada e passou de 7,5 cm de altura
    para 75 % da largura do texto.
- **Verificado:** `pdflatex` sem erros, sem referência indefinida e sem *overfull* (69 páginas); as páginas das
  figuras 17 e 18 conferidas na imagem.

### 2026-11-17 — envio por mudança: temperatura e pulseira

- **Pedido:** os dados vão para um banco, e enviar a temperatura e a pulseira a cada 30 s enche o banco de
  repetições (2.880 pontos por dia de cada, por quarto).
- **Decidido com o usuário:** medir e enviar ficam separados. A temperatura é medida a cada 60 s e publicada
  quando muda 0,2 °C ou a cada 5 min; `pulseira/<id>` sai no primeiro heartbeat e depois como resumo a cada
  5 min. O porquê está em [`decisoes.md`](decisoes.md) (*Envio por mudança*).
- **Feito:**
  - **componente comum novo** `firmware/common/report`: a regra de envio (limiar contado do último valor
    enviado, intervalo máximo, `reset`) e o RMS do resumo, em C puro, com 23 verificações no PC. Seis erros
    plantados numa cópia foram todos pegos;
  - **contrato:** `MQTT_REPORT_MAX_INTERVAL_S` (300) e `MQTT_TEMP_REPORT_DELTA_C` (0,2) em `protocol_mqtt.h`;
    [`protocolo.md`](protocolo.md) com a seção *Envio por mudança*, os tópicos e o campo `atividade`;
  - **hub:** `LM35_PERIOD_MS` 60 s; a temperatura vai às regras toda vez e ao MQTT e ao CAN só quando o
    `report` deixa (a reconexão republica o último valor enviado); `pulseira/<id>` com o resumo de 5 min e a
    volta do offline publicada na hora;
  - **pulseira com MQTT:** a telemetria sai no primeiro heartbeat, a cada 5 min e quando o hub para ou volta
    a responder; o heartbeat ESP-NOW e o CAN de bancada não mudaram;
  - **central:** o simulador mede a cada 60 s e publica pela mesma regra (passo do passeio de 0,1 °C), com o
    resumo da pulseira a cada 5 min; a demo publica o primeiro resumo aos 30 s e depois a cada 5 min. Grafana e
    Telegraf não mudaram (`last()` e `aggregateWindow` aceitam pontos espaçados);
  - **documentos:** `requisitos.md` (RF10), `arquitetura.md`, `decisoes.md`, `roadmap.md`, os READMEs e o
    `CLAUDE.md`;
  - **relatório do Q2:** RF10, aquisição da temperatura (5.5), tasks e testes (7), CAN (8), tópicos e o
    parágrafo *Envio por mudança* (9.3), verificação e plano (12).
- **Verificado:**
  - build sem avisos do hub e da pulseira (padrão, demo e com MAC, MQTT, CAN e coleta);
  - testes de host: `report` 23, regras 82, `hub_config` 60, `fall_pipeline` 885, `signal_proc` 25,
    `button_gesture` 42; notificador 22 e demo 19 testes;
  - o simulador, com a publicação interceptada: 12 mensagens de temperatura em 1 h estável (eram 120) e 15 numa
    rampa de 3,6 °C em 1 h;
  - relatório: 68 páginas, sem erros, sem referência indefinida e sem *overfull*; a página dos tópicos
    conferida na imagem.
- **Não verificado:** a placa (o ruído da leitura parada contra os 0,2 °C) e a central no Docker, que estava
  parado. Os dois ficaram em [`pendencias.md`](pendencias.md).

### 2026-11-17 — gestos do botão: dois toques cancelam, segurar pede ajuda

- **Pedido:** o botão não pode cancelar uma queda por um aperto sem querer, e deve servir também para pedir ajuda
  quando não há alerta (a queda que o modelo não viu, ou outra situação), também sem brecha para acidente.
- **Decidido com o usuário:** dois toques rápidos cancelam ("estou bem"); segurar 3 s pede ajuda; o pedido
  de ajuda pode ser cancelado pelos mesmos dois toques nos 15 s seguintes. O porquê está em
  [`decisoes.md`](decisoes.md) (*Gestos do botão*).
- **Feito:**
  - **componente novo** `firmware/gadget/components/button_gesture`: lógica pura dos gestos (estabilização de
    30 ms, dois toques, segurar), com testes no PC (42 verificações). Quatro erros plantados de propósito numa
    cópia foram todos pegos pelos testes;
  - **pulseira:** task `button` (núcleo 1) acordada pela ISR (as duas bordas), que entrega cada gesto à `comm`
    com a hora em que aconteceu. A `comm` passou a tratar "alertas do botão" (queda e ajuda): `HELP` e
    `HELP_CANCEL`, reenvio e reserva por MQTT iguais aos da queda, cada tipo esperando no seu lugar. Saiu o
    bloqueio de 300 ms, que engoliria o segundo toque. Kconfig novos: `GADGET_HELP_HOLD_S` e
    `GADGET_DOUBLE_TAP_GAP_MS`; a queda simulada de teste passou a ser dois toques sem alerta;
  - **contrato:** `HELP` (`0x06`) e `HELP_CANCEL` (`0x07`) no ESP-NOW, sem mudar a versão; tipo `ajuda` no
    MQTT, com `id` `<pulseira>-<seq>` ([`protocolo.md`](protocolo.md));
  - **hub:** regras `rules_on_help`/`rules_on_help_cancel` (a queda e a ajuda dividem a lógica de episódio),
    `espnow_link` e `main.c`; 20 verificações novas nas regras;
  - **central:** o notificador escreve "Pedido de ajuda (botão da pulseira)"; o simulador ganhou o comando `a`;
    a demo repassa o botão da placa (segurar e dois toques) e mostra a faixa "PEDIDO DE AJUDA";
  - **documentos:** `protocolo.md`, `requisitos.md` (RF05 reescrito, RF20 novo), `arquitetura.md`,
    `decisoes.md`, `roadmap.md`, os READMEs da pulseira, da central e do hardware, e o `CLAUDE.md`;
  - **relatório do Q2:** requisitos, sensores (subseção do botão com a tabela de gestos e o porquê),
    condicionamento, firmware (task `button`, figura nova dos gestos, contagens dos testes), sem fio (mensagens
    e alerta `ajuda`), verificação (linha "Botão" no plano) e quatro diagramas.
- **Verificado:**
  - build sem avisos do hub e da pulseira (padrão, demo e com MAC, MQTT, CAN, coleta e queda simulada);
  - testes de host: `button_gesture` 42, regras 82, `hub_config` 60, `fall_pipeline` 885, `signal_proc` 25;
  - notificador 22 testes, demo 19;
  - o simulador publicou `ajuda` ativo e cancelado (sem broker, com a publicação interceptada);
  - uma janela da demo, tocando uma sessão gravada com um pedido de ajuda, mostrou a faixa nova;
  - relatório do Q2: 68 páginas, sem erros, sem referência indefinida e sem *overfull*; as páginas mudadas
    conferidas na imagem.
- **Não verificado:** nada disso rodou na placa. Os gestos entraram como item de bancada em
  [`pendencias.md`](pendencias.md) ("Pulseira gravada e conferida").

### 2026-11-17 — relatório do Q2 como relatório final

- **Pedido:** o professor considera o Q2 o relatório final, então o texto não deve se apresentar como "primeira
  interação" nem "primeira versão"; e as menções de que o sistema não foi verificado em bancada saem do
  relatório.
- **Feito** (só no relatório do Q2):
  - capa, contracapa e resumo dizem "relatório final"; a seção 1.3 virou "O que este relatório cobre", sem
    citar o enunciado nem os quadros de foto;
  - saíram as frases de "ainda não verificado/medido/conferido": resumo, introdução, LM35 (termopar), consumo,
    CAN, paridade na placa, painel, "O que ainda não foi verificado", a coluna "Nível alcançado", o parágrafo
    sobre o Q3 e, nas conclusões, os "falta ..." e duas limitações;
  - saíram também as menções a "compila sem avisos" (resumo, conclusões, a linha "build" dos níveis e as das
    tabelas da Seção 12): é o mínimo de um trabalho final, e a verificação passou a citar os testes;
  - corrigido um exagero: o CAN e a tabela de objetivos diziam que a versão atual foi "verificada no modo de
    bancada", mas o modo de bancada roda na placa, e o CAN ainda não rodou em placa. Agora o texto só descreve o
    modo de bancada e aponta o teste entre as placas na Seção 12;
  - o plano de testes continua.
- **Verificado:** `pdflatex` sem erros nem *overfull*; capa, introdução, verificação e conclusões conferidas na
  imagem.

### 2026-11-17 — "pulseira" no lugar de "vestível" em todo o projeto

- **Pedido:** trocar "vestível" por "pulseira" no relatório e no resto do projeto, e explicar logo no início o que é
  a pulseira e o que é o hub.
- **Feito:**
  - **texto** (78 arquivos):
    - a troca foi feita por script, acertando artigos e contrações ("do vestível" → "da pulseira");
    - a concordância e os pronomes ("pareado", "ele mesmo", "o hub o declara") foram revisados à mão, a partir
      de buscas pelos padrões que o script não resolve;
  - **contrato MQTT** ([`protocolo.md`](protocolo.md)):
    - tópicos `pulseira/<id>`, `.../telemetria` e `.../mqtt`;
    - alerta `pulseira_offline`, campo `pulseira` no `detalhe`, `"via":"pulseira"`;
    - parâmetro `pulseira_offline_s`;
    - usuário `pulseira` do broker e `MQTT_PULSEIRA_PASSWORD`;
    - medidas `pulseira*` no InfluxDB;
    - a mudança foi feita junto no firmware, no Telegraf, no Grafana, no notificador, no simulador e na demo;
  - **arquivos renomeados:** `demo_pulseira.py` (e o teste), `09_exportar_pulseira.py`, `11_svm_pulseira.py`,
    `pulseira.json`, `svm_pulseira.json` e três arquivos do relatório do Q2;
  - **regenerados:** o `fall_model.c`/`.h` pelo `09` (só o cabeçalho mudou; vetores e números iguais) e as
    figuras e tabelas do Q2 pelo `12`;
  - **convenção** no início do relatório do Q2 (caixa "Termos usados neste relatório"), do `CLAUDE.md`, do
    `README.md` e da [`arquitetura.md`](arquitetura.md); decisão em [`decisoes.md`](decisoes.md);
  - **arquivos locais, fora do git:** a chave da senha no `interface/.env` e o usuário MQTT nos `sdkconfig` da
    pulseira (o padrão e o da demo);
  - **ficaram como estavam:** os materiais entregues do Q1 e o `interface/legado/`; o código C segue em inglês
    (`gadget`, `wearable`).
- **Verificado:**
  - build sem avisos do hub e da pulseira (padrão, demo e com MAC, MQTT, CAN e coleta);
  - testes de host: regras 62, `hub_config` 60, `fall_pipeline` 885, `signal_proc` 25;
  - notificador 19, demo 15, `pytest` do modelo 30;
  - `docker compose config` com o `.env` novo;
  - relatório do Q2 sem erros nem *overfull*; a caixa de termos conferida na imagem;
  - nenhuma menção a "vestível" fora das notas de convenção, da decisão e do título de um slide entregue.
- **Não verificado:** a central de ponta a ponta com os tópicos novos, porque o Docker não estava rodando.

### 2026-11-16 — relatório do Q2 (primeira versão do relatório final)

- **Pedido:** o relatório do Q2 no esquema LaTeX do Q1 (pastas de figuras, imagens e tabelas), como primeira
  versão do relatório final. Tem o projeto inteiro, com figuras, gráficos e diagramas, e espaço para as fotos
  que ainda não existem.
- **Decidido com o usuário:**
  - os modelos entram num capítulo resumido, que cita o Q1;
  - para a bancada, só o plano de testes, sem tabelas de resultado vazias.
- **Feito:**
  - **onde está:** `relatorios/Semestre 2/Projeto 3/Q2/latex/`, com 66 páginas, 13 seções e 4 apêndices, um
    arquivo por seção em `secoes/` e 11 diagramas em `diagramas/`;
  - **o script** `modelo/experimentos/12_relatorio_q2.py`:
    - gráficos: LM35 com os 22 pontos do S1 P2 e os resíduos, faixa do ADC, Bode do RC, resposta do biquad e
      cenários do MPU;
    - cópias de figuras do Q1, a lista de materiais com a função de cada peça e a tabela dos classificadores;
    - `numeros.tex`;
    - pelo `kicad-cli` e pelo `pdftoppm`: renders 3D, camadas de cobre, cada folha do esquema recortada,
      ERC e DRC;
  - **fotos:** cinco reaproveitadas dos relatórios anteriores (bancada e protoboard do LM35, simulação SPICE,
    PCB do S1, CAN de 28/08). Para as 11 que faltam, a macro `\fotoouespaco` desenha um quadro com o nome do
    arquivo; a lista fica no topo do `main.tex`;
  - **tamanho dos firmwares**, medido para o relatório:
    - hub: 992 672 bytes, 35 % da partição livre;
    - pulseira com ESP-NOW (build de teste no scratchpad): 960 688 bytes, 37 %;
    - pulseira sem hub: 383 184 bytes, 75 %.
- **Verificado:**
  - asserts do script: o ajuste reproduz `lm35.h` e o biquad bate com o `fall_model.c`;
  - DRC: 0 erros, 0 sem ligação, 0 divergências; ERC: 0;
  - `pdflatex` sem erros, sem *overfull* e sem referência indefinida;
  - as páginas conferidas na imagem;
  - a macro das fotos testada com e sem arquivo.
- **Não verificado:** nada novo em placa. O relatório diz isso e separa o verificado do plano de testes.

### 2026-11-16 — pendências em dia: Q1 entregue, PCB em mãos, `aulas/` fora do git

- **Informado pelo grupo:**
  - o S2 P3 Q1 foi entregue e apresentado;
  - a PCB v1 foi entregue no S2 P2 (16/10), fabricada e está em mãos;
  - `aulas/` não entra no git.
- **Feito:**
  - `pendencias.md`:
    - nova seção "Entregas da disciplina", com o Q2 (17/11) e o Q3 (24/11), que não estavam na lista;
    - saíram o "primeiro commit" e o "antena fora de cobre", que a v1 já tem;
    - a conferência da ordem dos pinos passou para o primeiro passo da montagem;
    - entrou a falta de um remoto para o repositório e o fim de linha da cópia de trabalho;
  - `aulas/` no `.gitignore` e fora do índice (os arquivos continuam no disco); decisão em
    [`decisoes.md`](decisoes.md).
- **Verificado:** `git check-ignore aulas/README.md` e os 18 arquivos ainda na pasta.

### 2026-11-13 — erros não tratados: revisão de código e correções

- **Pedido:** revisar o firmware, a interface e os scripts do `modelo/` em busca de erros não tratados e
  corrigir tudo, na ordem do relatório.
- **Feito:** os 32 achados e os 6 menores. O que mudou de comportamento e por quê está em
  [`decisoes.md`](decisoes.md) (2026-11-13), e o contrato em [`protocolo.md`](protocolo.md) §3.
  - **Quedas que se perdiam:**
    - a regra do hub reconhece a queda por `seq` e `uptime_ms`;
    - o `seq` da pulseira começa aleatório;
    - a FALL não entregue é reenviada até passar;
    - o MPU que reinicia ou o barramento I²C preso deixam de esconder a falta de detecção;
    - o notificador (sessão persistente, envio que insiste, alerta esquisito vira texto).
  - **Configuração errada:** chaves e MAC com tamanho errado falham o build; ESP-NOW e MQTT que não sobem dão um
    erro e a placa segue.
  - **Scripts do modelo:** o formato da coleta no pulso, a validação dos dados, o cache, a exportação que só grava
    no fim e uma fonte só para o gatilho e o filtro.
- **Ficou para decidir:** tirar do treino as janelas que passam da borda dos ensaios (item 19). O efeito medido
  e o que precisaria ser regenerado estão em [`pendencias.md`](pendencias.md) (Firmware).
- **Verificado:** builds sem avisos (pulseira padrão, demo e completo; hub), o build falhando com um MAC
  malformado, todos os testes de host e de Python (com casos novos para cada correção testável no PC). O `09`, o
  `04` e o `08` dão os mesmos resultados.
- **Não verificado:** nada em placa. A lista do que conferir está em [`pendencias.md`](pendencias.md).

### 2026-11-13 — demo na bancada: o gráfico congelava quando a USB piscava

- **Pedido:** a janela do `demo_pulseira.py` parou de mostrar pontos novos depois de um tempo. Era o Python ou a
  ESP?
- **Investigado (com a janela travada):**
  - a COM3 abriu, então o script já tinha soltado a porta;
  - a placa seguia mandando CSV a 200 Hz, com o tempo contínuo (~16 min, sem reinício);
  - o `Traceback` no terminal: `SerialException: GetOverlappedResult failed (Acesso negado)` no
    `conexao.readline()`. O link USB do conversor CH343 piscou, e a thread de leitura morreu sem avisar a janela;
  - descartados: a tarefa da ESP parar, o modo de seleção do terminal (sem `--mqtt-host` não há `print`) e a porta
    sumir num reinício (a placa não reiniciou).
- **Encontrado no caminho:**
  - uma linha CSV com um tempo absurdo (bytes perdidos) fazia o sinal do gráfico crescer sem limite;
  - uma linha de boot emendada derrubava a leitura (`ValueError`), achada num teste com 200 mil linhas estragadas.
- **Corrigido:**
  - a leitura reabre a porta sozinha, sem resetar a placa, e o título mostra a perda;
  - linhas com erro são ignoradas;
  - o sinal do gráfico tem um teto em pontos.
- **Verificado:**
  - 5 testes novos, que falhavam antes da correção (14 no total, todos passam);
  - estresse de 30 min simulados e o teste com linhas estragadas;
  - a leitura da placa real na COM3.
  - A reconexão com a placa real não foi testada: falta puxar o cabo.
- **Primeiro dado de bancada do firmware:** 200 Hz contínuos e \|a\| ≈ 1 g parado (≈ 4 077 contagens a 4 096/g,
  a faixa de ±8 g).

### 2026-11-13 — apresentação do Q1 enxugada (22 slides)

- **Pedido:** tirar a explicação do filtro (ele fica só no slide do pipeline), a impureza de Gini, o tópico
  "Paridade C × Python" e deixar o último slide só com "Obrigado".
- **Feito:**
  - saíram os 3 slides do filtro e o slide "como a árvore escolhe as perguntas" (Gini e pesos das classes);
  - os slides do Random Forest passaram a ser numerados de 1/5 a 5/5;
  - o `main.tex` passou para LF.
  - O roteiro foi renumerado e o guia dos modelos aponta os novos números. O Gini continua no guia, para o caso
    de perguntarem.
  - O `main.pdf` versionado foi atualizado: até aqui ele era o anterior às mudanças do dia (sem a nota da
    floresta da comparação e com "104 KB").
- **Verificado:** `pdflatex` sem erros nem *overfull*; 22 slides; os slides 5, 12, 20 e 22 conferidos na imagem.

### 2026-11-13 — guia dos modelos para a apresentação

- **Pedido:** um arquivo que explique como funciona cada modelo testado, para preparar a apresentação.
- **Feito:** [`relatorios/Semestre 2/Projeto 3/Q1/explicacao-modelos.md`](../relatorios/Semestre%202/Projeto%203/Q1/explicacao-modelos.md)
  (indexado em `relatorios/README.md`):
  - o que todos os métodos têm em comum (pipeline, atributos, como medimos);
  - cada um dos 11 métodos da tabela do slide 12: ideia, como foi montado, resultado e por que deu esse
    resultado;
  - o Random Forest a fundo, perguntas prováveis com respostas e um glossário.
- O roteiro dizia "60 árvores (104 KB)" no slide 24; passou a 106 kB, como os slides.
- **Verificado:** os parâmetros contra `modelo/queda/modelos.py` e `cnn.py`; os números contra
  `resultados/resumo.md`, `pulseira.json`, `svm_pulseira.json` e o `numeros.tex` da apresentação.

### 2026-11-13 — SVM na configuração da pulseira; tamanho do Random Forest nos slides

- **Pedido:**
  - conferir os 252 kB do Random Forest nos slides;
  - explicar por que não o SVM, que foi melhor nos nossos ensaios e é mais leve, rodando-o na configuração da
    pulseira.
- **Encontrado:**
  - os slides misturavam duas florestas. A da comparação (150 árvores de profundidade 12) ocupa 252 kB. A da
    pulseira (60 × 10) ocupa 106 kB, medidos no `gadget.elf` (`fall_rf_nodes`: 106 248 bytes, mais 120 das
    raízes) e mostrados no slide como "104 KB", em outra unidade;
  - o SVM nunca tinha rodado na configuração da pulseira, e o `decisoes.md` não dizia por que ele ficou de fora.
- **Feito:**
  - nota na tabela "Por que escolhemos o Random Forest" e kB nos dois slides (gerador `10` e `main.tex`);
  - `modelo/experimentos/11_svm_pulseira.py` e `resultados/svm_pulseira.json`;
  - [`decisoes.md`](decisoes.md) (2026-11-13), `modelo/README.md` e [`pendencias.md`](pendencias.md) (item do
    retreino).
- **Resultado (SisFall, ±8 g, gatilho causal, 23 atributos):**
  - Random Forest 60 × 10: 0,55 alarme/h, 95,1 % das quedas com ≤ 1 alarme/h;
  - SVM com todas as janelas: 0,69 alarme/h e 96,2 %, com 34 kB, mas ~24 mil operações e 348 exponenciais por
    janela;
  - regressão logística: 0,62 alarme/h e 94,9 %;
  - no bootstrap por pessoa, todos os intervalos de 95 % das diferenças passam pelo zero: os três empatam;
  - nos nossos ensaios, a vantagem do SVM (8 contra 7 de 10 quedas) é uma queda de joelhos.
- **Verificado:**
  - o `11` reproduz o limiar do Random Forest do `09`;
  - o `10` gerou as 15 figuras idênticas byte a byte; só mudaram a tabela e o número da pulseira;
  - `pdflatex` da apresentação sem erros nem *overfull*; os slides 21 e 24 conferidos na imagem.

### 2026-11-11 — demonstração com uma placa e peças ausentes sem erro

- **Pedidos:**
  - mostrar o modelo funcionando com uma ESP só, com o MPU-6050, ligada ao computador, de preferência mandando a
    queda à interface;
  - nas duas placas, seguir funcionando sem CAN, sem um sensor ou sem rede, com um aviso em vez de erros.
- **Encontrado no código:**
  - a pulseira sem MAC do hub, ou sem o MPU-6050, reiniciava em laço;
  - CAN sem barramento dava um E a cada ~1 s;
  - o cliente MQTT tentava mesmo sem Wi-Fi, com erros da biblioteca a cada ~10 s;
  - PIR e portas soltos geravam alertas falsos (inatividade, saída noturna).
- **Pulseira:**
  - modo sem hub (MAC vazio);
  - sem MPU segue rodando, com LED de falha, nova tentativa a cada 5 s e sem heartbeat;
  - hub fora do alcance dá um aviso só;
  - perfil `sdkconfig.demo.defaults`: coleta, 5 s de espera, console UART "custom" a 921600.
- **Hub:**
  - menu "Room hub: sensors";
  - `rules_set_presence_fitted` suspende inatividade e saída noturna;
  - o LM35 solto dá um aviso por transição.
- **Comuns:**
  - `net`: um aviso por queda, com o motivo;
  - `mqtt_link`: começa só com a rede de pé e silencia a biblioteca;
  - `can`: um aviso para bus-off e outro para "error passive", com recuperação de 1 s a 60 s;
  - `indicators`: `IND_STATUS_FAULT`;
  - `sys_events`: `SYS_EV_IMU_OK`.
- **Central:**
  - `interface/demo_pulseira.py`: lê CSV e FEAT da serial, mostra o \|a\| filtrado, os disparos com P(queda) e a
    faixa de queda, e com `--mqtt-host` publica `status`, `pulseira/<id>` e o alerta como o hub;
  - `--gravar` e `--reproduzir` para ensaiar sem a placa;
  - a tabela de alertas do Grafana ganhou a coluna P(queda).
- **Verificado no PC:**
  - os builds e os testes listados em `pendencias.md` (Concluído);
  - a janela renderizada a partir de uma sessão sintética;
  - a ponte contra o Mosquitto local, com as mensagens conferidas por `mosquitto_sub`.
- **Não verificado:**
  - nada em placa;
  - o caminho até o Grafana, porque o Docker não estava no ar.

  Ficam para o ensaio da demonstração (`pendencias.md`).

### 2026-11-11 — apresentação do Q1 em Beamer

- **Pedido:** apresentação do relatório do Q1, com:
  - os modelos testados (visão geral), o pipeline e os resultados, com figuras;
  - um sinal de exemplo antes e depois do filtro;
  - o Random Forest explicado a fundo.

  Sem o limiar antigo como "problema": o limiar aparece só como o gatilho do pipeline.
- **Feito:** `relatorios/Semestre 2/Projeto 3/Q1/apresentacao/`, em Beamer 16:9 com 26 slides e a identidade do
  relatório.
  - As figuras ficam em tamanho de slide, geradas por `modelo/experimentos/10_figuras_apresentacao.py`.
  - As tabelas e os números (`numeros.tex`) são gerados pelo mesmo script: nenhum é digitado à mão.
  - O `roteiro-apresentacao.md` foi reescrito para seguir os slides.
  - O estilo das figuras passou para `experimentos/_estilo.py`, usado pelo `07` e pelo `10`.
- **Verificado:**
  - o `07` gera as mesmas figuras, byte a byte, depois de extrair o estilo;
  - o script 10 confere que o Random Forest que ele explica é o da pulseira (limiar e vetores de teste,
    \|ΔP\| = 5·10⁻⁷);
  - `pdflatex` sem erros nem *overfull*;
  - os 26 slides foram renderizados (`pdftoppm`) e conferidos um a um.

### 2026-11-11 — pipeline de queda na pulseira

- **Pedido:** registrar a escolha do modelo e do gatilho e levar o pipeline ao firmware, com o envio da
  queda pelo CAN e pelo MQTT.
- **Antes de portar, uma comparação no PC** (SisFall, cenário A, validação por pessoa):
  - o gatilho causal (o que roda amostra a amostra) deixa passar as mesmas 99,2 % das quedas que o
    `find_peaks` e dispara menos: 753 contra 858 por hora;
  - só os 23 atributos no tempo bastam:
    - logística: 0,62 alarme/h (0,55 com todos);
    - Random Forest de 60 × 10: 0,55 (0,34 com todos).
- **Python:**
  - `pipeline.gatilho_causal` e `montar(..., causal=True)`, com 3 testes novos;
  - `experimentos/09_exportar_pulseira.py` treina os dois classificadores e gera o modelo em C e os vetores de
    teste;
  - `resultados/pulseira.json` guarda as métricas e os limiares.
- **Firmware da pulseira:**
  - componente `fall_pipeline`:
    - biquad do ESP-DSP por eixo;
    - gatilho;
    - um buffer circular por eixo;
    - atributos;
    - Random Forest e logística;
    - custo em ciclos com `portMUX`;
  - `Kconfig`:
    - `GADGET_FALL_TRIGGER_CG` (200);
    - escolha do classificador;
    - os limiares L<sub>on</sub>/L<sub>off</sub> da histerese saíram;
  - `fall_detect` e `sp_features_compute_timed` removidos;
  - a linha `FEAT:` passa a ter os 23 atributos, P e os ciclos.
- **Contrato** ([`protocolo.md`](protocolo.md)): P(queda) no `FALL` (13 bytes), no CAN `0x0FD` (3 bytes) e
  `"prob"` no `detalhe` do alerta.
  - O hub (`espnow_link`, regras, publicação e log do CAN) foi atualizado junto.
  - O notificador mostra a confiança do modelo, e o simulador publica `prob`.
- **Verificado:**
  - testes de host do `fall_pipeline` (885 verificações):
    - o gatilho em sinais sintéticos;
    - o filtro partindo do repouso;
    - a janela logo depois do boot;
    - os classificadores sobre os atributos do Python (\|ΔP\| < 10⁻⁶);
    - o pipeline inteiro em 8 trechos da SisFall: 26 janelas, mesmos picos, atributos a ≤ 3 % da tolerância;
  - regras do hub (54), `hub_config` (60), `signal_proc` (25), notificador (12) e `pytest` do `modelo/` (16);
  - build sem avisos:
    - pulseira padrão: 38 % da partição livre, `s_pipeline` com 20 KB de RAM, floresta com 104 KB de flash;
    - pulseira com MQTT + CAN + coleta + logística;
    - hub.
- **Não verificado:**
  - nada rodou em placa;
  - o custo em ciclos e a decisão na placa ficam para a bancada ([`pendencias.md`](pendencias.md)).

### 2026-11-10 — esquema SQLite da atividade de Fundamentos de Software

- **Onde:** fora deste repositório, em `C:\Users\henri\dev\tarefa python sql` (repositório do grupo).
- **Entrega:** notebook com SQLite, classes, DAOs, Faker e JOINs, sobre o banco deste projeto. Serve de
  protótipo para o banco relacional da central.
- **Revisão do esquema do grupo:**
  - `quarto` com chave artificial + `codigo` do tópico MQTT;
  - `ilpi.site`;
  - histórico em períodos, com índice parcial único;
  - alerta com estado, fim e cuidador;
  - tabelas `STRICT` e `CHECK` de datas com `IS date()`. Com `=`, uma data inválida passava, porque `date()`
    devolve NULL.
- **Correções nos módulos do grupo:**
  - conflito de merge;
  - nomes de tabela e de coluna errados;
  - `BancoSQLite` sem importar;
  - recursão infinita em `EventoSensor.id_evento`;
  - `faker.py` encobrindo a biblioteca.
- **Notebook:** gerado dos módulos por `gerar_notebook.py`.
- **Verificação:**
  - o notebook rodou duas vezes seguidas sem erro, com os `assert`s de CRUD, injection, integridade e JOIN ×
    Python, `foreign_key_check` vazio e `integrity_check` = ok;
  - o roteiro também roda no Python 3.12 (o do Colab).
- **Pendências:** as decisões para o PostgreSQL estão no item "Banco relacional" de `pendencias.md`.

### 2026-11-10 — cascata de modelos × modelo único (teste rápido)

- **Pergunta do usuário:** vale pôr modelos em cascata ou é melhor escolher um?
- **Teste** com os escores fora da dobra do cenário A (validação por pessoa), num script só no scratchpad.
  Quedas detectadas com ≤ 1 alarme/h:

  | Variante | Resultado |
  |---|---|
  | Random Forest sozinho | 95,7 % |
  | cascata "E" de LR + RF | 96,3 % |
  | cascata "E" de LR + SVM + RF | 96,9 % |
  | média de LR + SVM + RF + CNN | 98,1 % |
  | cascata "OU" | 94,0 % (piora) |

  As combinações foram escolhidas nos mesmos dados, então os números são otimistas.
- **Cascata por custo** (LR permissivo → RF): o LR deixa só 3,2 % das janelas de atividade chegarem ao RF, sem
  perda. No ESP32 isso não é necessário: o classificador roda ~860 vezes por hora no pior caso.
- **Janela mais longa depois do impacto** (+2 s em vez de +0,8 s): AUC do RF de 0,9987 para 0,9996. A regressão
  logística vai de 92,8 % para 95,3 % das quedas com ≤ 1 alarme/h.
- **Conclusão:** manter um classificador por vez (o pipeline já é uma cascata gatilho → classificador) e testar
  a janela mais longa quando houver dados do pulso. Registrado em `pendencias.md`, no item do pipeline.

### 2026-11-10 — relatório do Q1 em LaTeX

- **Onde:** `relatorios/Semestre 2/Projeto 3/Q1/latex/`, com `main.tex` e as pastas `figuras/`, `imagens/` e
  `tabelas/`.
- **Layout:** o do relatório de Numéricos do usuário: capa com o logo da Poli e os três integrantes, contracapa,
  resumo, sumário, seções em azul e cabeçalho "EPUSP - Projeto Integrativo 3".
- **Conteúdo:** introdução (binário × caracterizador), dados, pipeline, modelos (com as equações de
  correlação, DTW, AR/ARX, mínimos quadrados e AIC/BIC), avaliação e os 13 testes, resultados, discussão,
  conclusões, limitações, referências e apêndice de reprodução.
- **Figuras e tabelas:** 19 figuras e 9 tabelas. As 6 tabelas de resultados saem dos CSVs por
  `modelo/experimentos/08_relatorio_latex.py`, que também copia as figuras.
- **Verificação:**
  - `pdflatex` duas vezes: 26 páginas, sem erro, aviso ou linha estourada (corrigidos `headheight`, a ordem
    do `tocloft` e três tabelas largas);
  - páginas renderizadas e conferidas.
- **Falta:** os docentes do S2 não estão em `relatorios/README.md`. A linha deles na contracapa aparece quando
  o macro `\docentes` for preenchido.

### 2026-11-10 — figuras explicativas do Q1

- **Script novo**, `modelo/experimentos/07_figuras_explicativas.py`, com 11 figuras em `modelo/resultados/`:
  - saturação em ±16, ±8 e ±2 g;
  - funil gatilho → classificador;
  - escores de quedas × atividades em seis métodos;
  - separação por pico, quietude e inclinação;
  - importância e ablação do Random Forest;
  - AR(12) prevendo e errando;
  - templates da correlação;
  - espectro (FFT);
  - erros por tipo;
  - os 18 ensaios próprios: sinais e matriz por método.
- **Tabela nova** `ensaios_com_alarme_A.csv`: o limiar atual dá alarme em 97–100 % dos ensaios de correr,
  pular, tropeçar e subir escada rápido; o Random Forest, em nenhum.
- **Relatório e roteiro** do Q1 passam a apontar para as figuras.
- **Verificação:**
  - os escores salvos correspondem às janelas refeitas (assert no script);
  - o Random Forest do cenário B refeito confere com `proprios_por_arquivo.csv`, arquivo a arquivo;
  - os totais da matriz batem com `proprios_resumo.csv`;
  - `pytest`: 13 passando;
  - todas as figuras abertas e conferidas (sem texto cortado ou sobreposto).

### 2026-11-10 — banco relacional da central: modelagem

- **Decisão do grupo:** banco relacional em Python + `sqlite3`, integrado à interface. Antes, o plano era
  PostgreSQL no futuro. Por enquanto só ideias e arquitetura, sem código.
- **Revisão do diagrama ER do usuário** (`idoso`, `quarto`, `historico_quedas`, `historico_quartos`,
  `historico_sensores`):
  - FKs explícitas no lado "muitos", com cardinalidade;
  - `historico_quedas` vira `alerta`, para os 7 tipos do `protocolo.md`, com uma tabela de transições;
  - `historico_sensores` vira `sensor` + `evento_sensor`, porque cada porta é um sensor e as leituras chegam
    em horas diferentes;
  - entram `pulseira` (liga a queda ao idoso) e, opcional, `cuidador`.
- **Verificação:** nenhuma; é proposta. O que falta decidir está em [`pendencias.md`](pendencias.md) (Central).

### 2026-11-09 — modelos de queda no PC (S2 P3 Q1)

- **Pastas criadas a pedido do usuário:** `dados/` (fora do git, com README) e `modelo/` (Python 3.12 via `uv`).
- **Verificação dos dados**, antes de escrever o código:
  - SisFall com 4505 arquivos íntegros, 38 pessoas; os ensaios D17 da SA15 estão com o nome D17_SE15;
  - os 18 ensaios próprios são de uma pessoa, na cintura, em ±2 g e saturados, com tempo de resolução de 10 ms
    e curtos.
  - O plano foi revisto com isso: dois cenários de faixa (±8 g e ±2 g) e janela de −1,0 s a +0,8 s.
- **Pipeline:** filtro → gatilho → janela → atributos → classificador.
  - A primeira versão do gatilho (histerese + período refratário, como o `fall_detect.c`) só deixava passar
    ~93 % das quedas: um disparo da caminhada escondia o impacto. Trocada por detecção de picos (≥ 0,5 s
    entre picos): 99,2 % com L_on = 2,0 g.
- **22 métodos comparados** com validação cruzada por pessoa (números em `modelo/resultados/resumo.md`):
  - limiar atual: 69,9 alarmes falsos/h (±8 g);
  - Random Forest: 0,4/h; regressão logística: 1,4/h; ambos com ~94 % das quedas;
  - correlação, DTW e AR como anomalia ficam atrás;
  - a CNN só com os módulos fica em 59 % das quedas com ≤ 1 alarme/h; com os 6 eixos, 94 %.
- **Nos ensaios próprios:** RF, regressão logística e SVM dão 0 alarmes falsos e perdem as quedas de joelhos.
- **Como foi verificado:**
  - 13 testes de unidade, com os exemplos numéricos dos slides (AR(2) → a₁ = 2 e a₂ = −1; DTW = 0) e o gatilho
    vetorizado igual ao laço do firmware em 50 sinais aleatórios;
  - todos os scripts rodaram do início ao fim;
  - `assert` de que nenhuma pessoa fica no treino e no teste.
  - É verificação no PC; **nada disso roda na pulseira ainda**.
- **Rascunho do relatório e roteiro da apresentação** em `relatorios/Semestre 2/Projeto 3/Q1/`.
- **Documentação atualizada:** decisões, roadmap, coleta, pendências, CLAUDE.md e os índices de aulas e
  relatórios.

### 2026-10-23 — arquivos de fabricação da PCB v1

- Gerados pelo `kicad-cli` em `hardware/placas/v1/`: Gerbers, furação (Excellon, mm), `placa-v1-jlcpcb.zip`,
  esquema em PDF, lista de materiais e render 3D, com um README que traz o pedido na JLCPCB.
- Verificado antes de gerar: DRC com 0 erros, 0 ligações faltando e 0 divergências com o esquema; ERC 0.
  Verificado nos arquivos gerados: contorno de 100,0 × 59,8 mm; 114 furos metalizados, nas quantidades que os
  footprints pedem. O teste no papel foi feito pelo usuário.
- Falta conferir a ordem dos pinos da DevKit contra a peça e fazer o pedido (`pendencias.md`).

### 2026-10-23 — PCB roteada

- O usuário terminou o roteamento. Conferido pelo `kicad-cli` no arquivo salvo e numa cópia com o plano
  repreenchido: DRC com 0 erros e 0 ligações faltando, sem divergência com o esquema; tudo no F.Cu (uma via, a
  do TP3), plano de GND inteiro, larguras como as classes de rede. Sobram avisos de serigrafia e um toco de
  trilha. O CANH passa a 0,9 mm do pino 2 do LM358 (anotado como opcional em `pendencias.md`).
- Gerado um PDF 1:1 da face de cima para o teste no papel (no scratchpad, fora do repositório).

### 2026-10-23 — plano de GND e área da antena

- Área proibida "Antena" (F.Cu e B.Cu, sem trilhas, vias e zonas) e plano de GND no B.Cu, com alívio térmico.
  Placa reduzida para 100,0 × 58,2 mm.
- DRC: os itens sem ligação caíram de 66 para 42, porque o plano ligou os pads de GND. Sobram o TP3 (pad só
  de F.Cu, precisa de via) e um `starved_thermal` no pino 44 da DevKit (a área da antena encosta nele; recuar
  a borda para X 154,5). Os 3 avisos de serigrafia do C7 continuam.
- No roteamento, as trilhas não passavam entre os pinos da DevKit (0,84 mm livres para 0,9 mm necessários).
  Decidido: pads ovais de 1,4 × 2,2 mm, afastamento `Power` em 0,25 mm e trilha de 0,25 mm predefinida.

### 2026-10-23 — fabricação na JLCPCB

- Decidido: JLCPCB, 2 camadas com furo metalizado. Regras de projeto, classes de rede e padrões de serigrafia
  propostos em `hardware/README.md` (Fabricação) e registrados em `decisoes.md`.
- Conferi os anéis de todos os pads da placa: dois footprints ficam fora do recomendado (LM35 com 0,15 mm;
  DevKit com furo de 0,8 mm). As trocas estão em `pendencias.md`.
- O usuário aplicou as trocas e as regras. Conferido pelo arquivo e pelo `kicad-cli`: DRC sem erros, sem
  divergência com o esquema; LM35 no `TO-92_Inline_Wide`, DevKit com furo de 1,0 mm, anel mínimo de 0,30 mm;
  classes de rede e restrições como propostas. Ainda sem plano de GND nem áreas proibidas.

### 2026-10-23 — posicionamento da PCB conferido

- Li o `.kicad_pcb` (placa de 108 × 64,5 mm, 30 footprints, sem trilhas). O que falta corrigir está em
  `pendencias.md` (Hardware): TPs fora da placa, área proibida sob a antena e no LM35, RC longe do GPIO 4, par
  do CAN passando pelo bloco analógico, e as fileiras da DevKit com 22,86 mm no footprint, a medir.
- Explicado como criar o plano de GND (zona no B.Cu, rede GND, alívio térmico).
- Segunda conferência, depois das correções do usuário: DRC pelo `kicad-cli` com paridade de esquema. Restam
  courtyards malformados no J1 e no J5, os valores dos TPs fora de sincronia com o esquema e ajustes de
  posição; tudo em `pendencias.md`. A DevKit foi medida com ~25,4 mm entre as fileiras.
- Terceira conferência: DRC sem erros (courtyards corrigidos, nenhuma sobreposição). Restam avisos: J1, J2, J5
  e J6 diferentes da biblioteca, valores dos TPs fora de sincronia com o esquema. Os 25,25 mm do footprint da
  DevKit foram aceitos pelo usuário.
- Quarta conferência: o footprint da DevKit foi para o `projeto.pretty` e está associado ao U1. Os courtyards
  da biblioteca ficaram abertos (sobraram linhas do soquete): removi essas linhas dos quatro footprints, com
  cópia de segurança, e conferi que a biblioteca abre no `kicad-cli`. Falta atualizar os quatro na placa e
  acertar os valores de TP1–TP4.
- Quinta conferência: a biblioteca continua certa, mas a placa ainda tem as cópias antigas de J1, J2, J5 e J6
  (7 erros de courtyard). TP1–TP4 sincronizados; TP5/TP6 ainda diferem do esquema.
- Sexta conferência: paridade com o esquema zerada (TPs sincronizados). O editor de footprint regravou o
  WCMCU-230 e o MPU6050 com linhas abertas; corrigi de novo. A placa (aberta no KiCad, com alterações não
  salvas) ainda tem as cópias antigas de J1, J2, J5 e J6.
- Com o KiCad fechado, tirei as linhas soltas de courtyard do J1 e do J5 direto no `.kicad_pcb` (cópia de
  segurança no scratchpad; J2 e J6 já estavam atualizados). DRC: 0 erros, 0 divergências com o esquema e com a
  biblioteca; só 4 avisos de serigrafia na borda. O posicionamento está fechado; próximo passo: plano de GND,
  áreas proibidas e roteamento.

### 2026-10-23 — posição do bloco analógico no layout

- `hardware/README.md`: o LM358 fica junto ao LM35, longe da DevKit (calor), e não junto ao GPIO 4. Só o RC
  fica no GPIO 4; a trilha que vai até ele leva o sinal já amplificado. Só documentação.
- Posição do LM35: numa ponta da borda dos conectores, longe do módulo e embaixo se a placa ficar em pé; recorte
  na placa, GND por trilha (sem plano em volta) e terminais longos, porque no TO-92 o calor chega pelos
  terminais.
- Posição do PIR: encaixado na placa (J2), na metade oposta à antena (disparo falso com Wi-Fi), sem componentes
  embaixo, domo saindo por um furo na tampa. O PIR saiu da lista de conectores com RC/TVS (não chega por fio).

### 2026-10-23 — I²C: troca feita e desfeita

- SCL e SDA chegaram a ser trocados (SCL no 1, SDA no 2), mas a troca foi desfeita no mesmo dia: ela tirava a
  opção do ULP ler o MPU-6050 em *deep sleep*.
- O cruzamento das trilhas até o GY-521 vai ser resolvido girando o módulo no layout.
- Continua SDA no GPIO1 e SCL no GPIO2, no firmware e no esquema. Verificado por build das duas placas e,
  no esquema, pelo netlist exportado (`J5.4`–GPIO1 SDA, `J5.3`–GPIO2 SCL) e pelo ERC (0 violações).

### 2026-10-23 — esquemático e sem buzzer

- **Esquemático da PCB única em andamento:** DevKit, LM35 + LM358, MPU-6050, HC-SR501, 2 portas, CAN
  (WCMCU-230), botão e entrada USB (10 µF + Schottky até a DevKit).
- **Sem buzzer:**
  - a sinalização local é o LED, e o alerta chega ao cuidador pela central;
  - `PIN_BUZZER = GPIO_NUM_NC` nas duas placas, e o GPIO 21 fica livre;
  - RF12, arquitetura, protocolo e hardware atualizados;
  - verificado por build das duas placas.
- **O ramo de 5 V filtrado do LM35 foi dispensado:** ficou só o desacoplamento de 100 nF.
- **Alimentação:**
  - entrada USB-C única (só com cabo A → C, o módulo não tem os resistores de CC), com 10 µF e Schottky até a
    DevKit;
  - a pulseira roda com **power bank** na mesma entrada. O circuito de bateria na placa (LiPo + TP4056 + boost)
    foi avaliado e descartado nesta versão, por tempo e tamanho;
  - firmware: `GADGET_BATTERY_SENSE` passou a vir desligado (verificado por build).

### 2026-10-21 — condicionamento do LM35

- **Documentação corrigida:** o LM35 já era alimentado com 5 V (a Tabela 4 do S1 P3 estava errada), e o
  LM358 com ±5 V, por um inversor.
- **Decisão para a PCB nova:**
  - sem o inversor: o LM358 em 5 V e GND;
  - ~~ramo de 5 V analógico filtrado~~ (dispensado em 2026-10-23: só 100 nF de desacoplamento);
  - RC de 10 kΩ + 1 µF no GPIO 4 (passa-baixa de ~16 Hz, que também protege o pino);
  - o firmware e a curva continuam; na placa, conferir 3 ou 4 pontos;
  - detalhes em [`hardware/README.md`](../hardware/README.md).

### 2026-10-21 — lista de pendências

- Criada a [`pendencias.md`](pendencias.md): a lista única do que falta, com caixas de marcar.
- As pendências e os próximos passos que estavam aqui foram para lá.
- Regra nova no `CLAUDE.md`: atualizar a lista ao fim de toda tarefa.

### 2026-10-20 (noite) — mapa de pinos da PCB única

- **Decisão:** no primeiro projeto, uma só PCB para o hub e a pulseira; só o firmware muda.
- **Mapa:** 16 sinais sem conflito, escolhidos pela aula de PCB e pela documentação do ESP-IDF v6.0.1
  ([`hardware/README.md`](../hardware/README.md), com as regras e as recomendações de esquemático). Ficaram
  livres 6 e 7 (analógico) e 39–42 (expansão); 15 e 16 ficam reservados para o cristal de 32 kHz.
- **Código:**
  - novo componente `firmware/common/pcb` (`pcb_pins.h`), com o menu PCB para a versão da DevKit;
  - os `board_pins.h` do hub e da pulseira passaram a escolher a partir dele.
- **Mudança de pinos:**

  | Placa | Sinal | Antes → agora |
  |---|---|---|
  | hub | PIR | 17 → 8 |
  | hub | portas | 18/16 → 11/12 |
  | hub | LM35 | 1 → 4 |
  | hub | CAN | 4/5 → 13/14 |
  | pulseira | I²C | 8/9 → 1/2 |
  | pulseira | INT | 10 → 17 |
  | pulseira | botão | 11 → 18 |
  | pulseira | buzzer | 12 → 21 |
  | pulseira | bateria | 2 → 5 |
  | pulseira | CAN | 4/5 → 13/14 |

- **Verificação:**
  - build sem avisos: hub, pulseira padrão e pulseira com MQTT + CAN + coleta;
  - a verificação do mapa compilada com o gcc do PC: o original passa, e cada erro é recusado com a mensagem
    certa:
    - pino repetido;
    - pino de *strapping*;
    - pino do cristal de 32 kHz;
    - LM35 no ADC2;
    - botão no JTAG (não acorda do *deep sleep*);
    - SDA fora do par do ULP.
- **`relatorios/`:** 7 PDFs tinham sumido. 4 voltaram de cópias nas pastas de cada semestre, conferidas pelo
  número de páginas. 3 precisam ser baixados de novo (Pendências).

### 2026-10-20 (tarde) — firmware nos moldes das aulas; MQTT e CAN nas duas placas; ESP-DSP

- **`aulas/`:** slides do S1 e do S2 organizados por semestre, `anotacoes.md` e `material/` removidos,
  `README.md` virou o índice dos slides (assunto e onde cada um se aplica). 11 slides apagados por engano
  foram recuperados das pastas de aula de cada semestre, fora do repositório; `CEM_Projeto_PCB.pdf` não
  tinha cópia.
- **Componentes comuns** (`firmware/common/`):
  - `net`, `mqtt_link` e `can` saíram do hub, com pinos e tópicos passados pela placa;
  - novos: `rtos_util` (`task_config.h` com as prioridades nomeadas do slide de RTOS) e `sys_events` (event
    group);
  - `protocol_can.h`: nibbles e payloads do grupo `0xF`;
  - menus genéricos: Network (Wi-Fi), MQTT, CAN bus (TWAI).
- **Revisão contra os slides:**
  - toda task com `xTaskCreatePinnedToCore` e prioridade nomeada (núcleo 1: sensores e amostragem; núcleo 0:
    rádio e protocolos);
  - estado de rede, MQTT, hora e CAN no event group;
  - filtro de glitch nas portas, no PIR e no botão;
  - `portMUX` nos indicadores.
- **CAN reescrito:**
  - fila de TX e task dona do periférico (antes só uma task podia transmitir);
  - recuperação de *bus-off*, retransmissão até o ACK, filtro de hardware para IDs `0x0GS`.
- **Pulseira:**
  - MQTT opcional: telemetria própria e reserva da queda;
  - CAN opcional de bancada (`0x0FD`, `0x0FE`);
  - CAN TX/RX nos GPIO 4/5;
  - detector provisório com histerese L<sub>on</sub>/L<sub>off</sub>;
  - componente `signal_proc`: buffer circular e atributos com `espressif/esp-dsp` 1.8.2, custo medido em ciclos.
- **Central:**
  - usuário `pulseira` na ACL;
  - consumidores do Telegraf para `pulseira/<id>/telemetria` e `pulseira/<id>/mqtt`;
  - `hora` passa a ser gravado (o parser descartava booleanos);
  - o notificador marca a queda enviada pela pulseira.
- **Defeitos achados na revisão:**
  - o CAN do hub não definia `quanta_clk_out` nem `bus_off_indicator`; com o valor 0, o driver mandava o clock
    do TWAI e o sinal de *bus-off* para o GPIO0, que é pino de *strapping* e botão BOOT. Corrigido com
    `GPIO_NUM_NC`, como no slide;
  - os indicadores liam um prazo de 64 bits sem trava: num processador de 32 bits ele pode ser lido pela metade.
    Corrigido com `portMUX`;
  - a pulseira segurava o mutex do detector durante a impressão do modo de coleta (dezenas de ms). Agora o
    mutex só cobre a soma da atividade de cada lote.
- **Verificação:**
  - build sem avisos: hub (36% livre), pulseira padrão (45% livre) e pulseira com MQTT + CAN + coleta + botão de
    teste (35% livre);
  - testes no PC: regras 53, config 60, detector 14 e `signal_proc` 25 verificações; notificador 11 testes;
  - central:
    - o usuário `pulseira` publicou em `telemetria`, `mqtt` e `alerta`;
    - as publicações dele em `porta/...`, `pulseira/<id>` e `status` foram descartadas pelo broker;
    - o InfluxDB gravou `pulseira_telemetria` e `pulseira_mqtt` com a tag `id`;
  - todas as 10 criações de task conferidas: núcleo explícito e prioridade ≤ 20.
- **Encontrado no caminho:** um script de edição gravou arquivos com fim de linha CRLF, e o `entrypoint.sh`
  do Mosquitto parou de rodar (`set: illegal option -`). Voltaram para LF; a dica ficou em
  `interface/README.md`, Problemas comuns.

### 2026-10-20 — correções e melhorias da revisão

- **Firmware comum:**
  - `firmware/common/protocol`: contrato ESP-NOW com tamanhos verificados em compilação e nomes MQTT;
  - `firmware/common/indicators`: LED WS2812 + buzzer, compartilhados pelas duas placas.
- **Hub:**
  - debounce reescrito: o nível só é lido depois de 50 ms sem bordas, e só uma mudança real é reportada;
  - portas modulares por `BOARD_DOORS`;
  - LM35 implementado;
  - CAN com autoteste no `menuconfig` e IDs `0x0F2` (temperatura) e `0x0F3+` (portas);
  - componentes novos: `rules`, `hub_config`, `net`, `mqtt_link`, `espnow_link`;
  - watchdog de 30 s com pânico e partição de app grande (`sdkconfig.defaults`).
- **Pulseira** (era só esqueleto): `mpu6050`, `fall_detect` (provisório), `espnow_tx`, `battery`, botão, buzzer
  e modo de coleta.
- **Central:**
  - `interface/` virou a central em Docker Compose;
  - a demonstração de 18/09 foi para `interface/legado/`;
  - simulador reescrito no contrato atual.
- **Documentação:** `estado.md` (este arquivo) e `coleta-de-dados.md` criados. Atualizados `protocolo.md`,
  `arquitetura.md`, `decisoes.md`, `roadmap.md`, `CLAUDE.md` e os READMEs.
- **Encontrado no teste:** o serviço Mosquitto do Windows (início automático) escuta em `127.0.0.1:1883` e
  captura conexões a `localhost`. Para usar a central em Docker, use o IP da máquina ou pare esse serviço
  (`interface/README.md`, Problemas comuns).
