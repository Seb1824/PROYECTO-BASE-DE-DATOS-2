#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "buffer/buffer_pool_manager.h"
#include "buffer/car_replacer.h"
#include "buffer/page.h"
#include "storage/disk_manager.h"

using namespace minisgbd;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

void TestCarRejectsVictimWhenAllFramesArePinned() {
  CARReplacer replacer(2);
  replacer.RecordInsertion(10, 0);
  replacer.RecordInsertion(20, 1);

  frame_id_t victim = INVALID_FRAME_ID;
  Expect(replacer.Size() == 0,
         "Los frames recien insertados deben permanecer fijados.");
  Expect(!replacer.Victim(&victim),
         "CAR no debe elegir una victima si todos los frames estan fijados.");

  replacer.Unpin(0, false);
  Expect(replacer.Size() == 1,
         "Unpin debe incrementar la cantidad de frames expulsables.");

  replacer.Pin(0);
  Expect(replacer.Size() == 0,
         "Pin debe retirar el frame de los candidatos expulsables.");
  Expect(!replacer.Victim(&victim),
         "CAR debe terminar y retornar false sin candidatos.");

  replacer.Unpin(0, false);
  frame_id_t accessed_frame = INVALID_FRAME_ID;
  Expect(replacer.RecordAccess(10, &accessed_frame),
         "RecordAccess debe reconocer una pagina residente.");
  Expect(accessed_frame == 0,
         "RecordAccess debe devolver el frame correcto.");
  Expect(replacer.Victim(&victim) && victim == 0,
         "CAR debe expulsar el unico frame disponible.");
  Expect(replacer.Size() == 0,
         "La victima debe eliminarse de los frames expulsables.");
}

void TestPinnedPageCannotBeEvicted() {
  const std::string db_file = "test_pinned_page.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(1);
    BufferPoolManager bpm(1, &disk_manager, &replacer);

    page_id_t first_page_id = INVALID_PAGE_ID;
    Expect(bpm.NewPage(&first_page_id) != nullptr,
           "Debe crear la primera pagina.");

    page_id_t blocked_page_id = 123;
    Expect(bpm.NewPage(&blocked_page_id) == nullptr,
           "No debe expulsar una pagina que sigue fijada.");
    Expect(blocked_page_id == INVALID_PAGE_ID,
           "NewPage debe informar INVALID_PAGE_ID cuando no hay frame.");

    Expect(bpm.UnpinPage(first_page_id, false),
           "Debe poder liberar la primera pagina.");

    page_id_t second_page_id = INVALID_PAGE_ID;
    Expect(bpm.NewPage(&second_page_id) != nullptr,
           "Debe crear otra pagina despues de Unpin.");
    Expect(second_page_id != first_page_id,
           "Cada pagina nueva debe recibir un identificador distinto.");
    Expect(bpm.UnpinPage(second_page_id, false),
           "Debe liberar la segunda pagina.");
  }

  std::remove(db_file.c_str());
}

void TestPinCountPreventsEarlyEviction() {
  const std::string db_file = "test_pin_count.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(1);
    BufferPoolManager bpm(1, &disk_manager, &replacer);

    page_id_t page_id = INVALID_PAGE_ID;
    Expect(bpm.NewPage(&page_id) != nullptr, "Debe crear una pagina.");
    Expect(bpm.FetchPage(page_id) != nullptr,
           "FetchPage debe incrementar el pin count.");

    Expect(bpm.UnpinPage(page_id, false),
           "El primer Unpin debe reducir el pin count.");

    page_id_t blocked_page_id = INVALID_PAGE_ID;
    Expect(bpm.NewPage(&blocked_page_id) == nullptr,
           "Un pin restante debe impedir la expulsion.");

    Expect(bpm.UnpinPage(page_id, false),
           "El segundo Unpin debe volver la pagina expulsable.");

    page_id_t replacement_page_id = INVALID_PAGE_ID;
    Expect(bpm.NewPage(&replacement_page_id) != nullptr,
           "La pagina debe poder expulsarse al llegar a pin count cero.");
    Expect(bpm.UnpinPage(replacement_page_id, false),
           "Debe liberar la pagina de reemplazo.");
  }

  std::remove(db_file.c_str());
}

void TestDirtyPageSurvivesEviction() {
  const std::string db_file = "test_dirty_eviction.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(1);
    BufferPoolManager bpm(1, &disk_manager, &replacer);

    page_id_t first_page_id = INVALID_PAGE_ID;
    Page *first_page = bpm.NewPage(&first_page_id);
    Expect(first_page != nullptr, "Debe crear la pagina sucia.");

    const char expected[] = "contenido persistido por eviction";
    std::memcpy(first_page->get_data(), expected, sizeof(expected));
    Expect(bpm.UnpinPage(first_page_id, true),
           "Debe marcar la pagina como sucia.");

    page_id_t second_page_id = INVALID_PAGE_ID;
    Expect(bpm.NewPage(&second_page_id) != nullptr,
           "La segunda pagina debe expulsar la primera.");
    Expect(bpm.UnpinPage(second_page_id, false),
           "Debe liberar la segunda pagina.");

    Page *reloaded_page = bpm.FetchPage(first_page_id);
    Expect(reloaded_page != nullptr,
           "Debe recargar la pagina expulsada desde disco.");
    if (reloaded_page != nullptr) {
      Expect(std::memcmp(reloaded_page->get_data(), expected,
                         sizeof(expected)) == 0,
             "La pagina sucia debe conservar su contenido tras la expulsion.");
      Expect(bpm.UnpinPage(first_page_id, false),
             "Debe liberar la pagina recargada.");
    }
  }

  std::remove(db_file.c_str());
}

