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
    src/query/index_scan_operator.cpp
    src/query/parser.cpp
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

# 5. Definir el Ejecutable de Benchmark (comparación con índice / sin índice)
add_executable(benchmark_load src/benchmark/benchmark_load.cpp ${ENGINE_SOURCES})

# Habilitar pruebas con CTest
enable_testing()
add_test(NAME HashIndexTest COMMAND test_hash)
add_test(NAME ParserTest COMMAND test_parser)
