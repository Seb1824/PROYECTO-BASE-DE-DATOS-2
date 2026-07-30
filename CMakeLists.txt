cmake_minimum_required(VERSION 3.12)
project(MiniSGBD_CAR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Directorio de cabeceras global
include_directories(include)

# 1. Archivos fuente comunes del motor (tus módulos + los de Kari)
set(ENGINE_SOURCES
    src/storage/disk_manager.cpp
    src/buffer/car_replacer.cpp
    src/buffer/buffer_pool_manager.cpp
    src/buffer/page.cpp
    src/index/extensible_hash_table.cpp
    src/query/cli.cpp
    src/query/filter_operator.cpp
    src/query/index_scan_operator.cpp
    src/query/parser.cpp
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
    src/query/seq_scan_operator.cpp
)

# 6. Definir las pruebas del operador de filtrado
add_executable(test_filter
    tests/test_filter.cpp
    src/query/filter_operator.cpp
    src/query/seq_scan_operator.cpp
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

# 10. Definir el Ejecutable de Benchmark (comparación con índice / sin índice)
add_executable(benchmark_load src/benchmark/benchmark_load.cpp ${ENGINE_SOURCES})

# Habilitar pruebas con CTest
enable_testing()
add_test(NAME HashIndexTest COMMAND test_hash)
add_test(NAME ParserTest COMMAND test_parser)
add_test(NAME SeqScanTest COMMAND test_seq_scan)
add_test(NAME FilterTest COMMAND test_filter)
add_test(NAME QueryExecutorTest COMMAND test_query_executor)
add_test(NAME CliTest COMMAND test_cli)
add_test(NAME QueryProfilerTest COMMAND test_query_profiler)
