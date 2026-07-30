# Mini-SGBD en C++ con reemplazo adaptativo CAR

Proyecto universitario de Bases de Datos II: un Sistema Gestor de Base de
Datos educativo escrito en C++17. Integra almacenamiento en paginas fisicas
de 4 KB, Buffer Pool con reemplazo CAR, indice hash extensible, operadores
fisicos con Modelo Volcano, parser SQL, CLI y perfilado del costo de cada
consulta.

## Estado actual

El flujo principal del sistema es:

```text
SQL
 |
 v
Parser -> SelectQuery -> QueryExecutor
                           |
              +------------+-------------+
              |                          |
              v                          v
        IndexScanOperator       FilterOperator
                                      |
                                      v
                               SeqScanOperator
                           |
                           v
                     QueryProfiler
                           |
                           v
                Resultados + tiempo + Buffer/I/O
```

El indice utiliza la siguiente ruta de almacenamiento:

```text
archivo .db -> DiskManager -> BufferPoolManager -> CARReplacer
                                      |
                                      v
                           ExtensibleHashTable
```

Funcionalidades implementadas:

1. Paginas fisicas de tamano fijo (`PAGE_SIZE = 4096`).
2. Lectura, escritura y asignacion de paginas mediante `DiskManager`.
3. Catalogo persistente con las raices de tabla e indice.
4. `TableHeap` enlazado para almacenar registros en paginas fisicas.
5. Contadores de lecturas y escrituras logicas de paginas.
6. Buffer Pool con pin count, dirty bit, flush y metricas de hits/misses.
7. Reemplazo adaptativo CAR con listas T1/T2 y listas fantasma B1/B2.
8. Proteccion para impedir la expulsion de paginas fijadas.
9. Sincronizacion automatica de paginas sucias al destruir el Buffer Pool.
10. Indice hash extensible persistente mapeado sobre el Buffer Pool.
11. Recuperacion de tabla e indice al reabrir el archivo `.db`.
12. Contrato comun de operadores Volcano: `Open()`, `Next()` y `Close()`.
13. Operadores `IndexScanOperator`, `SeqScanOperator` y `FilterOperator`.
14. Parser basico para sentencias `SELECT` con condicion `WHERE`.
15. `QueryExecutor` con seleccion de plan indexado o secuencial.
16. CLI interactiva para ejecutar consultas SQL.
17. Profiler por consulta con tiempo, Buffer hits/misses, hit ratio e I/O.
18. Benchmark de busqueda con indice frente a escaneo secuencial.

## SQL soportado

La version actual implementa intencionalmente un subconjunto pequeno:

```sql
SELECT * FROM registros;
SELECT * FROM registros WHERE id = 103;
SELECT * FROM registros WHERE key = 103;
SELECT * FROM registros WHERE value = 515;
SELECT * FROM registros WHERE valor = 515;
```

Las palabras clave SQL no distinguen mayusculas de minusculas. Las
condiciones admiten igualdad con valores enteros positivos o negativos.

Seleccion de plan:

| Consulta | Plan |
|---|---|
| Sin `WHERE` | `SeqScan` |
| `WHERE key/id = N` con indice | `IndexScan` |
| `WHERE key/id = N` sin indice | `Filter + SeqScan` |
| `WHERE value/valor = N` | `Filter + SeqScan` |

## CLI y profiler

En la primera ejecucion, `main_app` crea `mini_sgbd.db` y carga una tabla de
demostracion llamada `registros` con cinco filas. En las ejecuciones
posteriores recupera la misma tabla y el mismo indice mediante la pagina de
catalogo; no vuelve a crear sus paginas raiz.

```text
mini-sgbd> SELECT * FROM registros WHERE id = 103;
key         value
--------------------
103         515
Filas: 1
Plan: IndexScan
Tiempo: 0.120 ms
Buffer hits: 3
Buffer misses: 0
Hit ratio: 100.00 %
Lecturas de disco: 0
Escrituras de disco: 0
Costo I/O: 0 operaciones de pagina
```

Comandos adicionales:

```text
help
exit
quit
```

Las metricas se calculan como diferencias antes y despues de cada consulta,
por lo que no arrastran los contadores de consultas anteriores. El costo de
I/O es la suma de lecturas y escrituras logicas de paginas. Una consulta
atendida completamente desde el Buffer Pool puede reportar cero operaciones
de disco.

## Estructura del proyecto

```text
PROYECTO-BASE-DE-DATOS-2/
|-- CMakeLists.txt
|-- .gitignore
|-- README.md
|-- include/
|   |-- storage/
|   |   |-- catalog_page.h
|   |   |-- catalog_manager.h
|   |   |-- table_page.h
|   |   `-- table_heap.h
|   |-- buffer/
|   |-- index/
|   `-- query/
|       |-- tuple.h
|       |-- operator.h
|       |-- query.h
|       |-- parser.h
|       |-- index_scan_operator.h
|       |-- seq_scan_operator.h
|       |-- filter_operator.h
|       |-- query_executor.h
|       |-- query_profiler.h
|       `-- cli.h
|-- src/
|   |-- storage/
|   |-- buffer/
|   |-- index/
|   |-- query/
|   |-- benchmark/
|   `-- main.cpp
|-- tests/
|   |-- test_hash.cpp
|   |-- test_parser.cpp
|   |-- test_seq_scan.cpp
|   |-- test_filter.cpp
|   |-- test_query_executor.cpp
|   |-- test_cli.cpp
|   |-- test_query_profiler.cpp
|   `-- test_buffer_pool.cpp
|-- docs/
`-- data/
```

