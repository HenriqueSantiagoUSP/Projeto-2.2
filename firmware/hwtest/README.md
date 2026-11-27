# firmware/hwtest — teste de hardware da PCB v1

Um firmware só para conferir a placa recém-soldada: ele lê **todas** as peças que a PCB carrega, as do hub e as
da pulseira, e mostra na serial se cada uma funciona. Como a placa é a mesma para os dois firmwares, uma gravação
basta.

Não liga Wi-Fi, MQTT nem as regras: são leituras cruas, atualizadas a cada segundo, sem precisar da central.

| Peça | Onde | O que o teste mostra |
|---|---|---|
| LM35 → LM358 → RC | GPIO 4 (TP4) | mV no pino, °C pela curva da cadeia (`lm35.h`) e o ruído em 10 s |
| PIR HC-SR501 | J2 → GPIO 8 | livre/detectado e quantas vezes detectou (depois de 1 min de aquecimento) |
| Porta 1 / Porta 2 | J3 / J4 → GPIO 11 / 12 | aberta/fechada e quantas vezes abriu e fechou |
| Botão | SW1 → GPIO 18 | solto/apertado e quantas vezes foi apertado |
| MPU-6050 (GY-521) | J5 → SDA 1, SCL 2 | endereço I²C, amostras/s, \|a\|, aceleração (g) e rotação (°/s) |
| INT do MPU | J5 pino 8 → GPIO 17 | pulsos por segundo (um por amostra) |
| CAN (WCMCU-230) | J1 → TX 13, RX 14 | quadros enviados e o eco: o próprio quadro lido de volta pelo transceptor |
| LED | o da DevKit | o resumo: verde = tudo OK, azul = esperando ação, vermelho = falha |

## Antes de ligar

Com o multímetro e a placa **sem** a DevKit:
- sem curto entre 5 V e GND nem entre 3V3 e GND (TP1, TP2, TP3).

Depois:
1. Ligue só a USB-C da PCB e meça **5 V no TP1**.
2. Encaixe a DevKit e meça **3,3 V no TP2**.

## Os dois cabos

- **USB-C da PCB** (com cabo **A → C**): é ela que alimenta o LM35, o LM358 e o PIR. O D1 impede que a USB da
  DevKit alimente esse lado, então, sem a USB-C, o LM35 aparece "sem sinal" e o PIR nunca detecta.
- **USB "UART" da DevKit, ligada ao PC:** é por ela que saem o painel e entram as teclas. A USB nativa também
  mostra o painel, mas não recebe as teclas.

Os dois ao mesmo tempo não têm problema: o D1 não deixa as fontes se juntarem.

## Compilar, gravar e abrir

No PowerShell, com o ESP-IDF ativado (`. C:\Espressif\tools\Microsoft.v6.0.1.PowerShell_profile.ps1`):

```powershell
cd firmware\hwtest
idf.py -p COMx build flash monitor
```

O primeiro build leva alguns minutos; os seguintes, segundos. Para voltar ao firmware de verdade, grave o hub ou
a pulseira como de costume. O `hwtest` não mexe na configuração deles.

Se a DevKit for a **v1.1**, ligue em `idf.py menuconfig` → Component config → PCB a opção da v1.1. Ela só muda o
pino do LED (GPIO 38 em vez de 48).

## Como ler o painel

```
=== Teste de hardware da PCB v1 ===   t = 75 s   [6/8 OK]   LED: azul (esperando acao)
ESP32-S3 rev v0.2 | MAC 7c:df:a1:00:00:00 | ESP-IDF v6.0.1 | LED da DevKit v1.0 (GPIO 48)

[ OK  ] LM35      GPIO 4  1391 mV  24.28 C  ruido 0.03 C  (1389 a 1393 mV em 100 leituras; TP4 deve medir o mesmo)
[ OK  ] PIR       J2      livre     detectou 2x
[ OK  ] Porta 1   J3      fechada   abriu 1x, fechou 2x
[ ... ] Porta 2   J4      aberta    abriu 0x, fechou 0x
                          -> feche e abra o contato (ima no reed, ou um fio entre os dois pinos do J4)
[ OK  ] Botao     SW1     solto     apertou 3x
[ OK  ] MPU-6050  J5      0x68  200 amostras/s  |a| 1.00 g  a (+0.01 -0.02 +1.00) g  w (  +0.3   -0.1   +0.2) dps
[ OK  ] INT MPU   GPIO 17 200 pulsos/s
[FALHA] CAN       J1      enviados 12 (recusados 0)  eco 0  de outros nos 0
                          -> sem eco: o transceptor esta no J1? TX/RX (GPIO 13/14) trocados? 3V3 no J1?
```

