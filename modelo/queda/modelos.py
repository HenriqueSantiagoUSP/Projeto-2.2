"""
Modelos comparados no Q1. Todos recebem as janelas que passaram pelo gatilho e devolvem um **escore**
(quanto maior, mais parecido com queda); o limiar sai da curva ROC na avaliação.

Interface comum: `ajustar(conj_treino, F_treino)` e `escore(conj_teste, F_teste)`, onde `conj` é o
pipeline.Conjunto (janelas, versão reduzida a 50 Hz, rótulos) e `F` a matriz de atributos já calculada.

Slides: correlação cruzada com templates (normalizada e não normalizada), DTW com banda de Sakoe–Chiba,
distâncias entre vetores de atributos (euclidiana, Manhattan, cosseno), AR e ARX (erro de predição como
anomalia, coeficientes como atributos). Extras da literatura: regras em cascata, regressão logística,
árvore de decisão, Random Forest, SVM, kNN e a CNN 1D (cnn.py).
"""

from __future__ import annotations

import numpy as np
from sklearn.cluster import KMeans
from sklearn.ensemble import RandomForestClassifier
from sklearn.linear_model import LogisticRegression
from sklearn.neighbors import KNeighborsClassifier
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.svm import SVC
from sklearn.tree import DecisionTreeClassifier

SEMENTE = 0


# ---------------------------------------------------------------------------------------------------------
# Templates: correlação cruzada e DTW (sobre |a| a 50 Hz)
# ---------------------------------------------------------------------------------------------------------

def _znorm(x: np.ndarray) -> np.ndarray:
    c = x - x.mean(axis=-1, keepdims=True)
    return c / np.maximum(c.std(axis=-1, keepdims=True), 1e-9)


def escolher_templates(series: np.ndarray, k: int, normalizar: bool) -> np.ndarray:
    """k medoides aproximados: k-means e, de cada grupo, o exemplo real mais perto do centro."""
    if len(series) <= k:
        return series.copy()
    base = _znorm(series) if normalizar else series
    km = KMeans(n_clusters=k, n_init=4, random_state=SEMENTE).fit(base)
    escolhidos = []
    for c in range(k):
        membros = np.flatnonzero(km.labels_ == c)
        d = np.linalg.norm(base[membros] - km.cluster_centers_[c], axis=1)
        escolhidos.append(series[membros[np.argmin(d)]])
    return np.asarray(escolhidos)


def correlacao_maxima(x: np.ndarray, r: np.ndarray, max_lag: int, normalizada: bool) -> np.ndarray:
    """
    Pico da correlação cruzada entre cada linha de x (K, N) e o template r (N,), nos atrasos de -max_lag a
    +max_lag. Normalizada: coeficiente de Pearson na parte sobreposta (entre -1 e 1, só a forma importa).
    Não normalizada: média do produto (depende da energia: janelas fortes pontuam alto).
    """
    n = x.shape[1]
    melhor = np.full(len(x), -np.inf)
    for m in range(-max_lag, max_lag + 1):
        xs = x[:, max(0, m):n + min(0, m)]
        rs = r[max(0, -m):n - max(0, m)]
        if normalizada:
            xc = xs - xs.mean(axis=1, keepdims=True)
            rc = rs - rs.mean()
            den = np.sqrt((xc * xc).sum(axis=1) * (rc * rc).sum())
            v = (xc @ rc) / np.maximum(den, 1e-12)
        else:
            v = (xs @ rs) / xs.shape[1]
        melhor = np.maximum(melhor, v)
    return melhor


def dtw_lote(r: np.ndarray, x: np.ndarray, raio: int) -> np.ndarray:
    """
    DTW (slide 45) entre o template r (N,) e cada linha de x (K, M), custo |r[i] - x[j]|, só dentro da banda
    de Sakoe–Chiba |i - j·N/M| <= raio. Programação dinâmica vetorizada sobre as K janelas.
    """
    n, m = len(r), x.shape[1]
    inf = np.inf
    anterior = np.full((len(x), m + 1), inf)
    anterior[:, 0] = 0.0
    for i in range(n):
        atual = np.full((len(x), m + 1), inf)
        centro = int(round(i * m / n))
        for j in range(max(0, centro - raio), min(m, centro + raio + 1)):
            custo = np.abs(r[i] - x[:, j])
            atual[:, j + 1] = custo + np.minimum(np.minimum(anterior[:, j], anterior[:, j + 1]), atual[:, j])
        anterior = atual
        if i == 0:
            anterior[:, 0] = inf
    return anterior[:, m]