void TestDestructorFlushesDirtyPages() {
  const std::string db_file = "test_destructor_flush.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(1);
    page_id_t page_id = INVALID_PAGE_ID;
    const char expected[] = "flush al destruir buffer pool";
    uint64_t writes_before_destructor = 0;

    {
      BufferPoolManager bpm(1, &disk_manager, &replacer);
      Page *page = bpm.NewPage(&page_id);
      Expect(page != nullptr, "Debe crear la pagina para probar el flush.");
      std::memcpy(page->get_data(), expected, sizeof(expected));
      Expect(bpm.UnpinPage(page_id, true),
             "Debe marcar la pagina del destructor como sucia.");
      writes_before_destructor = disk_manager.GetWriteCount();
    }

    Expect(disk_manager.GetWriteCount() == writes_before_destructor + 1,
           "El destructor debe escribir las paginas sucias.");

    char persisted[PAGE_SIZE];
    std::memset(persisted, 0, PAGE_SIZE);
    disk_manager.read_page(page_id, persisted);
    Expect(std::memcmp(persisted, expected, sizeof(expected)) == 0,
           "El destructor debe persistir los datos antes de liberar memoria.");
  }

  std::remove(db_file.c_str());
}

void TestRepeatedEvictionsPreserveAllPages() {
  const std::string db_file = "test_repeated_evictions.db";
  std::remove(db_file.c_str());

  {
    constexpr int kPageCount = 20;
    DiskManager disk_manager(db_file);
    CARReplacer replacer(3);
    BufferPoolManager bpm(3, &disk_manager, &replacer);
    page_id_t page_ids[kPageCount];

    for (int value = 0; value < kPageCount; ++value) {
      Page *page = bpm.NewPage(&page_ids[value]);
      Expect(page != nullptr,
             "CAR debe encontrar victimas durante la carga de presion.");
      if (page == nullptr) {
        break;
      }

      std::memcpy(page->get_data(), &value, sizeof(value));
      Expect(bpm.UnpinPage(page_ids[value], true),
             "Debe liberar cada pagina de la carga de presion.");
    }

    for (int expected = 0; expected < kPageCount; ++expected) {
      Page *page = bpm.FetchPage(page_ids[expected]);
      Expect(page != nullptr,
             "Debe recuperar todas las paginas despues de varias expulsiones.");
      if (page == nullptr) {
        continue;
      }

      int stored_value = -1;
      std::memcpy(&stored_value, page->get_data(), sizeof(stored_value));
      Expect(stored_value == expected,
             "Cada pagina recuperada debe conservar su contenido.");
      Expect(bpm.UnpinPage(page_ids[expected], false),
             "Debe liberar cada pagina recuperada.");
    }
  }

  std::remove(db_file.c_str());
}

void TestCarSnapshotsAndEvents() {
  CARReplacer replacer(2);
  std::vector<CAREvent> events;
  replacer.SetEventObserver(
      [&events](const CAREvent &event) { events.push_back(event); });

  replacer.RecordInsertion(10, 0);
  replacer.Unpin(0, false);

  frame_id_t accessed_frame = INVALID_FRAME_ID;
  Expect(replacer.RecordAccess(10, &accessed_frame),
         "La pagina residente debe producir un hit observable.");

  frame_id_t victim = INVALID_FRAME_ID;
  Expect(replacer.Victim(&victim) && victim == 0,
         "CAR debe expulsar la pagina despues de su segunda oportunidad.");

  const CARStateSnapshot snapshot = replacer.GetSnapshot();
  Expect(snapshot.t1.empty() && snapshot.t2.empty(),
         "La victima no debe permanecer en las listas residentes.");
  Expect(snapshot.b2.size() == 1 && snapshot.b2[0] == 10,
         "La pagina frecuente expulsada debe quedar en B2.");
  Expect(snapshot.hits == 1 && snapshot.misses == 0,
         "El snapshot debe exponer los contadores CAR.");

  bool observed_hit = false;
  bool observed_promotion = false;
  bool observed_eviction = false;
  for (const CAREvent &event : events) {
    observed_hit = observed_hit || event.type == "HIT";
    observed_promotion =
        observed_promotion || event.type == "PROMOTE_T1_T2";
    observed_eviction =
        observed_eviction || event.type == "EVICT_T2_B2";
  }
  Expect(observed_hit && observed_promotion && observed_eviction,
         "El observador debe capturar hit, promocion y expulsion.");

  replacer.ClearEventObserver();
}

void TestInvalidBufferPoolConfiguration() {
  const std::string db_file = "test_invalid_buffer_pool.db";
  std::remove(db_file.c_str());

  {
    DiskManager disk_manager(db_file);
    CARReplacer replacer(1);

    try {
      BufferPoolManager bpm(0, &disk_manager, &replacer);
      Expect(false, "Debe rechazar un pool de tamano cero.");
    } catch (const std::invalid_argument &) {
      // Resultado esperado.
    }

    try {
      BufferPoolManager bpm(1, nullptr, &replacer);
      Expect(false, "Debe rechazar un DiskManager nulo.");
    } catch (const std::invalid_argument &) {
      // Resultado esperado.
    }
  }

  std::remove(db_file.c_str());
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DE CAR Y BUFFER POOL ===\n";

  TestCarRejectsVictimWhenAllFramesArePinned();
  TestPinnedPageCannotBeEvicted();
  TestPinCountPreventsEarlyEviction();
  TestDirtyPageSurvivesEviction();
  TestDestructorFlushesDirtyPages();
  TestRepeatedEvictionsPreserveAllPages();
  TestCarSnapshotsAndEvents();
  TestInvalidBufferPoolConfiguration();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas de CAR y BufferPool pasaron correctamente.\n";
  return 0;
}
