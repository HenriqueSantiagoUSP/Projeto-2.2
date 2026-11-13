"""Gera resultados/resumo.md a partir das tabelas: os números do relatório saem daqui, nunca digitados."""

import json

import _comum
import pandas as pd

R = _comum.RESULTADOS


def pct(v):
    return "—" if pd.isna(v) else f"{100 * v:.1f} %"


def num(v, casas=1):
    return "—" if pd.isna(v) else f"{v:.{casas}f}"


def tabela_comparacao(c):
    df = pd.read_csv(R / f"comparacao_{c}.csv")
    linhas = ["| Método | Família | AUC | Quedas detectadas | Alarmes falsos/h | Quedas com ≤ 1 alarme/h | "
              "Operações/janela | Modelo (bytes) |", "|---|---|---|---|---|---|---|---|"]
    for _, l in df.iterrows():
        milhar = lambda v: f"{int(v):,}".replace(",", ".")      # noqa: E731
        linhas.append(f"| {l.metodo} | {l.familia} | {num(l.auc, 3)} | {pct(l.sens_ensaio)} | "
                      f"{num(l.alarmes_h)} | {pct(l.sens_1alarme_h)} | {milhar(l.ops_janela)} | "
                      f"{milhar(l.bytes_modelo)} |")
    return "\n".join(linhas)


def main():
    gat = pd.read_csv(R / "gatilho.csv")
    escolha = json.loads((R / "gatilho_escolhido.json").read_text())
    partes = ["# Resumo dos resultados (gerado por experimentos/06_resumo.py)", ""]
    partes += ["## Gatilho", ""]
    for c, l_on in escolha.items():
        s = gat[(gat.base == "sisfall") & (gat.cenario == c) & (gat.l_on_g == l_on)].iloc[0]
        partes.append(f"- Cenário {c}: L_on = {l_on} g deixa passar {pct(s.quedas_que_passam)} das quedas da "
                      f"SisFall, com {s.disparos_adl_por_h:.0f} disparos por hora de atividade.")
    s = gat[(gat.base == "propria") & (gat.l_on_g == escolha["B"])].iloc[0]
    partes.append(f"- Dados próprios (B): {pct(s.quedas_que_passam)} das quedas passam, "
                  f"{s.disparos_adl_por_h:.0f} disparos por hora de atividade.")
    for c in ("A", "B"):
        ar = pd.read_csv(R / f"ordem_ar_{c}.csv")
        partes += ["", f"## Comparação, cenário {c} (SisFall, validação cruzada por pessoa, 5 dobras)", "",
                   "Quedas detectadas e alarmes/h no ponto em que 95 % das janelas de impacto ficam acima do "
                   "limiar; a sensibilidade por ensaio conta também as quedas que o gatilho perdeu.", "",
                   tabela_comparacao(c), "",
                   f"Ordem do AR pelo BIC: p = {int(ar.sort_values('bic').iloc[0].p)} (variância do resíduo de "
                   f"{ar.var_residuo.iloc[0]:.3f} g² em p = 1 para {ar.var_residuo.iloc[1]:.3f} em p = 2 e "
                   f"{ar.var_residuo.iloc[-1]:.3f} em p = {int(ar.p.iloc[-1])})."]
    pr = pd.read_csv(R / "proprios_resumo.csv")
    partes += ["", "## Dados próprios (treino na SisFall saturada em ±2 g, teste nos 18 ensaios)", "",
               "| Método | Quedas detectadas | Alarmes falsos | Ensaios de atividade com alarme (de 8) |",
               "|---|---|---|---|"]
    for _, l in pr.iterrows():
        partes.append(f"| {l.metodo} | {int(l.quedas_detectadas)}/{int(l.quedas)} | {int(l.alarmes_falsos)} | "
                      f"{'—' if pd.isna(l.ensaios_adl_com_alarme) else int(l.ensaios_adl_com_alarme)} |")
    er = pd.read_csv(R / "erros_por_tipo_A.csv")
    rf = er[er.metodo == "random_forest"]
    q = rf[rf.queda].sort_values("perdidas", ascending=False).head(5)
    a = rf[(~rf.queda) & (rf.alarmes_falsos > 0)]
    partes += ["", "## Erros do Random Forest por tipo (cenário A)", "",
               "Quedas mais perdidas: " + ", ".join(f"{l.codigo} ({int(l.perdidas)} de {int(l.ensaios)})"
                                                   for _, l in q.iterrows()) + ".",
               "Atividades com alarme falso: " + (", ".join(f"{l.codigo} ({int(l.alarmes_falsos)})"
                                                           for _, l in a.iterrows()) or "nenhuma") + "."]
    # newline="\n": sem ele o Windows grava CRLF, e o repositório usa LF (CLAUDE.md).
    (R / "resumo.md").write_text("\n".join(partes) + "\n", encoding="utf-8", newline="\n")
    print("\n".join(partes))


if __name__ == "__main__":
    main()