## Componentes principales

### Almacenamiento y Buffer

- `DiskManager`: administra el archivo binario, paginas de 4 KB y contadores
  de lecturas/escrituras.
- `CatalogManager`: crea o recupera la pagina cero y mantiene las raices
  persistentes de tabla e indice.
- `TablePage`: almacena registros `key/value` y el enlace a la pagina
  siguiente.
- `TableHeap`: inserta registros en una cadena persistente de `TablePage`.
- `Page`: contiene datos crudos, identificador, pin count y dirty bit.
- `BufferPoolManager`: coordina memoria y disco, impide expulsar paginas
  fijadas, escribe victimas sucias y realiza flush al finalizar.
- `CARReplacer`: mantiene T1/T2 y B1/B2, adapta el parametro `p` y solo
  devuelve frames expulsables.

### Indice

- `ExtensibleHashTable`: implementa insercion y busqueda por clave. Su
  `directory_page_id` se guarda en el catalogo y se recupera al reiniciar.
- `HashDirectoryPage`: representa el directorio del hash.
- `HashBucketPage`: almacena pares clave/valor dentro de una pagina.

### Procesamiento de consultas

- `Parser`: transforma SQL en `SelectQuery`.
- `Operator`: interfaz comun del Modelo Volcano.
- `IndexScanOperator`: busqueda puntual mediante el indice hash.
- `SeqScanOperator`: recorre directamente las paginas de `TableHeap` mediante
  el Buffer Pool. Conserva ademas el constructor sobre `vector<Tuple>` para
  pruebas unitarias aisladas.
- `FilterOperator`: aplica un predicado a otro operador Volcano.
- `QueryExecutor`: crea el plan y recolecta los resultados.
- `QueryProfiler`: mide tiempo, Buffer hits/misses e I/O por consulta.
- `RunCli`: mantiene la sesion interactiva y presenta resultados/metricas.

## Compilacion

Requisitos:

- CMake 3.12 o superior.
- Compilador compatible con C++17.

Configuracion y compilacion:

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Ejecutar la CLI con un generador multiconfiguracion como Visual Studio:

```powershell
.\build\Debug\main_app.exe
```

Con un generador de configuracion unica, el ejecutable suele quedar en:

```powershell
.\build\main_app.exe
```

## Pruebas

Ejecutar todas las pruebas:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Suites registradas:

- `HashIndexTest`
- `ParserTest`
- `SeqScanTest`
- `FilterTest`
- `QueryExecutorTest`
- `CliTest`
- `QueryProfilerTest`
- `BufferPoolTest`
- `PersistenceTest`

`BufferPoolTest` valida especificamente:

- Ninguna pagina fijada puede ser expulsada.
- Un pin count mayor que cero bloquea el reemplazo.
- `Victim()` retorna sin entrar en un bucle infinito.
- Las paginas sucias se escriben al ser expulsadas.
- El destructor sincroniza las paginas sucias restantes.

## Benchmark

`benchmark_load` compara busqueda mediante `ExtensibleHashTable` contra
escaneo secuencial para diferentes cantidades de registros. El ejecutable
genera `resultados_benchmark.csv` en su directorio de trabajo. En `docs/` se
mantienen resultados y una grafica de referencia:

```text
docs/resultados_benchmark.csv
docs/plot_benchmark.py
docs/comparacion_busqueda.png
```

## Limitaciones y trabajo pendiente

- El catalogo administra actualmente una sola tabla y un solo indice.
- `TableHeap` es append-only: faltan borrado, actualizacion, reutilizacion de
  espacio y una lista de paginas libres.
- El parser solo soporta `SELECT *` e igualdad con enteros.
- No existen todavia `INSERT`, `UPDATE`, `DELETE`, joins ni proyecciones.
- Falta ampliar las pruebas del indice para cubrir splits masivos,
  profundidad maxima y recuperacion ante archivos danados.
- El directorio hash necesita limites explicitos antes de alcanzar la
  capacidad maxima de una pagina.

## Resumen

El proyecto ya dispone de un flujo funcional de extremo a extremo:

```text
SQL -> Parser -> Plan Volcano -> Ejecucion -> Resultados -> Profiler
```

El almacenamiento, Buffer Pool, CAR e indice sostienen las busquedas
indexadas; la CLI permite observar cuantitativamente el plan, el tiempo, el
comportamiento del Buffer Pool y el costo de I/O de cada consulta.
