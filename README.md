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
Parser -> SELECT / INSERT / UPDATE / DELETE
                         |
                         v
                   QueryExecutor
       +-----------------+------------------+
       |                 |                  |
       v                 v                  v
 IndexScan + RID   Filter + SeqScan   Mutaciones con rollback
       |                 |
       +--------> ProjectionOperator
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
13. Operadores `IndexScanOperator`, `SeqScanOperator`, `FilterOperator` y
    `ProjectionOperator`.
14. Parser para `SELECT`, `INSERT`, `UPDATE`, `DELETE` y comparadores.
15. `QueryExecutor` con seleccion de plan indexado o secuencial.
16. Inserciones, actualizaciones y borrados persistentes.
17. Indice `key -> RID`; `IndexScan` recupera la fila desde `TableHeap`.
18. Rollback de `INSERT` si falla la actualizacion del indice.
19. CLI interactiva para ejecutar consultas SQL.
20. Profiler por consulta con tiempo, Buffer hits/misses, hit ratio e I/O.
21. Benchmark con repeticiones, media y desviacion estandar.
22. Migracion automatica del indice legado `key -> value` a `key -> RID`.

## SQL soportado

La version actual implementa intencionalmente un subconjunto pequeno:

```sql
SELECT * FROM registros;
SELECT * FROM registros WHERE id = 103;
SELECT * FROM registros WHERE key = 103;
SELECT * FROM registros WHERE value = 515;
SELECT * FROM registros WHERE valor = 515;
SELECT key FROM registros WHERE value >= 515;
SELECT value, key FROM registros WHERE id != 103;
INSERT INTO registros VALUES (106, 530);
UPDATE registros SET value = 535 WHERE id = 106;
UPDATE registros SET key = 206 WHERE id = 106;
DELETE FROM registros WHERE id = 206;
```

Las palabras clave SQL no distinguen mayusculas de minusculas. Las
condiciones admiten `=`, `!=`, `<>`, `<`, `<=`, `>` y `>=` con enteros
positivos o negativos. Las proyecciones pueden usar `key/id` y
`value/valor`.

Seleccion de plan:

| Consulta | Plan |
|---|---|
| Sin `WHERE` | `SeqScan` |
| `WHERE key/id = N` con indice | `IndexScan` |
| Otro comparador sobre `key/id` | `Filter + SeqScan` |
| `WHERE value/valor OP N` | `Filter + SeqScan` |
| Proyeccion de columnas | agrega `ProjectionOperator` |
| `INSERT INTO registros VALUES (K, V)` | `Insert` |
| `UPDATE ... SET ... [WHERE ...]` | `Update` |
| `DELETE FROM ... [WHERE ...]` | `Delete` |

Las claves son unicas. El indice no duplica el valor de la fila: guarda un
`RID` compacto con `page_id` y `slot`. Si el indice falla durante `INSERT`,
el registro recien escrito se marca como borrado y el conteo del catalogo se
revierte.

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

Ejemplo de carga desde la misma CLI:

```text
mini-sgbd> INSERT INTO registros VALUES (106, 530);
Registro insertado: key=106, value=530
Filas afectadas: 1
Plan: Insert
```

