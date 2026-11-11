# Resumo dos resultados (gerado por experimentos/06_resumo.py)

## Gatilho

- Cenário A: L_on = 2.0 g deixa passar 99.2 % das quedas da SisFall, com 858 disparos por hora de atividade.
- Cenário B: L_on = 2.0 g deixa passar 99.2 % das quedas da SisFall, com 862 disparos por hora de atividade.
- Dados próprios (B): 100.0 % das quedas passam, 2645 disparos por hora de atividade.

## Comparação, cenário A (SisFall, validação cruzada por pessoa, 5 dobras)

Quedas detectadas e alarmes/h no ponto em que 95 % das janelas de impacto ficam acima do limiar; a sensibilidade por ensaio conta também as quedas que o gatilho perdeu.

| Método | Família | AUC | Quedas detectadas | Alarmes falsos/h | Quedas com ≤ 1 alarme/h | Operações/janela | Modelo (bytes) |
|---|---|---|---|---|---|---|---|
| limiar_do_firmware (2,5 g) | linha de base | — | 97.4 % | 69.9 | — | 1 | 8 |
| regras_cascata | literatura | 0.977 | 94.3 % | 54.9 | 23.5 % | 44.707 | 12 |
| corr_nao_normalizada | slides | 0.957 | 94.3 % | 139.2 | 22.0 % | 30.240 | 5.760 |
| corr_normalizada | slides | 0.956 | 94.3 % | 90.9 | 1.3 % | 30.240 | 5.760 |
| dtw | slides | 0.974 | 94.3 % | 74.0 | 7.6 % | 27.360 | 5.760 |
| ar12_anomalia | slides | 0.786 | 94.3 % | 688.1 | 8.6 % | 1.170 | 52 |
| arx12_anomalia | slides | 0.789 | 94.3 % | 676.4 | 11.4 % | 2.340 | 104 |
| centroide_euclidiana | slides | 0.993 | 94.3 % | 23.1 | 51.1 % | 44.840 | 544 |
| centroide_manhattan | slides | 0.992 | 94.3 % | 28.5 | 44.2 % | 44.840 | 544 |
| centroide_cosseno | slides | 0.996 | 94.3 % | 15.8 | 62.3 % | 44.840 | 544 |
| knn_euclidiana | slides | 0.996 | 94.3 % | 0.8 | 94.9 % | 819.972 | 1.550.536 |
| knn_manhattan | slides | 0.995 | 94.3 % | 0.5 | 96.1 % | 819.972 | 1.550.536 |
| knn_cosseno | slides | 0.995 | 94.3 % | 1.6 | 93.3 % | 819.972 | 1.550.536 |
| regressao_logistica | literatura | 0.997 | 94.3 % | 1.4 | 92.8 % | 44.806 | 412 |
| arvore_decisao | literatura | 0.986 | 95.9 % | 5.3 | 0.0 % | 44.710 | 612 |
| svm_rbf | literatura | 0.998 | 94.3 % | 0.8 | 94.6 % | 77.752 | 45.360 |
| rf_so_tempo | ablação | 0.999 | 94.3 % | 0.8 | 95.4 % | 16.189 | 240.144 |
| rf_so_freq | ablação | 0.927 | 94.3 % | 274.4 | 7.8 % | 24.840 | 1.344.336 |
| rf_so_ar | ablação | 0.706 | 94.3 % | 706.8 | 5.2 % | 9.064 | 1.385.472 |
| rf_tempo_freq | ablação | 0.999 | 94.3 % | 0.5 | 95.5 % | 39.235 | 236.568 |
| random_forest | literatura | 0.999 | 94.3 % | 0.4 | 95.7 % | 46.492 | 251.712 |
| cnn_1d | literatura | 0.994 | 94.3 % | 17.2 | 59.1 % | 202.976 | 24.260 |
| cnn_1d_6_eixos | literatura | 0.998 | 94.3 % | 1.2 | 93.9 % | 243.296 | 26.052 |

Ordem do AR pelo BIC: p = 12 (variância do resíduo de 0.268 g² em p = 1 para 0.194 em p = 2 e 0.114 em p = 12).

## Comparação, cenário B (SisFall, validação cruzada por pessoa, 5 dobras)

Quedas detectadas e alarmes/h no ponto em que 95 % das janelas de impacto ficam acima do limiar; a sensibilidade por ensaio conta também as quedas que o gatilho perdeu.

