#include "query/car_visualizer_demo.h"

#include <chrono>
#include <stdexcept>
#include <vector>

#include "buffer/car_replacer.h"
#include "query/execution_trace.h"
#include "query/query_profiler.h"
#include "query/query_visualizer.h"

namespace minisgbd {

void GenerateCARDemoReport(const std::string &path) {
  constexpr std::size_t kCapacity = 4;
  CARReplacer replacer(kCapacity);
  ExecutionTracer tracer(
      "CAR DEMO: 1,2,3,4,1,2,5,3,4,5,1");
  const int operator_id = tracer.RegisterOperator(
      "CAR Workload",
      "capacidad=4; combina recencia, frecuencia y presion de reemplazo");

  tracer.RecordCARSnapshot("INICIO", replacer.GetSnapshot());
  replacer.SetEventObserver(
      [&tracer](const CAREvent &event) { tracer.RecordCAREvent(event); });

  std::vector<frame_id_t> free_frames = {0, 1, 2, 3};
  auto load_page = [&replacer, &free_frames](page_id_t page_id) {
    frame_id_t frame_id = INVALID_FRAME_ID;
    if (replacer.RecordAccess(page_id, &frame_id)) {
      return;
    }

    if (!free_frames.empty()) {
      frame_id = free_frames.front();
      free_frames.erase(free_frames.begin());
    } else if (!replacer.Victim(&frame_id)) {
      throw std::runtime_error(
          "CAR demo no encontro un frame expulsable.");
    }

    replacer.RecordInsertion(page_id, frame_id);
    replacer.Unpin(frame_id, false);
  };

  const auto start = ExecutionTracer::Clock::now();
  load_page(1);
  load_page(2);
  load_page(3);
  load_page(4);
  load_page(1);
  load_page(2);
  load_page(5);
  load_page(3);
  load_page(4);
  load_page(5);
  load_page(1);
  const auto end = ExecutionTracer::Clock::now();

  replacer.ClearEventObserver();
  const CARStateSnapshot final_state = replacer.GetSnapshot();
  tracer.RecordCARSnapshot("FIN", final_state);
  tracer.RecordOperatorEvent(operator_id, "Execute", start, end);

  ProfiledQueryResult result;
  result.plan_type = QueryPlanType::kCarDemo;
  result.metrics.elapsed_ms =
      std::chrono::duration<double, std::milli>(end - start).count();
  result.metrics.buffer_hits = final_state.hits;
  result.metrics.buffer_misses = final_state.misses;
  const uint64_t accesses =
      result.metrics.buffer_hits + result.metrics.buffer_misses;
  if (accesses != 0) {
    result.metrics.buffer_hit_ratio =
        static_cast<double>(result.metrics.buffer_hits) /
        static_cast<double>(accesses);
  }
  result.trace = tracer.Finish();
  QueryVisualizer::WriteHtml(result, path);
}

}  // namespace minisgbd
