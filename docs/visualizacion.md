# Visual Query Profiler

## Objetivo

El visualizador conecta dos referencias centrales de la propuesta:

- S. Bansal y D. S. Modha, **CAR: Clock with Adaptive Replacement**,
  FAST 2004:
  <https://www.usenix.org/conference/fast-04/car-clock-adaptive-replacement>
- D. Moritz, D. Halperin, B. Howe y J. Heer,
  **Perfopticon: Visual Query Analysis for Distributed Databases**,
  EuroVis 2015:
  <https://dig.cmu.edu/publications/2015-perfopticon.html>

CAR sustenta la politica adaptativa del Buffer Pool. Perfopticon sustenta la
coordinacion visual entre el plan fisico, el costo de sus operadores y la
traza de ejecucion.

Perfopticon fue disenado para motores distribuidos. Este proyecto adapta sus
principios a un SGBD educativo de un solo nodo: el flujo entre servidores se
reemplaza por el flujo entre `TableHeap`, Buffer Pool, CAR y operadores
Volcano.

## Flujo instrumentado

```text
SQL
 |
 v
Parser -> QueryExecutor -> plan fisico
                         |
                         +-> Operator::Open/Next/Close
                         |
                         +-> BufferPoolManager -> CARReplacer
                                                   |
                                                   v
                                            snapshots y eventos
                         |
                         v
                  ExecutionTracer
                         |
                         v
                  QueryVisualizer
                         |
                         v
              build/query_profile.html
```

La instrumentacion vive en el contrato comun `Operator`. Por ello,
`Projection`, `Filter`, `SeqScan` e `IndexScan` producen la misma clase de
eventos sin duplicar logica de medicion.

La traza detallada se activa al configurar una ruta de visualizacion o al
solicitarla explicitamente. El benchmark no la habilita, de modo que el costo
de registrar cada llamada a `Next()` no contamina los experimentos de
rendimiento.

Los tiempos inclusivos contienen el trabajo de los hijos. El tiempo propio se
calcula restando el costo inclusivo de los hijos directos. Esta separacion
evita presentar el tiempo de `SeqScan` varias veces como si fuera costo
independiente de `Filter` y `Projection`.

## Vistas coordinadas

### Grafo del plan fisico

Los nodos se registran en orden raiz-hijo y conservan:

- nombre del operador;
- parametros o predicado;
- identificador del padre;
- tiempo inclusivo;
- filas producidas.

Seleccionar un nodo actualiza su detalle y filtra la linea temporal al
operador elegido.

### Costo por operador

Para cada operador se presentan:

- tiempo inclusivo;
- tiempo propio;
- tiempo acumulado en `Open`, `Next`, `Close` o `Execute`;
- numero de llamadas a `Next`;
- filas producidas.

### Linea temporal

La linea temporal mezcla dos categorias de eventos:

- Volcano: `Open`, `Next`, `Close` y `Execute`;
- CAR: hit, miss, insercion, promocion, segunda oportunidad y expulsion.

Los filtros permiten aislar cada fase o mostrar unicamente los eventos CAR.
La captura conserva hasta 2,000 eventos de operador y 1,000 eventos CAR.
Aunque se alcance ese limite, los acumulados por operador siguen
contabilizando la ejecucion completa.

### Estado adaptativo CAR

Cada evento copia un snapshot inmutable con:

- capacidad;
- frames expulsables;
- hits y misses acumulados;
- parametro objetivo `p`;
- paginas residentes en T1 y T2;
- paginas fantasma en B1 y B2.

El control inferior permite avanzar o retroceder por la traza y observar como
las paginas cambian de lista.

Eventos principales:

| Evento | Interpretacion |
|---|---|
| `HIT` | La pagina ya estaba en T1 o T2 y se activa su bit de referencia |
| `MISS_COLD` | La pagina no estaba residente ni en las listas fantasma |
| `MISS_B1` | Hit fantasma reciente; `p` crece |
| `MISS_B2` | Hit fantasma frecuente; `p` disminuye |
| `INSERT_T1` | Una pagina se incorpora a la lista reciente |
| `PROMOTE_T1_T2` | La segunda oportunidad mueve una pagina a frecuente |
| `SECOND_CHANCE_T2` | Una pagina frecuente conserva su residencia |
| `EVICT_T1_B1` | Una pagina reciente se mueve a su historial fantasma |
| `EVICT_T2_B2` | Una pagina frecuente se mueve a su historial fantasma |

## Uso

Compilar y ejecutar:

```powershell
cmake -S . -B build
cmake --build build --config Debug
.\build\Debug\main_app.exe
```

En una configuracion unica, el ejecutable puede estar en
`.\build\main_app.exe`.

Cada consulta actualiza:

```text
build/query_profile.html
```

Ejemplo:

```sql
SELECT nombre, profesion
FROM personas
WHERE ciudad = 'Arequipa';
```

Abrir el reporte:

```powershell
Start-Process .\build\query_profile.html
```

## Demostracion completa de CAR

La tabla inicial no genera suficiente presion para poblar siempre B1 y B2.
El comando `car-demo` ejecuta la secuencia reproducible:

```text
1, 2, 3, 4, 1, 2, 5, 3, 4, 5, 1
```

con capacidad cuatro. La secuencia genera hits residentes, hits fantasma en
B1 y B2, promociones, expulsiones y cambios de `p`.

```text
mini-sgbd> car-demo
Demostracion CAR generada: build/car_demo.html
```

```powershell
Start-Process .\build\car_demo.html
```

## Evidencias

- `docs/evidencias/visual_query_profiler.png`: plan, costo y traza de una
  consulta real.
- `docs/evidencias/car_demo.png`: carga determinista y estado final CAR.
- `tests/test_query_visualizer.cpp`: contenido del HTML y eventos
  adaptativos esperados.
- `tests/test_query_profiler.cpp`: jerarquia del plan, filas por operador y
  fases Volcano.
- `tests/test_buffer_pool.cpp`: snapshots y observador de CAR.
