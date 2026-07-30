# Experimentos y resultados

## Protocolo

El benchmark utiliza el flujo completo del motor:

```text
archivo .db -> TableHeap -> BufferPool/CAR
                         -> QueryExecutor
                         -> IndexScan o Filter + SeqScan
                         -> QueryProfiler
```

- Tamaños: 1,000; 5,000; 10,000; 50,000 y 100,000 registros.
- Consultas por repetición: 100 claves existentes distribuidas
  determinísticamente.
- Repeticiones independientes: 5 por tamaño y por plan.
- Buffer Pool: 10 frames.
- Cada base se cierra después de la carga y se vuelve a abrir antes de medir.
- Se reporta media y desviación estándar muestral.
- El índice almacena un `RID`; `IndexScan` recupera la fila desde
  `TableHeap`.
- `resultados_benchmark_raw.csv` conserva las 50 mediciones individuales.

## Resultados principales

Los tiempos corresponden al total de 100 consultas en cada repetición.

| N | IndexScan, ms | Filter + SeqScan, ms | Aceleración | I/O índice | I/O secuencial | Hit ratio índice | Hit ratio secuencial |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.436 ± 0.143 | 0.935 ± 0.152 | 2.14x | 5 | 2 | 98.75 % | 99.00 % |
| 5,000 | 0.726 ± 0.138 | 4.076 ± 1.964 | 5.62x | 75 | 20 | 81.25 % | 98.00 % |
| 10,000 | 1.087 ± 0.304 | 19.644 ± 4.363 | 18.08x | 101 | 2,000 | 74.75 % | 0.00 % |
| 50,000 | 1.724 ± 0.940 | 58.961 ± 3.183 | 34.20x | 196 | 9,800 | 51.00 % | 0.00 % |
| 100,000 | 1.594 ± 0.367 | 128.342 ± 23.612 | 80.50x | 200 | 19,600 | 50.00 % | 0.00 % |

## Tamaño físico

| N | Base con índice | Base sin índice | Sobrecosto |
|---:|---:|---:|---:|
| 1,000 | 24,576 B | 12,288 B | 2.00x |
| 5,000 | 114,688 B | 45,056 B | 2.55x |
| 10,000 | 221,184 B | 86,016 B | 2.57x |
| 50,000 | 933,888 B | 405,504 B | 2.30x |
| 100,000 | 1,859,584 B | 806,912 B | 2.30x |

## Interpretación

En 1,000 y 5,000 registros, las páginas de tabla caben total o casi
totalmente en los 10 frames. Por eso el escaneo secuencial necesita pocas
lecturas después de calentar el Buffer Pool, aunque ya es más lento por
examinar cada fila.

Desde 10,000 registros, el conjunto de trabajo supera el Buffer Pool. El
escaneo secuencial provoca reemplazo continuo: su hit ratio cae a cero y el
costo crece hasta 19,600 lecturas para 100 consultas. `IndexScan` mantiene
un costo cercano a dos lecturas por consulta en 100,000 registros y resulta
80.5 veces más rápido.

El índice tiene dos costos. La carga de 100,000 registros tarda
889.270 ± 45.365 ms frente a 35.095 ± 4.553 ms sin índice, y el archivo
ocupa aproximadamente 2.30 veces más espacio. El beneficio se obtiene en
consultas puntuales repetidas, no en la carga inicial.

Las escrituras durante la fase medida son cero porque el benchmark separa
la carga de la búsqueda y reabre la base antes de consultar. Las lecturas,
hits y misses son diferencias de contadores tomadas por `QueryProfiler` para
cada sentencia.

## Artefactos reproducibles

- `resultados_benchmark.csv`: medias y desviaciones.
- `resultados_benchmark_raw.csv`: mediciones individuales.
- `comparacion_busqueda.svg`: gráfica generada por el propio benchmark.
- `plot_benchmark.py`: alternativa para producir una gráfica PNG cuando
  Python, pandas y matplotlib estén disponibles.

