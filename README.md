# Mini-SGBD en C++ con reemplazo adaptativo CAR y perfilado del costo de consultas

Proyecto universitario de Bases de Datos II: un Sistema Gestor de Base de Datos (SGBD) educativo escrito en C++, con almacenamiento en paginas fisicas, un buffer pool con el algoritmo de reemplazo adaptativo **CAR** (Clock with Adaptive Replacement) y un indice hash extensible mapeado sobre el gestor de paginas.

## Estado Actual

El pipeline implementado hasta el momento es:

```text
archivo .db (disco) -> DiskManager -> BufferPoolManager (CAR) -> ExtensibleHashTable -> IndexScanOperator (Volcano)
```

El avance actual incluye:

1. Gestor de almacenamiento (`DiskManager`) para lectura y escritura de paginas fisicas de tamano fijo.
2. Abstraccion de pagina en memoria (`Page`) con control de pin count y dirty bit.
3. Algoritmo de reemplazo adaptativo **CAR** (`CARReplacer`), con listas T1/T2 y listas fantasma B1/B2.
4. `BufferPoolManager` como orquestador entre disco y el algoritmo de reemplazo, con contadores propios de hits/misses.
5. Indice hash extensible (`ExtensibleHashTable`) con directorio dinamico y buckets, mapeado sobre el `BufferPoolManager`.
6. Proteccion contra insercion de claves duplicadas (evita recursion infinita en el split de buckets).
7. Operador fisico `IndexScanOperator` siguiendo el Modelo Volcano (`Open()`, `Next()`, `Close()`).
8. Demo interactivo (`main.cpp`) que valida el flujo completo: insercion, busqueda puntual y ejecucion via Volcano.
9. Suite de pruebas unitarias (`test_hash.cpp`) para el indice.
10. Benchmark de carga masiva (`benchmark_load.cpp`) comparando busqueda "con indice" vs "sin indice" (escaneo secuencial), con exportacion a CSV.
11. Script de graficacion (`plot_benchmark.py`) que genera la comparacion visual a partir del CSV.

## Estructura del Proyecto

```text
PROYECTO-BASE-DE-DATOS-2/
├── CMakeLists.txt
├── .gitignore
├── README.md
├── include/
│   ├── buffer/       (buffer_pool_manager.h, car_replacer.h, page.h)
│   ├── index/         (extensible_hash_table.h, hash_bucket_page.h, hash_directory_page.h)
│   ├── query/          (index_scan_operator.h)
│   └── storage/        (disk_manager.h)
├── src/
│   ├── main.cpp          # Demo interactivo en consola
│   ├── buffer/            (buffer_pool_manager.cpp, car_replacer.cpp, page.cpp)
│   ├── index/              (extensible_hash_table.cpp)
│   ├── query/               (index_scan_operator.cpp)
│   ├── storage/               (disk_manager.cpp)
│   └── benchmark/              (benchmark_load.cpp)
├── tests/
│   └── test_hash.cpp      # Suite de pruebas unitarias del indice
├── docs/
│   ├── resultados_benchmark.csv
│   ├── plot_benchmark.py
│   └── comparacion_busqueda.png
└── data/                    # Carpeta para archivos .db generados
```

## Componentes

### Modulo de Almacenamiento y Buffer (`storage/`, `buffer/`)

- `disk_manager.cpp/.h`: creacion de archivos de base de datos en disco, lectura y escritura de paginas fisicas de tamano fijo.
- `page.cpp/.h`: abstraccion de pagina en memoria (pin count, dirty bit, datos crudos).
- `car_replacer.cpp/.h`: implementacion del algoritmo **CAR**. Mantiene dos listas de paginas activas (T1, T2) y dos listas fantasma de historial (B1, B2) para adaptar dinamicamente el parametro `p`, que decide cuanto peso dar a recencia vs frecuencia al elegir que pagina expulsar.
- `buffer_pool_manager.cpp/.h`: orquestador principal. Entrega paginas (`FetchPage`, `NewPage`, `UnpinPage`) coordinando el `DiskManager` y el `CARReplacer`. Expone contadores propios de **hits, misses y hit ratio** (`GetHitCount()`, `GetMissCount()`, `GetHitRatio()`), listos para usarse en el CLI Profiler.

### Modulo de Indexacion (`index/`)