class Templates:
    """Classifica pela semelhança com templates de cada classe (filtro casado ou DTW)."""

    def __init__(self, metodo: str, k: int = 8, max_lag: int = 10, raio: int = 9):
        self.metodo, self.k, self.max_lag, self.raio = metodo, k, max_lag, raio   # 10 amostras = 0,2 s

    def _serie(self, conj):
        return conj.reduzidas[:, 0, :].astype(np.float64)          # |a| a 50 Hz

    def ajustar(self, conj, F=None):
        x, y = self._serie(conj), conj.y
        normalizar = self.metodo == "corr_norm"
        self.t_queda = escolher_templates(x[y == 1], self.k, normalizar)
        self.t_adl = escolher_templates(x[y == 0], self.k, normalizar)
        return self

    def _semelhanca(self, x, templates):
        if self.metodo == "dtw":
            return -np.min([dtw_lote(t, x, self.raio) for t in templates], axis=0)   # distância → semelhança
        normalizada = self.metodo == "corr_norm"
        return np.max([correlacao_maxima(x, t, self.max_lag, normalizada) for t in templates], axis=0)

    def escore(self, conj, F=None):
        x = self._serie(conj)
        return self._semelhanca(x, self.t_queda) - self._semelhanca(x, self.t_adl)

    def custo(self, n: int) -> tuple[int, int]:
        """(operações por janela, bytes do modelo): estimativa."""
        t = 2 * self.k
        ops = t * n * (2 * self.raio + 1) if self.metodo == "dtw" else t * n * (2 * self.max_lag + 1)
        return ops, t * n * 4


# ---------------------------------------------------------------------------------------------------------
# AR e ARX: erro de predição como detector de anomalia (slides "Modelos Autorregressivos")
# ---------------------------------------------------------------------------------------------------------

def _regressores(a: np.ndarray, w: np.ndarray | None, p: int):
    """Linhas [1, x[n-1..n-p], (u[n..n-p])] e alvo x[n], para cada janela (K, N)."""
    n = a.shape[1]
    colunas = [np.ones_like(a[:, p:])] + [a[:, p - k:n - k] for k in range(1, p + 1)]
    if w is not None:
        colunas += [w[:, p - k:n - k] for k in range(0, p + 1)]
    return np.stack(colunas, axis=2), a[:, p:]


class ARAnomalia:
    """
    Ajusta um AR(p) (ou ARX(p), com |ω| como entrada exógena) só nas janelas de atividade normal do treino.
    Na queda o modelo erra muito: o escore é o maior |e[n]| = |x[n] - x̂[n]| na janela.
    """

    def __init__(self, p: int = 4, exogena: bool = False):
        self.p, self.exogena = p, exogena

    def _series(self, conj):
        a = conj.reduzidas[:, 0, :].astype(np.float64)
        w = conj.reduzidas[:, 1, :].astype(np.float64) / 100.0 if self.exogena else None
        return a, w

    def ajustar(self, conj, F=None):
        a, w = self._series(conj)
        normal = conj.y == 0
        A, b = _regressores(a[normal], None if w is None else w[normal], self.p)
        A, b = A.reshape(-1, A.shape[2]), b.reshape(-1)
        self.coef, *_ = np.linalg.lstsq(A, b, rcond=None)
        return self

    def escore(self, conj, F=None):
        a, w = self._series(conj)
        A, b = _regressores(a, w, self.p)
        erro = b - A @ self.coef
        return np.abs(erro).max(axis=1)

    def custo(self, n: int) -> tuple[int, int]:
        return n * len(self.coef), len(self.coef) * 4


# ---------------------------------------------------------------------------------------------------------
# Regras em cascata (linha de base clássica da literatura)
# ---------------------------------------------------------------------------------------------------------

class Regras:
    """
    Queda = queda livre antes do impacto (|a| < 0,6 g) E, depois dele, corpo parado (desvio < 0,15 g) ou
    inclinação mudada (> 40°). O escore conta as condições atendidas, para dar uma curva ROC.
    """

    def __init__(self, nomes: list[str]):
        self.i_ql = nomes.index("queda_livre_min")
        self.i_pos = nomes.index("pos_std")
        self.i_inc = nomes.index("inclinacao_graus")

    def ajustar(self, conj, F):
        return self

    def escore(self, conj, F):
        ql, pos, inc = F[:, self.i_ql], F[:, self.i_pos], F[:, self.i_inc]
        return (ql < 0.6) * 2.0 + (pos < 0.15) + (inc > 40) + 0.01 * np.clip(inc, 0, 90) - 0.1 * ql

    def custo(self, n: int) -> tuple[int, int]:
        return 3, 12