- **`[ OK  ]`:** conferido.
- **`[ ... ]`:** ainda falta uma ação sua, ou uma medida. A linha `->` diz qual.
- **`[FALHA]`:** a linha `->` diz o que conferir.

O MAC do cabeçalho é o de estação, o que o hub e a pulseira usam no pareamento do ESP-NOW.

## O que fazer em cada item

| Item | Para ficar OK |
|---|---|
| LM35 | nada: basta uma leitura entre 0 e 57 °C. Compare o mV com o **TP4** no multímetro e o °C com um termômetro. Ruído acima de 0,2 °C aparece como aviso, porque é o limiar em que o hub publicaria uma mudança que não existe |
| PIR | espere o **1 minuto** de aquecimento e passe a mão na frente. Ajuste o potenciômetro de tempo para o mínimo (~3 s) para ele voltar a "livre" rápido |
| Porta 1 e 2 | feche e abra o contato: aproxime o ímã do reed, ou ponha um fio entre os dois pinos do J3/J4 |
| Botão | aperte e solte |
| MPU-6050 | deixe a placa parada um instante: \|a\| deve dar ~1 g. Gire a placa: o 1 g passa de um eixo para outro. Com ±8 g, parado, cada eixo lê no máximo ~1 g. Valores 4× menores indicariam a escala errada |
| INT | nada: ~200 pulsos/s com o MPU respondendo |
| CAN | nada: o eco cresce a cada segundo. É o modo bancada: o nó dispensa a confirmação de outro nó e relê o próprio quadro **pelo transceptor**, o que confere o WCMCU-230 e as trilhas 13/14 |

Com o CAN ligado, CANH e CANL ficam perto de 2,3 V em repouso. Dá para medir no TP5 e no TP6.

## Teclas

| Tecla | O que faz |
|---|---|
| `c` | CAN rápido: 200 quadros/s. Compare o **ruído do LM35** com o CAN ligado e desligado. A trilha do CANH passa a 0,9 mm do pino 2 do LM358 ([`docs/pendencias.md`](../../docs/pendencias.md), Hardware) |
| `i` | varre o I²C e lista os endereços que respondem (o MPU-6050 é 0x68; com o AD0 alto, 0x69) |
| `r` | recomeça a conferência: zera as bordas, o eco do CAN e as leituras do LM35 |
| `p` | alterna entre o painel que se redesenha e o modo rolando (útil para copiar o histórico) |
| `h` | mostra a ajuda por 15 s |

## Problemas comuns

| Sintoma | Causa provável |
|---|---|
| LM35 "sem sinal" | a USB-C da PCB não está ligada, o LM358 está fora do soquete ou invertido, ou falta solda no U2/U3 |
| LM35 fora da faixa, ou °C muito diferente do termômetro | ganho errado (R1 de 2 kΩ, R2 de 9,1 kΩ), o C3 trocado por um eletrolítico, ou o LM35 invertido (ele esquenta) |
| MPU "nao responde no I2C" | GY-521 fora do J5 ou deslocado, SDA/SCL trocados, ou sem 3V3. A tecla `i` mostra se algo responde |
| INT "sem pulsos" com o MPU OK | o pino 8 do J5 (INT do GY-521) sem solda até o GPIO 17 |
| CAN "sem eco" | o WCMCU-230 fora do J1, TX/RX invertidos no módulo, ou sem 3V3 nele |
| PIR nunca detecta | ainda no aquecimento (o painel mostra quanto falta), a USB-C da PCB desligada, ou o R4 (1 kΩ) sem solda |
| Porta ou botão parados em um estado | falta solda no J3, no J4 ou no SW1; um curto deixa a entrada sempre "fechada"/"apertado" |
| Painel embaralhado, sem redesenhar | o terminal não entende o código de limpar a tela: tecle `p` para o modo rolando |

## Testes no PC

O julgamento de cada item fica no componente puro `components/hwtest_checks`, com testes no PC (Git Bash, com o
gcc do MSYS2 no PATH):

```bash
sh firmware/hwtest/components/hwtest_checks/test_host/run_tests.sh
```
