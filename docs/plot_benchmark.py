from pathlib import Path
import sys

import matplotlib.pyplot as plt
import pandas as pd


csv_path = (
    Path(sys.argv[1])
    if len(sys.argv) >= 2
    else Path(__file__).with_name("resultados_benchmark.csv")
)
output_path = (
    Path(sys.argv[2])
    if len(sys.argv) >= 3
    else csv_path.with_name("comparacion_busqueda.png")
)

df = pd.read_csv(csv_path)
required_columns = {
    "n",
    "modo",
    "plan",
    "queries",
    "search_ms_mean",
    "search_ms_stddev",
    "io_operations_mean",
    "io_operations_stddev",
}
missing_columns = required_columns.difference(df.columns)
if missing_columns:
    missing = ", ".join(sorted(missing_columns))
    raise ValueError(
        f"El CSV no pertenece al benchmark actual. Faltan: {missing}"
    )

df["avg_search_ms"] = df["search_ms_mean"] / df["queries"]
df["avg_search_stddev"] = df["search_ms_stddev"] / df["queries"]
df["avg_io"] = df["io_operations_mean"] / df["queries"]
df["avg_io_stddev"] = df["io_operations_stddev"] / df["queries"]

con_indice = df[df["modo"] == "con_indice"].sort_values("n")
sin_indice = df[df["modo"] == "sin_indice"].sort_values("n")

fig, axes = plt.subplots(1, 2, figsize=(12, 5.2))

axes[0].errorbar(
    con_indice["n"],
    con_indice["avg_search_ms"],
    yerr=con_indice["avg_search_stddev"],
    marker="o",
    linewidth=2,
    color="#2563eb",
    label="IndexScan",
)
axes[0].errorbar(
    sin_indice["n"],
    sin_indice["avg_search_ms"],
    yerr=sin_indice["avg_search_stddev"],
    marker="s",
    linewidth=2,
    color="#dc2626",
    label="Filter + SeqScan",
)
axes[0].set_xscale("log")
axes[0].set_yscale("log")
axes[0].set_xlabel("Número de registros (N)")
axes[0].set_ylabel("Tiempo promedio por consulta (ms)")
axes[0].set_title("Tiempo de búsqueda")
axes[0].grid(True, which="both", linestyle="--", alpha=0.35)
axes[0].legend()

axes[1].errorbar(
    con_indice["n"],
    con_indice["avg_io"],
    yerr=con_indice["avg_io_stddev"],
    marker="o",
    linewidth=2,
    color="#2563eb",
    label="IndexScan",
)
axes[1].errorbar(
    sin_indice["n"],
    sin_indice["avg_io"],
    yerr=sin_indice["avg_io_stddev"],
    marker="s",
    linewidth=2,
    color="#dc2626",
    label="Filter + SeqScan",
)
axes[1].set_xscale("log")
axes[1].set_yscale("symlog", linthresh=0.01)
axes[1].set_xlabel("Número de registros (N)")
axes[1].set_ylabel("Operaciones de página por consulta")
axes[1].set_title("Costo de I/O")
axes[1].grid(True, which="both", linestyle="--", alpha=0.35)
axes[1].legend()

fig.suptitle(
    "Media y desviación estándar: TableHeap + QueryExecutor + QueryProfiler",
    fontsize=13,
    fontweight="bold",
)
fig.tight_layout()
fig.savefig(output_path, dpi=200)
print(f"Gráfico generado: {output_path}")
