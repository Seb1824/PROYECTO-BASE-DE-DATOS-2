cmake_minimum_required(VERSION 3.12)
project(MiniSGBD_CAR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Directorio de cabeceras global
include_directories(include)

# 1. Archivos fuente comunes del motor (tus módulos + los de Kari)
set(ENGINE_SOURCES
    src/storage/catalog_manager.cpp
    src/storage/disk_manager.cpp
    src/storage/table_heap.cpp
    src/buffer/car_replacer.cpp
    src/buffer/buffer_pool_manager.cpp
    src/buffer/page.cpp
    src/index/extensible_hash_table.cpp
    src/query/cli.cpp
    src/query/filter_operator.cpp
    src/query/index_scan_operator.cpp
    src/query/parser.cpp
    src/query/projection_operator.cpp
    src/query/query_executor.cpp
    src/query/query_profiler.cpp
    src/query/seq_scan_operator.cpp
)

# 2. Definir el Ejecutable Principal (Tu script de validación / sandbox)
add_executable(main_app src/main.cpp ${ENGINE_SOURCES})

# 3. Definir el Ejecutable de Pruebas Unitarias (Tus tests actuales)
add_executable(test_hash tests/test_hash.cpp ${ENGINE_SOURCES})

# 4. Definir las pruebas aisladas del parser de consultas
add_executable(test_parser
    tests/test_parser.cpp
    src/query/parser.cpp
)

# 5. Definir las pruebas aisladas del escaneo secuencial
add_executable(test_seq_scan
    tests/test_seq_scan.cpp
    ${ENGINE_SOURCES}
)

# 6. Definir las pruebas del operador de filtrado
add_executable(test_filter
    tests/test_filter.cpp
    ${ENGINE_SOURCES}
)

# 7. Definir las pruebas de integracion del ejecutor de consultas
add_executable(test_query_executor
    tests/test_query_executor.cpp
    ${ENGINE_SOURCES}
)

# 8. Definir las pruebas de la interfaz de consola
add_executable(test_cli
    tests/test_cli.cpp
    ${ENGINE_SOURCES}
)

# 9. Definir las pruebas del profiler de consultas
add_executable(test_query_profiler
    tests/test_query_profiler.cpp
    ${ENGINE_SOURCES}
)

# 10. Definir las pruebas de CAR y Buffer Pool
add_executable(test_buffer_pool
    tests/test_buffer_pool.cpp
    src/storage/disk_manager.cpp
    src/buffer/car_replacer.cpp
    src/buffer/buffer_pool_manager.cpp
    src/buffer/page.cpp
)

# 11. Definir pruebas de tabla fisica y recuperacion del indice
add_executable(test_persistence
    tests/test_persistence.cpp
    ${ENGINE_SOURCES}
)

# 12. Definir el Ejecutable de Benchmark (comparación con índice / sin índice)
add_executable(benchmark_load src/benchmark/benchmark_load.cpp ${ENGINE_SOURCES})

# 13. Pruebas extendidas de splits, reinicio, limites y catalogo
add_executable(test_index_stress
    tests/test_index_stress.cpp
    ${ENGINE_SOURCES}
)

# 14. Pruebas del operador de proyeccion Volcano
add_executable(test_projection
    tests/test_projection.cpp
    ${ENGINE_SOURCES}
)

# Habilitar pruebas con CTest
enable_testing()
add_test(NAME HashIndexTest COMMAND test_hash)
add_test(NAME ParserTest COMMAND test_parser)
add_test(NAME SeqScanTest COMMAND test_seq_scan)
add_test(NAME FilterTest COMMAND test_filter)
add_test(NAME QueryExecutorTest COMMAND test_query_executor)
add_test(NAME CliTest COMMAND test_cli)
add_test(NAME QueryProfilerTest COMMAND test_query_profiler)
add_test(NAME BufferPoolTest COMMAND test_buffer_pool)
add_test(NAME PersistenceTest COMMAND test_persistence)
add_test(NAME IndexStressTest COMMAND test_index_stress)
add_test(NAME ProjectionTest COMMAND test_projection)