```text
mini-sgbd> UPDATE registros SET value = 535 WHERE id = 106;
Filas actualizadas: 1
Plan: Update

mini-sgbd> DELETE FROM registros WHERE id = 106;
Filas eliminadas: 1
Plan: Delete
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
|   |   |-- rid.h
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
|       |-- projection_operator.h
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
|   |-- test_buffer_pool.cpp
|   |-- test_persistence.cpp
|   |-- test_index_stress.cpp
|   `-- test_projection.cpp
|-- docs/
|   |-- experimentos.md
|   `-- evidencias/
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
- `TableHeap`: inserta, actualiza y marca registros borrados en una cadena
  persistente de `TablePage`; expone acceso directo mediante `RID`.
- `Page`: contiene datos crudos, identificador, pin count y dirty bit.
- `BufferPoolManager`: coordina memoria y disco, impide expulsar paginas
  fijadas, escribe victimas sucias y realiza flush al finalizar.
- `CARReplacer`: mantiene T1/T2 y B1/B2, adapta el parametro `p` y solo
  devuelve frames expulsables.

### Indice

- `ExtensibleHashTable`: implementa insercion, actualizacion, borrado y
  busqueda `key -> RID`. Su
  `directory_page_id` se guarda en el catalogo y se recupera al reiniciar.
- `HashDirectoryPage`: representa el directorio del hash.
- `HashBucketPage`: almacena pares clave/RID dentro de una pagina.

### Procesamiento de consultas

- `Parser`: transforma SQL en `SelectQuery`, `InsertQuery`, `UpdateQuery` o
  `DeleteQuery`.
- `Operator`: interfaz comun del Modelo Volcano.
- `IndexScanOperator`: obtiene un RID del indice y lee la fila desde
  `TableHeap`.
- `SeqScanOperator`: recorre directamente las paginas de `TableHeap` mediante
  el Buffer Pool. Conserva ademas el constructor sobre `vector<Tuple>` para
  pruebas unitarias aisladas.
- `FilterOperator`: aplica un predicado a otro operador Volcano.
- `ProjectionOperator`: conserva solo las columnas solicitadas.
- `QueryExecutor`: crea el plan y recolecta los resultados.
- Para mutaciones, `QueryExecutor` mantiene coordinados `TableHeap`, catalogo
  e indice; revierte cambios locales cuando una operacion falla.
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
- `IndexStressTest`
- `ProjectionTest`

`BufferPoolTest` valida especificamente:

- Ninguna pagina fijada puede ser expulsada.
- Un pin count mayor que cero bloquea el reemplazo.
- `Victim()` retorna sin entrar en un bucle infinito.
- Las paginas sucias se escriben al ser expulsadas.
- El destructor sincroniza las paginas sucias restantes.

`IndexStressTest` valida 5,000 claves, varios splits consecutivos, reinicio,
duplicados, claves inexistentes, el limite fisico del directorio y el rechazo
de catalogos invalidos o incompletos.

## Benchmark

`benchmark_load` crea bases fisicas independientes y las vuelve a abrir antes
de medir. Ejecuta cinco repeticiones por defecto y calcula media y desviacion
estandar muestral. Todas las busquedas pasan por `QueryExecutor` y
`QueryProfiler`:
con indice usa `IndexScan`; sin indice usa `Filter + SeqScan` sobre
`TableHeap`. El CSV incluye tiempo, hits, misses, hit ratio, lecturas,
escrituras, costo total de I/O y tamano del archivo.

Ejecucion completa; la grafica SVG se genera sin Python:

```powershell
.\build\Debug\benchmark_load.exe
```

Para una ejecucion corta de validacion se pueden indicar archivo de salida,
tamano maximo, numero de consultas y repeticiones:

```powershell
.\build\Debug\benchmark_load.exe .\build\benchmark_smoke.csv 1000 10 3
```

Los artefactos de referencia se mantienen en:

```text
docs/resultados_benchmark.csv
docs/resultados_benchmark_raw.csv
docs/plot_benchmark.py
docs/comparacion_busqueda.svg
docs/comparacion_busqueda.png
docs/experimentos.md
docs/evidencias/
```

`docs/experimentos.md` contiene el protocolo, la tabla de resultados y la
interpretacion de tiempo, hit ratio, I/O y espacio. `docs/evidencias/`
contiene capturas de la CLI, reinicio, persistencia y benchmark.

## Limitaciones y trabajo pendiente

- El catalogo administra actualmente una sola tabla y un solo indice.
- Los borrados dejan tombstones persistentes; falta compactacion global y
  una lista de paginas libres.
- La clave `INT_MIN` esta reservada como marca interna de borrado.
- Cada `WHERE` admite una comparacion; faltan `AND`, `OR`, joins y
  agregaciones.
- El directorio hash ocupa una sola pagina y detiene su crecimiento al llegar
  a la profundidad global maxima representable.
- El rollback cubre fallos reportados durante la operacion, pero no reemplaza
  un WAL para recuperar un cierre abrupto del proceso o del sistema.

## Resumen

El proyecto ya dispone de un flujo funcional de extremo a extremo:

```text
SQL -> Parser -> Plan Volcano -> Ejecucion -> Resultados -> Profiler
```

El almacenamiento, Buffer Pool, CAR e indice sostienen las busquedas
indexadas; la CLI permite observar cuantitativamente el plan, el tiempo, el
comportamiento del Buffer Pool y el costo de I/O de cada consulta.
