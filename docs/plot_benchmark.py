import pandas as pd
import matplotlib.pyplot as plt

df = pd.read_csv("resultados_benchmark.csv")

con_indice = df[df["modo"] == "con_indice"].sort_values("n")
sin_indice = df[df["modo"] == "sin_indice"].sort_values("n")

fig, ax = plt.subplots(figsize=(8, 5.5))

ax.plot(con_indice["n"], con_indice["search_ms"], marker="o", linewidth=2,
        color="#2563eb", label="Con índice (Hash Extensible)")
ax.plot(sin_indice["n"], sin_indice["search_ms"], marker="s", linewidth=2,
        color="#dc2626", label="Sin índice (escaneo secuencial)")

ax.set_xscale("log")
ax.set_yscale("log")

ax.set_xlabel("Número de registros (N)", fontsize=11)
ax.set_ylabel("Tiempo de búsqueda (ms, escala log)", fontsize=11)
ax.set_title("Costo de búsqueda: con índice vs. sin índice", fontsize=13, fontweight="bold")

ax.grid(True, which="both", linestyle="--", alpha=0.4)
ax.legend(fontsize=10)

fig.tight_layout()
fig.savefig("comparacion_busqueda.png", dpi=200)
print("Gráfico generado: comparacion_busqueda.png")