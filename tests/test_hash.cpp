#include <iostream>
#include <cassert>
#include <cstdio>
#include "storage/disk_manager.h"
#include "buffer/car_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "index/extensible_hash_table.h"
#include "query/index_scan_operator.h"

using namespace minisgbd;

void RunTests() {
    std::cout << "=== INICIANDO PRUEBAS DEL MOTOR (Hash + Volcano Scan) ===" << std::endl;

    // 1. Configurar componentes de almacenamiento y búfer (con CARReplacer)
    std::string db_file = "test_engine.db";
    std::remove(db_file.c_str()); // Limpiar archivo previo si existe

    DiskManager disk_manager(db_file);
    size_t pool_size = 10;
    CARReplacer replacer(pool_size); // Política CAR implementada por Kari
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    
    // El índice hash solo recibe el BufferPoolManager
    ExtensibleHashTable hash_index(&bpm);

    // 2. Probar inserciones en el Índice Hash Extensible
    std::cout << "[Test] Insertando claves en el indice hash..." << std::endl;
    for (int i = 1; i <= 10; ++i) {
        page_id_t dummy_page_id = i * 100; // Simulando ID de página
        bool inserted = hash_index.Insert(i, dummy_page_id);
        assert(inserted == true);
    }

    // 3. Probar búsquedas directas
    std::cout << "[Test] Verificando busquedas directas..." << std::endl;
    page_id_t found_page = 0;
    bool found = hash_index.GetValue(5, &found_page);
    assert(found == true);
    assert(found_page == 500);
    std::cout << " > Clave 5 encontrada en pagina: " << found_page << std::endl;

    // 4. Probar el Operador Volcano (IndexScanOperator)
    std::cout << "[Test] Probando IndexScanOperator (Modelo Volcano)..." << std::endl;
    int search_target = 7;
    IndexScanOperator scan_op(&hash_index, &bpm, search_target);

    // Patrón Volcano: Open -> Next -> Close
    scan_op.Open();

    RID out_rid;
    int out_val = 0;
    bool has_data = scan_op.Next(&out_rid, &out_val);

    assert(has_data == true);
    assert(out_val == 7);
    assert(out_rid.page_id == 700);
    std::cout << " > Volcano Scan exitoso para clave: " << out_val << " -> PageID: " << out_rid.page_id << std::endl;

    scan_op.Close();

    // Limpieza final del archivo de prueba
    std::remove(db_file.c_str());
    std::cout << "=== TODAS LAS PRUEBAS PASARON EXITOSAMENTE ===" << std::endl;
}

int main() {
    RunTests();
    return 0;
}