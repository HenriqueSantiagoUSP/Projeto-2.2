# Projeto 3, Q1: roteiro da apresentação (~10 min)

Figuras em `modelo/resultados/`; números em `modelo/resultados/resumo.md` e `ensaios_com_alarme_A.csv` (as contagens do funil
estão na própria figura).

1. **O problema.** O vestível de hoje decide a queda por um limiar de 2,5 g. Na cintura, isso dá ~70 alarmes
   falsos por hora de atividade, porque correr, pular e sentar com força também passam do limiar.
   Figuras: `fig_picos.png` e, à direita de `fig_erros_tipo.png`, a % de ensaios com alarme por atividade
   (limiar atual: 97–100 % ao correr, pular, tropeçar, subir escada rápido).
2. **Os dados.**
   - SisFall: 38 pessoas, 4505 ensaios, cintura, 200 Hz.
   - Nossos 18 ensaios: cintura, ±2 g e saturados.
   - Dois cenários de faixa do sensor: ±8 g (vestível atual) e ±2 g (nossos dados).
   Figuras: `fig_sinais.png` e `fig_saturacao.png` (em ±2 g, queda e corrida têm quase o mesmo pico).
3. **Binário, não caracterizador.** O alarme só precisa de "queda ou não". Os 15 tipos de queda e as 19
   atividades servem para entender os erros.
4. **O pipeline.**
   - Filtro → gatilho (picos > 2,0 g) → janela de −1,0 s a +0,8 s → atributos → classificador.
   - O gatilho só acorda o classificador: deixa passar 99,2 % das quedas.
   Figuras: `fig_pipeline.png`, `fig_gatilho.png` e `fig_funil.png` (1798 quedas → 1784 passam → 1695
   detectadas; ~858 disparos/h → 0,4 alarme/h).
5. **Os métodos testados.**
   - Das aulas: correlação normalizada e não normalizada, DTW, AR e ARX, FFT, atributos no tempo,
     distâncias euclidiana, Manhattan e cosseno.
   - Da literatura: regras, regressão logística, árvore, SVM, Random Forest e CNN 1D.
   Use a tabela da seção 4 do relatório.
6. **Como avaliamos.**
   - Validação cruzada por pessoa: ninguém no treino e no teste.
   - Métricas: quedas detectadas, alarmes falsos por hora e custo no ESP32.
7. **Resultado principal.** Mesmo com ~95 % das quedas detectadas:
   - limiar atual: 69,9 alarmes/h;
   - Random Forest: 0,4/h;
   - regressão logística: 1,4/h.
   Figuras: `fig_resumo.png` e `fig_escores.png` (por que: quanto as distribuições de queda e de atividade
   se sobrepõem em cada método).
8. **O que cada método aprendeu.**
   - Comparar formas (correlação, DTW) e o AR como anomalia não separam queda de corrida.
   - O que separa é a queda livre antes e a postura/quietude depois do impacto. Ablação do Random Forest:
     só atributos no tempo dão 95,4 %; só FFT, 7,8 %; só AR, 5,2 %.
   - A CNN sobe de 59 % para 94 % quando recebe os 6 eixos.
   Figuras:
   - `fig_separacao.png`: pico × quietude e inclinação;
   - `fig_atributos_rf.png`: importância e ablação;
   - `fig_ar_explicado.png`: o AR erra na corrida como na queda;
   - `fig_fft.png`: o espectro da corrida e o da queda se sobrepõem.
9. **Normalizada × não normalizada.** A normalizada compara a forma e dá menos alarmes com sensibilidade alta.
   A não normalizada usa a energia e desaba quando o sensor satura (AUC 0,96 → 0,84).
   Figura: `fig_templates.png` (os templates e por que uma corrida passa do limiar).
10. **Custo no ESP32-S3.** Figura: `fig_custo_A.png`.
    - Regressão logística: 412 bytes.
    - Random Forest: 252 kB na flash.
    - CNN: 26 kB.
    - kNN: 1,5 MB, fora.
11. **Nos nossos ensaios.**
    - Random Forest, regressão logística e SVM: 0 alarmes falsos em caminhada e corrida, com todas as quedas
      de frente e de costas.
    - Perdem as quedas de joelhos.
    - O limiar atual: alarme em todas as 8 atividades.
    Figuras: `fig_proprios_matriz.png` (todos os métodos × 18 ensaios) e `fig_proprios_ensaios.png` (os
    sinais, com cada disparo e a decisão).
12. **Limitações e próximos passos.**
    - Os dados são da cintura, e a pulseira vai no pulso: nova coleta no pulso, ±8 g, várias pessoas.
    - Pipeline no ESP32: regressão logística primeiro, depois Random Forest, com o classificador rodando só
      quando o gatilho dispara.
