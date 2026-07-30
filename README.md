# Mini-SGBD en C++ con reemplazo adaptativo CAR

Proyecto universitario de Bases de Datos II: un motor educativo escrito en
C++17 con almacenamiento binario paginado, Buffer Pool, reemplazo CAR,
indice hash extensible, operadores Volcano, SQL, profiler y visualizacion
interactiva.

## Estado actual

El flujo funcional es:

```text
SQL
 |
 v
Parser -> SELECT / INSERT / UPDATE / DELETE
 |
 v
QueryExecutor
 +-- IndexScan (id -> RID)
 +-- Filter -> SeqScan
 +-- Projection
 +-- Mutaciones con rollback local
 |
 v
QueryProfiler
 +-- tiempo, hits, misses, hit ratio e I/O
 +-- Open / Next / Close por operador
 +-- estados T1 / T2 / B1 / B2 / p de CAR
 |
 v
CLI + Visual Query Profiler HTML
```

El almacenamiento sigue esta ruta:

```text
personas_sgbd.db
        |
        v
DiskManager -> BufferPoolManager -> CARReplacer
        |                |
        v                +-> ExtensibleHashTable (id -> RID)
    TableHeap
```

## Esquema de datos

La tabla persistente se llama `personas`:

| Columna | Tipo logico | Limite fisico | Uso |
|---|---|---:|---|
| `id` | entero de 32 bits | 4 bytes | clave primaria e indice hash |
| `nombre` | texto | 63 bytes UTF-8 | nombre de la persona |
| `ciudad` | texto | 47 bytes UTF-8 | ciudad de residencia |
| `profesion` | texto | 63 bytes UTF-8 | profesion u ocupacion |

Cada registro se codifica con longitud fija dentro de una pagina de 4 KB.
La ocupacion se representa con un indicador explicito, por lo que ningun
valor de `id` esta reservado como tombstone. Los textos se guardan dentro
del archivo `.db`; no son punteros ni objetos `std::string` serializados.

El indice almacena exclusivamente `id -> RID`. Un `RID` contiene
`page_id + slot`; `IndexScanOperator` usa ese identificador para recuperar
la persona completa desde `TableHeap`.

## Funcionalidades implementadas

- Paginas fisicas de 4 KB y archivo binario persistente.
- Catalogo v3 con raiz de tabla, raiz de indice y conteo de filas.
- `TableHeap` enlazado con insercion, lectura, actualizacion, borrado y
  restauracion para rollback.
- Buffer Pool con pin count, dirty bit, flush, hits y misses.
- CAR con T1/T2, listas fantasma B1/B2 y parametro adaptativo `p`.
- Indice hash extensible persistente sobre el Buffer Pool.
- Recuperacion de tabla e indice despues de reiniciar.
- Operadores Volcano `Open()`, `Next()` y `Close()`.
- `IndexScanOperator`, `SeqScanOperator`, `FilterOperator` y
  `ProjectionOperator`.
- Parser y ejecutor para `SELECT`, `INSERT`, `UPDATE` y `DELETE`.
- Comparadores `=`, `!=`, `<>`, `<`, `<=`, `>` y `>=`.
- Rollback de `INSERT` si falla la escritura del indice.
- Coordinacion del indice al actualizar un `id` o eliminar una fila.
- CLI con metricas por consulta.
- Benchmark de `IndexScan` frente a `Filter + SeqScan`.
- Visualizador HTML inspirado en Perfopticon.

## SQL soportado

```sql
SELECT * FROM personas;
SELECT * FROM personas WHERE id = 103;
SELECT nombre, profesion
FROM personas
WHERE ciudad = 'Arequipa';

INSERT INTO personas
VALUES (106, 'Ana Torres', 'Arequipa', 'Ingeniera de Software');

UPDATE personas
SET profesion = 'Arquitecta'
WHERE id = 106;

UPDATE personas
SET id = 206
WHERE id = 106;

DELETE FROM personas WHERE id = 206;
DELETE FROM personas WHERE ciudad = 'Lima';
```

Los literales de texto requieren comillas simples. Una comilla dentro del
texto se escapa duplicandola:

```sql
SELECT * FROM personas WHERE nombre = 'D''Angelo';
```

Las palabras clave SQL no distinguen mayusculas de minusculas. Los nombres
de columnas validos son `id`, `nombre`, `ciudad` y `profesion`. El ejecutor
rechaza un texto usado contra `id` y un entero usado contra una columna de
texto.

### Seleccion del plan

| Consulta | Plan |
|---|---|
| Sin `WHERE` | `SeqScan` |
| `WHERE id = N` con indice | `IndexScan` |
| Otro comparador sobre `id` | `Filter + SeqScan` |
| Comparacion sobre texto | `Filter + SeqScan` |
| Seleccion parcial de columnas | agrega `ProjectionOperator` |
| `INSERT INTO personas VALUES (...)` | `Insert` |
| `UPDATE ... SET ... [WHERE ...]` | `Update` |
| `DELETE FROM ... [WHERE ...]` | `Delete` |

## CLI y persistencia

En la primera ejecucion, `main_app` crea `personas_sgbd.db` y carga:

| id | nombre | ciudad | profesion |
|---:|---|---|---|
| 101 | Ana Torres | Arequipa | Ingeniera |
| 102 | Luis Mendoza | Lima | Medico |
| 103 | Carla Rojas | Cusco | Arquitecta |
| 104 | Diego Salazar | Arequipa | Analista de Datos |
| 105 | Sofia Vargas | Trujillo | Abogada |