- `extensible_hash_table.cpp/.h`: indice hash extensible con directorio dinamico y buckets. Usa el `BufferPoolManager` para pedir, modificar y persistir sus paginas — no vive aislado en RAM. Metodos clave: `Insert(clave, valor)` y `GetValue(clave, &valor)`.
- Al insertar una clave que ya existe, `Insert()` la rechaza (retorna `false`) en vez de intentar dividir el bucket, evitando una recursion infinita cuando dos claves identicas nunca pueden separarse por hash.
- `hash_bucket_page.h` / `hash_directory_page.h`: estructuras de pagina fisica para buckets y el directorio del indice.

### Modulo de Consultas (`query/`) — parcialmente implementado

- `index_scan_operator.cpp/.h`: operador fisico que sigue el Modelo Volcano (`Open()`, `Next(RID*, int*)`, `Close()`) para iterar sobre el indice hash de forma estandar.
- **Pendiente**: no existe todavia un Parser para `SELECT`/`WHERE`, ni operadores adicionales del modelo Volcano (por ejemplo, un `FilterOperator`). Solo esta implementado el escaneo por indice.

### Benchmark de Carga Masiva (`src/benchmark/`)

- `benchmark_load.cpp`: compara el rendimiento de busqueda "con indice" (`ExtensibleHashTable`) vs "sin indice" (escaneo secuencial de paginas reutilizando `HashBucketPage` como heap), para multiples tamanos de N (1,000 a 100,000 registros).
- Exporta resultados a `docs/resultados_benchmark.csv`: tiempo de insercion, tiempo de busqueda, hits, misses y hit ratio del `BufferPoolManager` para cada modo y tamano.
- `docs/plot_benchmark.py` genera `docs/comparacion_busqueda.png` a partir del CSV (requiere `pandas` y `matplotlib`).

**Hallazgo principal**: con `pool_size = 10`, existe un punto de cruce (*crossover*) alrededor de N=5,000-10,000 registros. Por debajo de ese umbral, el overhead de indireccion del hash (dos `FetchPage` por consulta: directorio + bucket) pesa mas que un escaneo lineal corto. Por encima, el indice escala de forma casi plana mientras que el escaneo secuencial se degrada fuertemente (hasta ~104x mas lento en N=100,000), principalmente porque el hit ratio del buffer pool colapsa en el escaneo secuencial a medida que crece N.

## Configuracion de Compilacion (CMake)

Requiere CMake >= 3.12 y C++17. Se compila en Windows con MSVC (`cl.exe`). Existen tres targets independientes:

- **`main_app`**: demo interactivo de integracion completa.
  ```powershell
  cmake --build . --target main_app
  .\Debug\main_app.exe
  ```
- **`test_hash`**: pruebas unitarias del indice.
  ```powershell
  cmake --build . --target test_hash
  ctest -C Debug --output-on-failure
  ```
- **`benchmark_load`**: benchmark de carga masiva con indice vs sin indice.
  ```powershell
  cmake --build . --target benchmark_load
  .\Debug\benchmark_load.exe
  ```

Todos los targets comparten el mismo conjunto de fuentes del motor (`ENGINE_SOURCES` en `CMakeLists.txt`).

## Pendientes 

- Implementar un **Parser basico** para procesar sentencias `SELECT` y clausulas `WHERE`.
- Disenar los **operadores fisicos restantes** del modelo Volcano (ademas de `IndexScanOperator`), por ejemplo un operador de filtro, siguiendo el mismo estandar `Open()`/`Next()`/`Close()`.
- Construir el **CLI Profiler**: imprimir en pantalla el costo de las consultas usando los contadores ya disponibles en `BufferPoolManager` (`GetHitCount()`, `GetMissCount()`, `GetHitRatio()`) y medir el tiempo exacto de ejecucion.

## Pruebas

Ejecutar con CTest desde la carpeta de build:

```powershell
ctest -C Debug --output-on-failure
```

Cobertura actual de pruebas:

- Insercion y busqueda basica en el indice hash extensible.
- Manejo de division de buckets (split) al superar la capacidad.
- Rechazo de claves duplicadas.

## Resumen

El nucleo de almacenamiento, buffer y el indice ya estan implementados, integrados y validados end-to-end mediante el demo (`main_app`) y el benchmark de carga masiva, que ademas confirma con datos reales el comportamiento asintotico esperado del indice frente a un escaneo secuencial. Lo que falta por desarrollar es la capa de procesamiento de consultas (parser, operadores adicionales y profiler).