# ---------------------------------------------------------------------------------------------------------
# Classificadores sobre o vetor de atributos
# ---------------------------------------------------------------------------------------------------------

class CentroideMaisProximo:
    """Distância ao centroide de cada classe (slide 43). Escore = d(atividade) - d(queda)."""

    def __init__(self, metrica: str):
        self.metrica = metrica

    def _prep(self, F):
        Z = self.escala.transform(F)
        if self.metrica == "cosseno":
            Z = Z / np.maximum(np.linalg.norm(Z, axis=1, keepdims=True), 1e-12)
        return Z

    def _dist(self, Z, c):
        if self.metrica == "manhattan":
            return np.abs(Z - c).sum(axis=1)
        return np.linalg.norm(Z - c, axis=1)

    def ajustar(self, conj, F):
        self.escala = StandardScaler().fit(F)
        Z = self._prep(F)
        self.c_queda, self.c_adl = Z[conj.y == 1].mean(axis=0), Z[conj.y == 0].mean(axis=0)
        return self

    def escore(self, conj, F):
        Z = self._prep(F)
        return self._dist(Z, self.c_adl) - self._dist(Z, self.c_queda)

    def custo(self, n_atr: int) -> tuple[int, int]:
        return 4 * n_atr, 4 * 4 * n_atr          # escala (média e desvio) + 2 centroides


class Sklearn:
    """Envolve um classificador do scikit-learn (com padronização quando ele precisa)."""

    def __init__(self, nome: str, max_negativos: int | None = None):
        self.nome, self.max_negativos = nome, max_negativos

    def _novo(self):
        if self.nome == "regressao_logistica":
            return make_pipeline(StandardScaler(), LogisticRegression(max_iter=3000, class_weight="balanced"))
        if self.nome == "arvore":
            return DecisionTreeClassifier(max_depth=6, class_weight="balanced", random_state=SEMENTE)
        if self.nome == "random_forest":
            return RandomForestClassifier(n_estimators=150, max_depth=12, min_samples_leaf=2, n_jobs=-1,
                                          class_weight="balanced_subsample", random_state=SEMENTE)
        if self.nome == "svm":
            return make_pipeline(StandardScaler(), SVC(kernel="rbf", C=3.0, gamma="scale",
                                                       class_weight="balanced"))
        if self.nome.startswith("knn"):
            metrica = {"knn_euclidiana": "euclidean", "knn_manhattan": "manhattan", "knn_cosseno": "cosine"}
            return make_pipeline(StandardScaler(), KNeighborsClassifier(
                n_neighbors=7, weights="distance", metric=metrica[self.nome], algorithm="brute"))
        raise ValueError(self.nome)

    def ajustar(self, conj, F):
        y = conj.y
        if len(np.unique(y)) < 2:
            # O escore usa predict_proba[:, 1], que nem existe com uma classe: falha aqui, dizendo por quê.
            raise ValueError(f"o treino de {self.nome} tem uma classe só ({len(y)} janelas): faltam quedas ou "
                             "atividades nesta dobra")
        idx = np.arange(len(y))
        if self.max_negativos and (y == 0).sum() > self.max_negativos:
            rng = np.random.default_rng(SEMENTE)
            neg = rng.choice(np.flatnonzero(y == 0), self.max_negativos, replace=False)
            idx = np.concatenate([np.flatnonzero(y == 1), neg])
        self.modelo = self._novo().fit(F[idx], y[idx])
        self.n_treino = len(idx)
        return self

    def escore(self, conj, F):
        m = self.modelo
        if hasattr(m, "decision_function") and not self.nome.startswith("knn"):
            return m.decision_function(F)
        return m.predict_proba(F)[:, 1]

    def custo(self, n_atr: int) -> tuple[int, int]:
        m = self.modelo
        final = m[-1] if hasattr(m, "steps") else m
        if self.nome == "regressao_logistica":
            return 3 * n_atr, 4 * (3 * n_atr + 1)
        if self.nome == "arvore":
            return final.get_depth(), 12 * final.tree_.node_count
        if self.nome == "random_forest":
            nos = sum(e.tree_.node_count for e in final.estimators_)
            prof = sum(e.get_depth() for e in final.estimators_)
            return prof, 12 * nos
        if self.nome == "svm":
            vs = final.support_vectors_.shape[0]
            return vs * n_atr * 3, 4 * vs * (n_atr + 1)
        return self.n_treino * n_atr * 2, 4 * self.n_treino * n_atr      # kNN guarda o treino inteiro