Ejemplo:

```text
mini-sgbd> SELECT nombre, profesion FROM personas WHERE ciudad = 'Arequipa';
nombre         profesion
-------------  -----------------
Ana Torres     Ingeniera
Diego Salazar  Analista de Datos
Filas: 2
Plan: Filter + SeqScan
Tiempo: 0.120 ms
Buffer hits: 1
Buffer misses: 0
Hit ratio: 100.00 %
Lecturas de disco: 0
Escrituras de disco: 0
Costo I/O: 0 operaciones de pagina
Visualizacion: build/query_profile.html
```

Comandos propios de la CLI:

```text
help
visualize
car-demo
exit
quit
```

Las metricas son diferencias de contadores antes y despues de cada
consulta. Una consulta atendida desde el Buffer Pool puede reportar cero
operaciones de disco.

## Visual Query Profiler

Cada consulta reemplaza `build/query_profile.html` con un reporte
autocontenido que incluye:

1. Grafo del plan fisico.
2. Tiempo inclusivo y propio por operador.
3. Linea temporal de `Open`, `Next` y `Close`.
4. Hits, misses, hit ratio y costo de I/O.
5. Evolucion de T1/T2, B1/B2 y `p`.
6. Exportacion del perfil a JSON.

La traza se activa en `main_app` y permanece desactivada durante el
benchmark para no sesgar sus tiempos.

```powershell
.\build\Debug\main_app.exe
Start-Process .\build\query_profile.html
```

Para generar una carga CAR determinista:

```text
mini-sgbd> car-demo
Demostracion CAR generada: build/car_demo.html
```

La arquitectura del visualizador esta en
[`docs/visualizacion.md`](docs/visualizacion.md).

## Estructura principal

```text
include/
|-- storage/
|   |-- person_record.h
|   |-- table_page.h
|   |-- table_heap.h
|   |-- catalog_page.h
|   `-- catalog_manager.h
|-- buffer/
|-- index/
`-- query/
    |-- tuple.h
    |-- query.h
    |-- parser.h
    |-- operator.h
    |-- *_operator.h
    |-- query_executor.h
    |-- query_profiler.h
    |-- query_visualizer.h
    `-- cli.h

src/
|-- storage/
|-- buffer/
|-- index/
|-- query/
|-- benchmark/
`-- main.cpp

tests/                  12 suites automatizadas
docs/                   informe, resultados y evidencias
data/                   archivos de datos generados
```

## Compilacion

Requisitos:

- CMake 3.12 o superior.
- Compilador compatible con C++17.

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Con Visual Studio:

```powershell
.\build\Debug\main_app.exe
```

Con un generador de configuracion unica:

```powershell
.\build\main_app.exe
```

## Pruebas

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Las 12 suites cubren parser, operadores, CLI, profiler, visualizador,
persistencia, Buffer Pool, CAR, indice y casos de estres. Entre otros casos,
validan:

- Campos de texto, espacios y comillas SQL escapadas.
- `IndexScan` mediante `id -> RID`.
- Filtros textuales mediante `Filter + SeqScan`.
- Varios splits, 5,000 claves, duplicados y claves inexistentes.
- Limite del directorio hash.
- Rollback de una insercion cuando falla el indice.
- Persistencia de `INSERT`, `UPDATE` y `DELETE`.
- Rechazo de catalogos invalidos, incompletos o incompatibles.
- Plan fisico, fases Volcano y eventos CAR del reporte HTML.

## Benchmark

`benchmark_load` crea bases independientes, las reabre y ejecuta las
consultas a traves de `QueryExecutor` y `QueryProfiler`. Por defecto realiza
cinco repeticiones y calcula media y desviacion estandar muestral.

```powershell
.\build\Debug\benchmark_load.exe
```

Ejecucion corta:

```powershell
.\build\Debug\benchmark_load.exe .\build\benchmark_smoke.csv 1000 10 3
```

Genera CSV de resumen, CSV crudo y una grafica SVG sin depender de Python.
El codigo carga personas sinteticas deterministas; los dos modos consultan
el mismo predicado `id = N`.

## Compatibilidad del archivo binario

El esquema de cuatro columnas usa `CATALOG_VERSION = 3`. Los archivos de
las versiones anteriores almacenaban pares de enteros y no se
reinterpretan automaticamente. `main_app` usa el archivo nuevo
`personas_sgbd.db`, por lo que un `mini_sgbd.db` anterior queda intacto.

Si se abre deliberadamente un catalogo v1 o v2 con este motor, se devuelve
un error de version incompatible en vez de leer registros con un formato
incorrecto.

## Limitaciones

- El catalogo administra una tabla y un indice.
- Los borrados reutilizan slots, pero falta compactacion global y una lista
  de paginas libres.
- Cada `WHERE` admite una comparacion; faltan `AND`, `OR`, joins y
  agregaciones.
- El directorio hash ocupa una pagina y tiene una profundidad maxima.
- El rollback local no reemplaza un WAL frente a un cierre abrupto.
- Los textos tienen limites fijos por el diseno de registros paginados.
- La traza conserva hasta 2,000 eventos Volcano y 1,000 eventos CAR; los
  acumulados por operador siguen siendo completos.

## Resumen

```text
SQL -> Parser -> Plan Volcano -> Ejecucion -> Perfil -> Visualizacion
```

El proyecto ya permite almacenar y consultar personas con un esquema
realista, comparar acceso indexado y secuencial, observar CAR y relacionar
el plan fisico con el tiempo y el costo de I/O.
