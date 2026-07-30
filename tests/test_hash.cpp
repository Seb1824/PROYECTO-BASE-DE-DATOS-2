#include <cstdio>
#include <iostream>
#include <optional>
#include <string>

#include "storage/disk_manager.h"
#include "buffer/car_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "index/extensible_hash_table.h"
#include "query/index_scan_operator.h"
#include "storage/table_heap.h"

using namespace minisgbd;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "[FALLO] " << message << std::endl;
        ++failures;
    }
}

void TestIndexScanOperator(ExtensibleHashTable *hash_index,
                           TableHeap *table_heap) {
    std::cout << "[Test] Probando ciclo de vida de IndexScanOperator..."
              << std::endl;

    Tuple tuple;
    IndexScanOperator unopened_scan(hash_index, table_heap, 7);
    Expect(!unopened_scan.Next(&tuple),
           "Next() antes de Open() debe retornar false.");

    IndexScanOperator found_scan(hash_index, table_heap, 7);
    found_scan.Open();

    Expect(found_scan.Next(&tuple),
           "Next() debe encontrar una clave existente.");
    Expect(tuple.id == 7, "El Tuple debe contener el id buscado.");
    Expect(tuple.nombre == "Persona 7" &&
               tuple.ciudad == "Lima" &&
               tuple.profesion == "Profesion 7",
           "IndexScan debe recuperar la persona completa desde TableHeap.");
    Expect(!found_scan.Next(&tuple),
           "El segundo Next() debe indicar el fin de los resultados.");

    found_scan.Close();
    Expect(!found_scan.Next(&tuple),
           "Next() despues de Close() debe retornar false.");

    IndexScanOperator missing_scan(hash_index, table_heap, 999);
    missing_scan.Open();
    Expect(!missing_scan.Next(&tuple),
           "Una clave inexistente no debe producir resultados.");
    missing_scan.Close();
}

void RunTests() {
    std::cout << "=== INICIANDO PRUEBAS DEL MOTOR (Hash + Volcano Scan) ===" << std::endl;

    // 1. Configurar componentes de almacenamiento y búfer (con CARReplacer)
    std::string db_file = "test_engine.db";
    std::remove(db_file.c_str()); // Limpiar archivo previo si existe

    DiskManager disk_manager(db_file);
    size_t pool_size = 10;
    CARReplacer replacer(pool_size); // Política CAR implementada por Kari
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    TableHeap table_heap(&bpm);
    
    // El índice hash solo recibe el BufferPoolManager
    ExtensibleHashTable hash_index(&bpm);

    // 2. Probar inserciones en el Índice Hash Extensible
    std::cout << "[Test] Insertando claves en el indice hash..." << std::endl;
    for (int i = 1; i <= 10; ++i) {
        const PersonRecord person{
            i,
            "Persona " + std::to_string(i),
            "Lima",
            "Profesion " + std::to_string(i)};
        const std::optional<RID> rid =
            table_heap.InsertTuple(person);
        bool inserted =
            rid.has_value() && hash_index.Insert(i, rid->Encode());
        Expect(inserted, "La insercion de una clave nueva debe ser exitosa.");
    }

    // 3. Probar búsquedas directas
    std::cout << "[Test] Verificando busquedas directas..." << std::endl;
    int encoded_rid = 0;
    bool found = hash_index.GetValue(5, &encoded_rid);
    Expect(found, "La busqueda directa debe encontrar la clave 5.");
    PersonRecord found_person;
    const RID rid = RID::Decode(encoded_rid);
    Expect(table_heap.GetRecord(rid, &found_person),
           "El RID debe apuntar a una fila fisica.");
    Expect(found_person.id == 5 &&
               found_person.nombre == "Persona 5",
           "El id 5 debe recuperar la persona desde TableHeap.");
    std::cout << " > Clave 5 encontrada en RID: "
              << rid.page_id << ":" << rid.slot << std::endl;

    // 4. Probar el Operador Volcano (IndexScanOperator)
    TestIndexScanOperator(&hash_index, &table_heap);

    // Limpieza final del archivo de prueba
    std::remove(db_file.c_str());
}

}  // namespace

int main() {
    RunTests();

    if (failures != 0) {
        std::cerr << failures << " prueba(s) fallaron." << std::endl;
        return 1;
    }

    std::cout << "=== TODAS LAS PRUEBAS PASARON EXITOSAMENTE ===" << std::endl;
    return 0;
}
