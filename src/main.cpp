#include <iostream>
#include <cstdio>
#include <string>

#include "storage/disk_manager.h"
#include "buffer/car_replacer.h"
#include "buffer/buffer_pool_manager.h"
#include "index/extensible_hash_table.h"
#include "query/index_scan_operator.h"

using namespace minisgbd;

void PrintHeader() {
    std::cout << "\n============================================\n";
    std::cout << "      MINI-SGBD: DEMO INTERACTIVO DE MOTOR\n";
    std::cout << "============================================\n";
}

int main() {
    std::string db_file = "demo_engine.db";
    std::remove(db_file.c_str());

    DiskManager disk_manager(db_file);
    size_t pool_size = 10;
    CARReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    
    ExtensibleHashTable hash_index(&bpm);

    PrintHeader();
    std::cout << "[INFO] Motor inicializado correctamente en disco.\n";
    std::cout << "[INFO] Gestor de Bufer (CAR) y Indice Hash listos.\n";

    std::cout << "\n--------------------------------------------\n";
    std::cout << ">> [PASO 1] Insertando registros en el indice...\n";
    std::cout << "--------------------------------------------\n";
    
    for (int id = 101; id <= 105; ++id) {
        page_id_t page_simulada = id * 5; 
        bool exito = hash_index.Insert(id, page_simulada);
        
        if (exito) {
            std::cout << "   -> Insertado con exito: Clave ID [" << id 
                      << "] mapeada a Pagina Fisica [" << page_simulada << "]\n";
        }
    }

    std::cout << "\n--------------------------------------------\n";
    std::cout << ">> [PASO 2] Buscando una clave especifica...\n";
    std::cout << "--------------------------------------------\n";
    
    int clave_a_buscar = 103;
    page_id_t pagina_encontrada = 0;
    
    std::cout << "   Buscando Clave: " << clave_a_buscar << " ...\n";
    bool encontrado = hash_index.GetValue(clave_a_buscar, &pagina_encontrada);

    if (encontrado) {
        std::cout << "   [RESULTADO] ¡Encontrado! La clave " << clave_a_buscar 
                  << " apunta a la Pagina: " << pagina_encontrada << "\n";
    } else {
        std::cout << "   [RESULTADO] La clave no existe.\n";
    }

    std::cout << "\n--------------------------------------------\n";
    std::cout << ">> [PASO 3] Ejecutando consulta con Modelo Volcano...\n";
    std::cout << "--------------------------------------------\n";
    
    int clave_consulta = 104;
    std::cout << "   Lanzando operador IndexScan para la clave: " << clave_consulta << "\n";
    
    IndexScanOperator scan_op(&hash_index, &bpm, clave_consulta);
    scan_op.Open();

    RID rid_resultado;
    int val_resultado = 0;
    bool hay_datos = scan_op.Next(&rid_resultado, &val_resultado);

    if (hay_datos) {
        std::cout << "   [VOLCANO NEXT()] Registro recuperado -> Clave: " << val_resultado 
                  << " | PageID: " << rid_resultado.page_id << "\n";
    } else {
        std::cout << "   [VOLCANO NEXT()] Fin del flujo (Sin resultados).\n";
    }

    scan_op.Close();

    std::remove(db_file.c_str());
    
    std::cout << "\n========================\n";
    std::cout << " FINALIZADO CON EXITO\n";
    std::cout << "==========================\n";

    return 0;
}