| Método | Família | AUC | Quedas detectadas | Alarmes falsos/h | Quedas com ≤ 1 alarme/h | Operações/janela | Modelo (bytes) |
|---|---|---|---|---|---|---|---|
| limiar_do_firmware (2,5 g) | linha de base | — | 90.0 % | 49.5 | — | 1 | 8 |
| regras_cascata | literatura | 0.974 | 94.3 % | 55.5 | 34.7 % | 44.707 | 12 |
| corr_nao_normalizada | slides | 0.843 | 94.3 % | 441.5 | 2.1 % | 30.240 | 5.760 |
| corr_normalizada | slides | 0.927 | 94.3 % | 128.9 | 4.7 % | 30.240 | 5.760 |
| dtw | slides | 0.974 | 94.3 % | 91.2 | 27.7 % | 27.360 | 5.760 |
| ar12_anomalia | slides | 0.661 | 94.3 % | 756.0 | 2.6 % | 1.170 | 52 |
| arx12_anomalia | slides | 0.643 | 94.3 % | 771.4 | 2.3 % | 2.340 | 104 |
| centroide_euclidiana | slides | 0.994 | 94.3 % | 18.7 | 48.0 % | 44.840 | 544 |
| centroide_manhattan | slides | 0.994 | 94.3 % | 21.7 | 44.7 % | 44.840 | 544 |
| centroide_cosseno | slides | 0.996 | 94.3 % | 15.1 | 62.2 % | 44.840 | 544 |
| knn_euclidiana | slides | 0.995 | 94.3 % | 0.5 | 95.6 % | 823.644 | 1.557.880 |
| knn_manhattan | slides | 0.995 | 94.3 % | 0.6 | 96.3 % | 823.644 | 1.557.880 |
| knn_cosseno | slides | 0.996 | 94.3 % | 1.2 | 93.3 % | 823.644 | 1.557.880 |
| regressao_logistica | literatura | 0.998 | 94.3 % | 1.4 | 91.7 % | 44.806 | 412 |
| arvore_decisao | literatura | 0.983 | 96.8 % | 6.6 | 0.9 % | 44.710 | 564 |
| svm_rbf | literatura | 0.998 | 94.3 % | 0.8 | 94.5 % | 80.098 | 48.580 |
| rf_so_tempo | ablação | 0.999 | 94.3 % | 1.2 | 93.2 % | 16.195 | 263.496 |
| rf_so_freq | ablação | 0.898 | 94.3 % | 372.2 | 2.8 % | 24.840 | 1.121.424 |
| rf_so_ar | ablação | 0.863 | 94.3 % | 434.3 | 5.1 % | 9.064 | 1.329.696 |
| rf_tempo_freq | ablação | 0.999 | 94.3 % | 1.0 | 94.2 % | 39.240 | 263.112 |
| random_forest | literatura | 0.999 | 94.3 % | 0.8 | 94.4 % | 46.503 | 275.808 |
| cnn_1d | literatura | 0.994 | 94.3 % | 20.5 | 30.8 % | 202.976 | 24.260 |
| cnn_1d_6_eixos | literatura | 0.999 | 94.3 % | 2.2 | 79.4 % | 243.296 | 26.052 |

Ordem do AR pelo BIC: p = 12 (variância do resíduo de 0.136 g² em p = 1 para 0.106 em p = 2 e 0.059 em p = 12).

## Dados próprios (treino na SisFall saturada em ±2 g, teste nos 18 ensaios)

| Método | Quedas detectadas | Alarmes falsos | Ensaios de atividade com alarme (de 8) |
|---|---|---|---|
| limiar_do_firmware (2,5 g) | 10/10 | 8 | — |
| regras_cascata | 8/10 | 5 | 4 |
| corr_normalizada | 10/10 | 12 | 6 |
| dtw | 10/10 | 8 | 6 |
| ar12_anomalia | 10/10 | 18 | 8 |
| centroide_cosseno | 8/10 | 4 | 3 |
| knn_manhattan | 8/10 | 1 | 1 |
| regressao_logistica | 7/10 | 0 | 0 |
| svm_rbf | 8/10 | 0 | 0 |
| random_forest | 7/10 | 0 | 0 |
| cnn_1d | 9/10 | 12 | 7 |

## Erros do Random Forest por tipo (cenário A)

Quedas mais perdidas: F10 (23 de 119), F14 (17 de 120), F13 (16 de 120), F05 (13 de 120), F08 (12 de 120).
Atividades com alarme falso: D11 (3), D14 (3).
