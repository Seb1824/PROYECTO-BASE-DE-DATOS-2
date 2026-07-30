# Experimentos y resultados

## Protocolo

El benchmark utiliza el flujo completo del motor:

```text
archivo .db -> TableHeap -> BufferPool/CAR
                         -> QueryExecutor
                         -> IndexScan o Filter + SeqScan
                         -> QueryProfiler
```

- Esquema: `personas(id, nombre, ciudad, profesion)`.
- Tamaños: 1,000; 5,000; 10,000; 50,000 y 100,000 personas.
- Consultas por repetición: 100 ids existentes distribuidos
  determinísticamente.
- Repeticiones independientes: 5 por tamaño y por plan.
- Buffer Pool: 10 frames.
- Cada base se cierra después de la carga y se vuelve a abrir antes de medir.
- Se reporta media y desviación estándar muestral.
- Ambos modos ejecutan exactamente `WHERE id = N`.
- Con índice se usa `id -> RID`; sin índice se fuerza
  `Filter + SeqScan`.
- `resultados_benchmark_raw.csv` conserva las 50 mediciones individuales.

Medición regenerada después de introducir el registro físico de cuatro
columnas.

## Resultados principales

Los tiempos corresponden al total de 100 consultas en cada repetición.

| N | IndexScan, ms | Filter + SeqScan, ms | Aceleración | I/O índice | I/O secuencial | Hit ratio índice | Hit ratio secuencial |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 2.280 ± 0.350 | 62.194 ± 4.641 | 27.28x | 49 | 4,600 | 87.75 % | 0.00 % |
| 5,000 | 3.815 ± 1.134 | 362.776 ± 45.501 | 95.10x | 169 | 22,800 | 57.75 % | 0.00 % |
| 10,000 | 6.055 ± 1.322 | 1,156.271 ± 69.661 | 190.95x | 181 | 45,500 | 54.75 % | 0.00 % |
| 50,000 | 7.164 ± 2.703 | 5,870.287 ± 140.535 | 819.44x | 199 | 227,300 | 50.25 % | 0.00 % |
| 100,000 | 7.035 ± 5.216 | 7,675.127 ± 802.907 | 1,090.99x | 200 | 454,600 | 50.00 % | 0.00 % |

## Tamaño físico

| N | Base con índice | Base sin índice | Sobrecosto |
|---:|---:|---:|---:|
| 1,000 | 204,800 B | 192,512 B | 1.064x |
| 5,000 | 1,007,616 B | 937,984 B | 1.074x |
| 10,000 | 2,002,944 B | 1,867,776 B | 1.072x |
| 50,000 | 9,842,688 B | 9,314,304 B | 1.057x |
| 100,000 | 19,677,184 B | 18,624,512 B | 1.057x |

## Interpretación

El registro de personas es mayor que el antiguo par de enteros. Incluso
1,000 filas necesitan más de 10 páginas de tabla, por lo que un escaneo
completo ya supera los 10 frames del Buffer Pool. Cada nueva consulta vuelve
a recorrer toda la relación y el hit ratio del plan secuencial cae a cero.

El costo del escaneo crece linealmente con el número de páginas. Para
100,000 personas, cien consultas provocan 454,600 operaciones de lectura.
`IndexScan` recorre el directorio y un bucket, recupera el RID y lee solo la
fila correspondiente. En el mayor tamaño necesita 200 operaciones, unas
2,273 veces menos I/O, y resulta aproximadamente 1,091 veces más rápido en
esta ejecución.

El índice incrementa el archivo de 100,000 filas en aproximadamente 5.7 %.
Este porcentaje es menor que con registros de dos enteros porque ahora el
contenido de texto domina el tamaño de la tabla. El costo aparece en la
carga: 2,699.428 ± 783.379 ms con índice frente a
406.946 ± 78.273 ms sin índice.

Las escrituras durante la fase medida son cero porque el benchmark separa
la carga de la búsqueda y reabre la base antes de consultar. Hits, misses,
lecturas y escrituras son diferencias de contadores tomadas por
`QueryProfiler` para cada sentencia.

La desviación de `IndexScan` en 100,000 filas es sensible a ruido del sistema
porque el tiempo absoluto sigue siendo de pocos milisegundos. Por ello se
publican también las 50 observaciones crudas.

## Artefactos reproducibles

- `resultados_benchmark.csv`: medias y desviaciones.
- `resultados_benchmark_raw.csv`: mediciones individuales.
- `comparacion_busqueda.svg`: gráfica generada por el benchmark.
- `plot_benchmark.py`: alternativa para producir una gráfica PNG.
