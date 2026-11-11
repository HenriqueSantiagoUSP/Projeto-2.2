# PCB v1 (2026-10-23)

Primeira PCB do projeto: uma placa-base para as duas placas (hub e vestível), com a ESP32-S3-DevKitC-1 encaixada.
Fonte em [`../../fontes/esquematico/`](../../fontes/esquematico/); decisões em [`../../README.md`](../../README.md).

| Arquivo | O quê |
|---|---|
| `placa-v1-jlcpcb.zip` | **o que se envia à JLCPCB**: Gerbers + furação |
| `gerber/` | os mesmos arquivos soltos, mais o mapa de furação e o job file (fora do zip) |
| `esquema.pdf` | esquema completo |
| `lista-de-materiais.csv` | lista de materiais agrupada por valor e footprint |
| `placa-3d-cima.png` | render 3D da face de cima |

## Placa

- 100,0 × 59,8 mm, 2 camadas, FR-4 de 1,6 mm, cobre de 1 oz, furo metalizado.
- Trilhas no F.Cu; plano de GND no B.Cu, com alívio térmico. A única via é a do GND do TP3.
- Furos: 114 metalizados.
  - 72 de 1,0 mm (DevKit e conectores);
  - 35 de 0,8 mm;
  - 4 de 1,3 mm (botão);
  - 2 de 1,1 mm (diodo);
  - 1 via de 0,4 mm.
  
  Nenhum furo não metalizado.

## Como foi gerado e conferido

Gerado com o `kicad-cli` do KiCad 10.0.6, a partir de `esquematico.kicad_pcb` salvo às 19:57 de 2026-10-23:

```
kicad-cli pcb export gerbers --layers "F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts" \
    --subtract-soldermask --no-x2 --no-netlist --check-zones -o gerber/ esquematico.kicad_pcb
kicad-cli pcb export drill --format excellon --excellon-units mm --excellon-zeros-format decimal \
    --excellon-oval-format alternate --excellon-separate-th --generate-map --map-format gerberx2 \
    -o gerber/ esquematico.kicad_pcb
```

Conferido antes de gerar:
- **DRC** (com paridade de esquema): 0 erros, 0 ligações faltando, 0 divergências com o esquema. Restam 2
  avisos de serigrafia: o desenho da antena da DevKit sai da borda, como esperado.
- **ERC:** 0 violações.
- **Teste no papel:** impressão em 1:1 com as peças de verdade encaixadas; o usuário confirmou o encaixe e a
  ordem dos pinos.

Conferido nos arquivos gerados:
- o contorno (Edge.Cuts) mede 100,0 × 59,8 mm;
- as contagens de furos acima batem com os footprints.

## Pedido na JLCPCB

Envie o `placa-v1-jlcpcb.zip` e use estas opções:

| Opção | Valor |
|---|---|
| Base material | FR-4 |
| Layers | 2 |
| Dimensions | 100 × 59,8 mm (lido do Gerber) |
| PCB Qty | 5 (mínimo) |
| PCB Thickness | 1.6 mm |
| PCB Color | verde (o mais rápido e barato) |
| Silkscreen | branco |
| Surface Finish | HASL (com chumbo, ou lead-free) |
| Outer Copper Weight | 1 oz |
| Via Covering | Tented |
| Mark on PCB | o número do pedido sai em lugar escolhido pela fábrica; a placa não tem o texto `JLCJLCJLCJLC` |
| PCB Assembly | não (a montagem é feita à mão) |

Confira o visualizador de Gerber do site antes de pagar. Deve aparecer:
- 2 camadas de cobre;
- o plano de GND embaixo, com o retângulo sem cobre sob a antena;
- todos os furos.
