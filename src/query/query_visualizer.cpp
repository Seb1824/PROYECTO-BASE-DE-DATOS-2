#include "query/query_visualizer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include "query/query_profiler.h"

namespace minisgbd {
namespace {

std::string PlanName(QueryPlanType plan_type) {
  switch (plan_type) {
    case QueryPlanType::kSeqScan:
      return "SeqScan";
    case QueryPlanType::kFilteredSeqScan:
      return "Filter + SeqScan";
    case QueryPlanType::kIndexScan:
      return "IndexScan";
    case QueryPlanType::kInsert:
      return "Insert";
    case QueryPlanType::kUpdate:
      return "Update";
    case QueryPlanType::kDelete:
      return "Delete";
    case QueryPlanType::kCarDemo:
      return "CAR Demo";
  }
  return "Desconocido";
}

std::string JsonString(const std::string &text) {
  std::ostringstream output;
  output << '"';
  for (unsigned char character : text) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      case '<':
        output << "\\u003c";
        break;
      case '>':
        output << "\\u003e";
        break;
      case '&':
        output << "\\u0026";
        break;
      default:
        if (character < 0x20) {
          output << "\\u" << std::hex << std::setw(4)
                 << std::setfill('0') << static_cast<int>(character)
                 << std::dec << std::setfill(' ');
        } else {
          output << static_cast<char>(character);
        }
    }
  }
  output << '"';
  return output.str();
}

void WritePageIds(std::ostream &output,
                  const std::vector<page_id_t> &page_ids) {
  output << '[';
  for (std::size_t index = 0; index < page_ids.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << page_ids[index];
  }
  output << ']';
}

void WriteCARState(std::ostream &output, const CARStateSnapshot &state) {
  output << "{\"capacity\":" << state.capacity
         << ",\"evictableCount\":" << state.evictable_count
         << ",\"p\":" << state.target_p << ",\"hits\":" << state.hits
         << ",\"misses\":" << state.misses << ",\"t1\":";
  WritePageIds(output, state.t1);
  output << ",\"t2\":";
  WritePageIds(output, state.t2);
  output << ",\"b1\":";
  WritePageIds(output, state.b1);
  output << ",\"b2\":";
  WritePageIds(output, state.b2);
  output << '}';
}

std::string BuildProfileJson(const ProfiledQueryResult &result) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(6);
  output << "{\"sql\":" << JsonString(result.trace.sql)
         << ",\"planName\":" << JsonString(PlanName(result.plan_type))
         << ",\"rowCount\":" << result.rows.size() << ",\"metrics\":{"
         << "\"elapsedMs\":" << result.metrics.elapsed_ms
         << ",\"bufferHits\":" << result.metrics.buffer_hits
         << ",\"bufferMisses\":" << result.metrics.buffer_misses
         << ",\"hitRatio\":" << result.metrics.buffer_hit_ratio
         << ",\"diskReads\":" << result.metrics.disk_reads
         << ",\"diskWrites\":" << result.metrics.disk_writes
         << ",\"ioOperations\":" << result.metrics.io_operations << "},";

  output << "\"operators\":[";
  for (std::size_t index = 0; index < result.trace.operators.size();
       ++index) {
    if (index != 0) {
      output << ',';
    }
    const OperatorProfile &profile = result.trace.operators[index];
    output << "{\"id\":" << profile.id
           << ",\"parentId\":" << profile.parent_id
           << ",\"name\":" << JsonString(profile.name)
           << ",\"detail\":" << JsonString(profile.detail)
           << ",\"openMs\":" << profile.open_ms
           << ",\"nextMs\":" << profile.next_ms
           << ",\"closeMs\":" << profile.close_ms
           << ",\"executeMs\":" << profile.execute_ms
           << ",\"inclusiveMs\":" << profile.inclusive_ms
           << ",\"selfMs\":" << profile.self_ms
           << ",\"openCalls\":" << profile.open_calls
           << ",\"nextCalls\":" << profile.next_calls
           << ",\"closeCalls\":" << profile.close_calls
           << ",\"executeCalls\":" << profile.execute_calls
           << ",\"rowsOut\":" << profile.rows_out << '}';
  }

  output << "],\"timeline\":[";
  for (std::size_t index = 0; index < result.trace.timeline.size();
       ++index) {
    if (index != 0) {
      output << ',';
    }
    const TimelineEvent &event = result.trace.timeline[index];
    output << "{\"sequence\":" << event.sequence
           << ",\"operatorId\":" << event.operator_id
           << ",\"category\":" << JsonString(event.category)
           << ",\"phase\":" << JsonString(event.phase)
           << ",\"startMs\":" << event.start_ms
           << ",\"durationMs\":" << event.duration_ms
           << ",\"rowsProduced\":" << event.rows_produced
           << ",\"detail\":" << JsonString(event.detail) << '}';
  }

  output << "],\"carEvents\":[";
  for (std::size_t index = 0; index < result.trace.car_events.size();
       ++index) {
    if (index != 0) {
      output << ',';
    }
    const CARTraceEvent &event = result.trace.car_events[index];
    output << "{\"sequence\":" << event.sequence
           << ",\"type\":" << JsonString(event.type)
           << ",\"pageId\":" << event.page_id
           << ",\"frameId\":" << event.frame_id
           << ",\"previousP\":" << event.previous_p
           << ",\"timestampMs\":" << event.timestamp_ms
           << ",\"state\":";
    WriteCARState(output, event.state);
    output << '}';
  }
  output << "],\"timelineTruncated\":"
         << (result.trace.timeline_truncated ? "true" : "false")
         << ",\"carEventsTruncated\":"
         << (result.trace.car_events_truncated ? "true" : "false")
         << '}';
  return output.str();
}

const char *HtmlBeforeData() {
  return R"HTML(<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Mini-SGBD · Visual Query Profiler</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #07111f;
      --panel: #0d1b2d;
      --panel-2: #11233a;
      --line: #27405f;
      --text: #edf6ff;
      --muted: #91a8c2;
      --cyan: #45d6c4;
      --blue: #67a9ff;
      --amber: #ffbc66;
      --rose: #ff718b;
      --violet: #b79cff;
      --good: #70e09b;
      --shadow: 0 18px 50px rgba(0, 0, 0, .28);
    }

    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-width: 320px;
      background: var(--bg);
      color: var(--text);
      font-family: Inter, ui-sans-serif, system-ui, -apple-system,
                   BlinkMacSystemFont, "Segoe UI", sans-serif;
    }

    button, input { font: inherit; }
    button { color: inherit; }

    .shell {
      width: min(1500px, calc(100% - 32px));
      margin: 0 auto;
      padding: 30px 0 56px;
    }

    .topbar {
      display: flex;
      align-items: flex-start;
      justify-content: space-between;
      gap: 24px;
      margin-bottom: 22px;
    }

    .eyebrow {
      color: var(--cyan);
      font-size: 12px;
      font-weight: 800;
      letter-spacing: .16em;
      text-transform: uppercase;
    }

    h1 {
      margin: 8px 0 6px;
      font-size: clamp(28px, 4vw, 48px);
      line-height: 1;
      letter-spacing: -.045em;
    }

    .subtitle {
      max-width: 760px;
      margin: 0;
      color: var(--muted);
      line-height: 1.55;
    }

    .download {
      flex: 0 0 auto;
      border: 1px solid var(--line);
      border-radius: 10px;
      background: var(--panel);
      padding: 10px 14px;
      cursor: pointer;
    }
    .download:hover { border-color: var(--cyan); }

    .query-card, .panel {
      border: 1px solid var(--line);
      background: var(--panel);
      box-shadow: var(--shadow);
    }

    .query-card {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 18px;
      align-items: center;
      border-radius: 16px;
      padding: 18px 20px;
      margin-bottom: 18px;
    }

    .query-label {
      display: block;
      margin-bottom: 7px;
      color: var(--muted);
      font-size: 11px;
      font-weight: 800;
      letter-spacing: .13em;
      text-transform: uppercase;
    }

    .query-text {
      overflow-wrap: anywhere;
      color: #dff7ff;
      font: 600 15px/1.5 ui-monospace, SFMono-Regular, Consolas, monospace;
    }

    .plan-pill {
      border: 1px solid rgba(69, 214, 196, .45);
      border-radius: 999px;
      background: rgba(69, 214, 196, .1);
      color: var(--cyan);
      padding: 8px 12px;
      font-size: 12px;
      font-weight: 800;
    }

    .metrics {
      display: grid;
      grid-template-columns: repeat(6, minmax(0, 1fr));
      gap: 12px;
      margin-bottom: 18px;
    }

    .metric {
      min-height: 112px;
      border: 1px solid var(--line);
      border-radius: 14px;
      background: var(--panel);
      padding: 16px;
    }
    .metric small {
      display: block;
      color: var(--muted);
      font-size: 11px;
      font-weight: 700;
      letter-spacing: .08em;
      text-transform: uppercase;
    }
    .metric strong {
      display: block;
      margin-top: 12px;
      font-size: clamp(22px, 3vw, 32px);
      letter-spacing: -.04em;
    }
    .metric span { color: var(--muted); font-size: 12px; }

    .grid-two {
      display: grid;
      grid-template-columns: minmax(0, 1.05fr) minmax(420px, .95fr);
      gap: 18px;
      margin-bottom: 18px;
    }

    .panel {
      min-width: 0;
      border-radius: 16px;
      overflow: hidden;
    }

    .panel-head {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 16px;
      border-bottom: 1px solid var(--line);
      padding: 15px 18px;
    }
    .panel-head h2 {
      margin: 0;
      font-size: 15px;
      letter-spacing: -.01em;
    }
    .panel-head p {
      margin: 3px 0 0;
      color: var(--muted);
      font-size: 12px;
    }

    .legend {
      display: flex;
      flex-wrap: wrap;
      gap: 10px;
      color: var(--muted);
      font-size: 11px;
    }
    .legend i {
      display: inline-block;
      width: 8px;
      height: 8px;
      margin-right: 5px;
      border-radius: 50%;
    }

    .plan-stage {
      position: relative;
      min-height: 410px;
      overflow: auto;
      background-color: #09172a;
      background-image:
        linear-gradient(rgba(103, 169, 255, .055) 1px, transparent 1px),
        linear-gradient(90deg, rgba(103, 169, 255, .055) 1px,
                        transparent 1px);
      background-size: 28px 28px;
    }
    .plan-stage svg {
      position: absolute;
      inset: 0;
      width: 100%;
      height: 100%;
      pointer-events: none;
    }
    .plan-edge {
      stroke: #3b5878;
      stroke-width: 2;
      fill: none;
    }
    .plan-node {
      position: absolute;
      width: 220px;
      min-height: 78px;
      transform: translateX(-50%);
      border: 1px solid var(--line);
      border-radius: 12px;
      background: var(--panel-2);
      padding: 12px 14px;
      text-align: left;
      cursor: pointer;
      box-shadow: 0 10px 28px rgba(0, 0, 0, .24);
    }
    .plan-node:hover, .plan-node.selected {
      border-color: var(--cyan);
      outline: 2px solid rgba(69, 214, 196, .12);
    }
    .plan-node-name {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 8px;
      font-weight: 850;
    }
    .plan-node-name span:last-child {
      color: var(--cyan);
      font: 700 11px ui-monospace, monospace;
    }
    .plan-node-detail {
      margin-top: 7px;
      overflow: hidden;
      color: var(--muted);
      font-size: 11px;
      text-overflow: ellipsis;
      white-space: nowrap;
    }

    .operator-body { padding: 10px 18px 18px; }
    .operator-row {
      width: 100%;
      border: 0;
      border-bottom: 1px solid rgba(39, 64, 95, .7);
      background: transparent;
      padding: 13px 0;
      text-align: left;
      cursor: pointer;
    }
    .operator-row:last-child { border-bottom: 0; }
    .operator-row.selected .op-name { color: var(--cyan); }
    .op-line {
      display: grid;
      grid-template-columns: minmax(120px, 1fr) 80px 70px;
      gap: 12px;
      align-items: center;
    }
    .op-name { font-weight: 800; }
    .op-time, .op-rows {
      color: var(--muted);
      font: 700 11px ui-monospace, monospace;
      text-align: right;
    }
    .bar {
      height: 6px;
      margin-top: 9px;
      overflow: hidden;
      border-radius: 99px;
      background: #07111f;
    }
    .bar > span {
      display: block;
      height: 100%;
      border-radius: inherit;
      background: var(--blue);
    }
    .operator-detail {
      border: 1px solid var(--line);
      border-radius: 12px;
      background: #09172a;
      padding: 14px;
      margin-top: 14px;
    }
    .operator-detail h3 { margin: 0 0 7px; font-size: 15px; }
    .operator-detail p { margin: 0; color: var(--muted); font-size: 12px; }
    .detail-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 8px;
      margin-top: 12px;
    }
    .detail-grid div {
      border: 1px solid rgba(39, 64, 95, .8);
      border-radius: 8px;
      padding: 9px;
    }
    .detail-grid small { display: block; color: var(--muted); font-size: 9px; }
    .detail-grid strong { font-size: 12px; }

    .timeline-controls {
      display: flex;
      flex-wrap: wrap;
      gap: 7px;
    }
    .filter-btn, .car-btn {
      border: 1px solid var(--line);
      border-radius: 8px;
      background: transparent;
      padding: 6px 9px;
      color: var(--muted);
      font-size: 11px;
      cursor: pointer;
    }
    .filter-btn.active, .filter-btn:hover, .car-btn:hover {
      border-color: var(--blue);
      color: var(--text);
    }
    .timeline-body {
      max-height: 460px;
      overflow: auto;
      padding: 10px 18px 18px;
    }
    .timeline-row {
      display: grid;
      grid-template-columns: 82px minmax(130px, 190px) minmax(180px, 1fr)
                           82px;
      gap: 10px;
      align-items: center;
      min-height: 34px;
      border-bottom: 1px solid rgba(39, 64, 95, .5);
      font-size: 11px;
    }
    .phase {
      display: inline-flex;
      width: max-content;
      border-radius: 999px;
      padding: 4px 7px;
      background: rgba(103, 169, 255, .12);
      color: var(--blue);
      font-weight: 800;
    }
    .phase.car { background: rgba(255, 188, 102, .12); color: var(--amber); }
    .event-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .track { position: relative; height: 7px; border-radius: 99px; background: #07111f; }
    .event-bar {
      position: absolute;
      top: 0;
      min-width: 4px;
      height: 100%;
      border-radius: 99px;
      background: var(--blue);
    }
    .event-bar.car { background: var(--amber); }
    .event-time { color: var(--muted); font-family: ui-monospace, monospace; text-align: right; }
    .empty { padding: 34px; color: var(--muted); text-align: center; }
    .notice { margin: 10px 18px 0; color: var(--amber); font-size: 11px; }

    .car-layout {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 230px;
      gap: 18px;
      padding: 18px;
    }
    .car-summary {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 10px;
      margin-bottom: 15px;
    }
    .car-stat {
      border: 1px solid var(--line);
      border-radius: 10px;
      background: #09172a;
      padding: 11px;
    }
    .car-stat small { display: block; color: var(--muted); font-size: 9px; text-transform: uppercase; }
    .car-stat strong { display: block; margin-top: 5px; font-size: 18px; }
    .car-list {
      display: grid;
      grid-template-columns: 62px 1fr;
      gap: 10px;
      align-items: start;
      min-height: 48px;
      border-top: 1px solid rgba(39, 64, 95, .65);
      padding: 11px 0;
    }
    .car-list strong { color: var(--muted); font-size: 12px; }
    .chips { display: flex; flex-wrap: wrap; gap: 6px; }
    .chip {
      min-width: 31px;
      border: 1px solid rgba(69, 214, 196, .3);
      border-radius: 7px;
      background: rgba(69, 214, 196, .08);
      padding: 5px 7px;
      color: #c8fff5;
      font: 700 11px ui-monospace, monospace;
      text-align: center;
    }
    .chip.ghost {
      border-color: rgba(183, 156, 255, .34);
      background: rgba(183, 156, 255, .08);
      color: #ded3ff;
    }
    .empty-chip { color: #58718f; font: 11px ui-monospace, monospace; }
    .p-track {
      height: 8px;
      overflow: hidden;
      border-radius: 99px;
      background: #07111f;
      margin-top: 8px;
    }
    .p-track span { display: block; height: 100%; background: var(--cyan); }
    .car-event-card {
      border: 1px solid var(--line);
      border-radius: 12px;
      background: #09172a;
      padding: 15px;
    }
    .car-event-card .event-type { color: var(--amber); font-weight: 900; }
    .car-event-card dl {
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 9px;
      margin: 16px 0;
      font-size: 11px;
    }
    .car-event-card dt { color: var(--muted); }
    .car-event-card dd { margin: 0; font-family: ui-monospace, monospace; }
    .car-nav { display: flex; gap: 7px; }
    .car-nav .car-btn { flex: 1; }
    .car-range { width: 100%; accent-color: var(--cyan); margin-top: 14px; }
    .footer {
      display: flex;
      justify-content: space-between;
      gap: 16px;
      margin-top: 18px;
      color: var(--muted);
      font-size: 11px;
    }

    @media (max-width: 1100px) {
      .metrics { grid-template-columns: repeat(3, 1fr); }
      .grid-two { grid-template-columns: 1fr; }
    }
    @media (max-width: 720px) {
      .shell { width: min(100% - 20px, 1500px); padding-top: 18px; }
      .topbar, .query-card { grid-template-columns: 1fr; display: grid; }
      .download { width: max-content; }
      .metrics { grid-template-columns: repeat(2, 1fr); }
      .car-layout { grid-template-columns: 1fr; }
      .car-summary { grid-template-columns: repeat(2, 1fr); }
      .timeline-row { grid-template-columns: 70px 1fr 72px; }
      .timeline-row .track { display: none; }
      .detail-grid { grid-template-columns: repeat(2, 1fr); }
      .footer { flex-direction: column; }
    }
  </style>
</head>
<body>
  <main class="shell">
    <header class="topbar">
      <div>
        <div class="eyebrow">Perfopticon-inspired analysis</div>
        <h1>Visual Query Profiler</h1>
        <p class="subtitle">Plan físico, tiempos por operador, traza Volcano y
          evolución del reemplazo adaptativo CAR en una sola vista coordinada.</p>
      </div>
      <button class="download" id="download-json" data-testid="download-json">
        Exportar perfil JSON
      </button>
    </header>

    <section class="query-card" aria-label="Consulta analizada">
      <div>
        <span class="query-label">Consulta SQL</span>
        <div class="query-text" id="query-text" data-testid="query-text"></div>
      </div>
      <div class="plan-pill" id="plan-pill" data-testid="plan-pill"></div>
    </section>

    <section class="metrics" id="metrics" data-testid="metrics"></section>

    <section class="grid-two">
      <article class="panel">
        <div class="panel-head">
          <div>
            <h2>Grafo del plan físico</h2>
            <p>Selecciona un nodo para coordinar las demás vistas.</p>
          </div>
          <div class="legend"><span><i style="background:var(--cyan)"></i>selección</span></div>
        </div>
        <div class="plan-stage" id="plan-stage" data-testid="plan-stage"></div>
      </article>

      <article class="panel">
        <div class="panel-head">
          <div>
            <h2>Costo por operador</h2>
            <p>Tiempo inclusivo y propio; las llamadas siguen Volcano.</p>
          </div>
        </div>
        <div class="operator-body">
          <div id="operator-list" data-testid="operator-list"></div>
          <div class="operator-detail" id="operator-detail"
               data-testid="operator-detail"></div>
        </div>
      </article>
    </section>

    <section class="panel" style="margin-bottom:18px">
      <div class="panel-head">
        <div>
          <h2>Línea temporal de ejecución</h2>
          <p>Eventos Open, Next y Close con eventos CAR en la misma escala.</p>
        </div>
        <div class="timeline-controls" id="timeline-controls">
          <button class="filter-btn active" data-filter="all">Todos</button>
          <button class="filter-btn" data-filter="Open">Open</button>
          <button class="filter-btn" data-filter="Next">Next</button>
          <button class="filter-btn" data-filter="Close">Close</button>
          <button class="filter-btn" data-filter="CAR">CAR</button>
        </div>
      </div>
      <div id="timeline-notice"></div>
      <div class="timeline-body" id="timeline" data-testid="timeline"></div>
    </section>

    <section class="panel">
      <div class="panel-head">
        <div>
          <h2>Estado adaptativo CAR</h2>
          <p>Recorre cada hit, miss, promoción y expulsión.</p>
        </div>
        <div class="legend">
          <span><i style="background:var(--cyan)"></i>residente</span>
          <span><i style="background:var(--violet)"></i>fantasma</span>
        </div>
      </div>
      <div class="car-layout" id="car-layout" data-testid="car-layout">
        <div>
          <div class="car-summary" id="car-summary"></div>
          <div id="car-lists"></div>
        </div>
        <aside class="car-event-card" id="car-event-card"></aside>
      </div>
    </section>

    <footer class="footer">
      <span>Mini-SGBD CAR · visualización local autocontenida</span>
      <span>Inspirada en Perfopticon; adaptada a un motor de un solo nodo.</span>
    </footer>
  </main>

  <script>
    const profile = )HTML";
}

const char *HtmlAfterData() {
  return R"HTML(;
    const $ = (id) => document.getElementById(id);
    const fmt = (value, digits = 3) =>
      Number(value || 0).toLocaleString("es-PE", {
        minimumFractionDigits: digits,
        maximumFractionDigits: digits
      });

    const operatorById = new Map(profile.operators.map(op => [op.id, op]));
    let selectedOperatorId = profile.operators.length ? profile.operators[0].id : -1;
    let timelineFilter = "all";
    let carIndex = Math.max(0, profile.carEvents.length - 1);

    $("query-text").textContent = profile.sql || "Consulta no disponible";
    $("plan-pill").textContent = profile.planName;

    function renderMetrics() {
      const accesses = profile.metrics.bufferHits + profile.metrics.bufferMisses;
      const cards = [
        ["Tiempo total", `${fmt(profile.metrics.elapsedMs)} ms`, "consulta completa"],
        ["Filas", profile.rowCount.toLocaleString("es-PE"), "resultado producido"],
        ["Hit ratio", `${fmt(profile.metrics.hitRatio * 100, 2)} %`,
         `${accesses} accesos al buffer`],
        ["Buffer hits", profile.metrics.bufferHits.toLocaleString("es-PE"), "página residente"],
        ["Buffer misses", profile.metrics.bufferMisses.toLocaleString("es-PE"), "acceso no residente"],
        ["Costo I/O", profile.metrics.ioOperations.toLocaleString("es-PE"),
         `${profile.metrics.diskReads} lecturas · ${profile.metrics.diskWrites} escrituras`]
      ];
      $("metrics").innerHTML = cards.map(card => `
        <article class="metric">
          <small>${card[0]}</small>
          <strong>${card[1]}</strong>
          <span>${card[2]}</span>
        </article>`).join("");
    }

    function nodeDepth(node) {
      let depth = 0;
      let current = node;
      const visited = new Set();
      while (current && current.parentId >= 0 && !visited.has(current.id)) {
        visited.add(current.id);
        current = operatorById.get(current.parentId);
        depth += 1;
      }
      return depth;
    }

    function selectOperator(id) {
      selectedOperatorId = id;
      renderPlan();
      renderOperators();
      renderTimeline();
    }

    function renderPlan() {
      const stage = $("plan-stage");
      stage.innerHTML = "";
      if (!profile.operators.length) {
        stage.innerHTML = '<div class="empty">No hay plan físico registrado.</div>';
        return;
      }

      const levels = new Map();
      profile.operators.forEach(op => {
        const depth = nodeDepth(op);
        if (!levels.has(depth)) levels.set(depth, []);
        levels.get(depth).push(op);
      });

      const maxDepth = Math.max(...levels.keys());
      stage.style.minHeight = `${Math.max(410, 80 + (maxDepth + 1) * 116)}px`;
      const width = Math.max(stage.clientWidth, 520);
      const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
      svg.setAttribute("aria-hidden", "true");
      stage.appendChild(svg);

      const positions = new Map();
      [...levels.entries()].forEach(([depth, nodes]) => {
        nodes.forEach((node, index) => {
          const x = width * (index + 1) / (nodes.length + 1);
          const y = 34 + depth * 116;
          positions.set(node.id, {x, y});
        });
      });

      profile.operators.forEach(op => {
        if (op.parentId < 0 || !positions.has(op.parentId)) return;
        const parent = positions.get(op.parentId);
        const child = positions.get(op.id);
        const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
        const startY = parent.y + 78;
        const endY = child.y;
        const middleY = (startY + endY) / 2;
        path.setAttribute("d", `M ${parent.x} ${startY} C ${parent.x} ${middleY}, ${child.x} ${middleY}, ${child.x} ${endY}`);
        path.setAttribute("class", "plan-edge");
        svg.appendChild(path);
      });

      profile.operators.forEach(op => {
        const position = positions.get(op.id);
        const button = document.createElement("button");
        button.className = `plan-node${op.id === selectedOperatorId ? " selected" : ""}`;
        button.style.left = `${position.x}px`;
        button.style.top = `${position.y}px`;
        button.dataset.testid = `plan-node-${op.id}`;
        button.setAttribute("aria-label", `Operador ${op.name}`);
        button.innerHTML = `
          <div class="plan-node-name">
            <span>${op.name}</span><span>${fmt(op.inclusiveMs)} ms</span>
          </div>
          <div class="plan-node-detail">${op.detail || "Sin parámetros"}</div>`;
        button.addEventListener("click", () => selectOperator(op.id));
        stage.appendChild(button);
      });
    }

    function renderOperators() {
      const maxSelf = Math.max(...profile.operators.map(op => op.selfMs), .000001);
      $("operator-list").innerHTML = profile.operators.map(op => `
        <button class="operator-row${op.id === selectedOperatorId ? " selected" : ""}"
                data-operator-id="${op.id}" data-testid="operator-row-${op.id}">
          <div class="op-line">
            <span class="op-name">${op.name}</span>
            <span class="op-time">${fmt(op.selfMs)} ms</span>
            <span class="op-rows">${op.rowsOut} filas</span>
          </div>
          <div class="bar"><span style="width:${Math.max(2, op.selfMs / maxSelf * 100)}%"></span></div>
        </button>`).join("");

      document.querySelectorAll("[data-operator-id]").forEach(button => {
        button.addEventListener("click", () => selectOperator(Number(button.dataset.operatorId)));
      });

      const op = operatorById.get(selectedOperatorId);
      if (!op) {
        $("operator-detail").innerHTML = "Selecciona un operador.";
        return;
      }
      $("operator-detail").innerHTML = `
        <h3>${op.name}</h3>
        <p>${op.detail || "Sin parámetros adicionales."}</p>
        <div class="detail-grid">
          <div><small>Inclusivo</small><strong>${fmt(op.inclusiveMs)} ms</strong></div>
          <div><small>Tiempo propio</small><strong>${fmt(op.selfMs)} ms</strong></div>
          <div><small>Next()</small><strong>${op.nextCalls}</strong></div>
          <div><small>Filas</small><strong>${op.rowsOut}</strong></div>
          <div><small>Open()</small><strong>${fmt(op.openMs)} ms</strong></div>
          <div><small>Next()</small><strong>${fmt(op.nextMs)} ms</strong></div>
          <div><small>Close()</small><strong>${fmt(op.closeMs)} ms</strong></div>
          <div><small>Execute()</small><strong>${fmt(op.executeMs)} ms</strong></div>
        </div>`;
    }

    function combinedEvents() {
      const operatorEvents = profile.timeline.map(event => ({
        ...event,
        label: operatorById.get(event.operatorId)?.name || "Operador",
        eventCategory: "operator"
      }));
      const carEvents = profile.carEvents.map(event => ({
        sequence: event.sequence,
        operatorId: -1,
        phase: "CAR",
        startMs: event.timestampMs,
        durationMs: 0,
        rowsProduced: 0,
        detail: event.type,
        label: event.type,
        eventCategory: "car"
      }));
      return [...operatorEvents, ...carEvents].sort((a, b) =>
        a.startMs === b.startMs ? a.sequence - b.sequence : a.startMs - b.startMs);
    }

    function renderTimeline() {
      const all = combinedEvents();
      const filtered = all.filter(event => {
        if (timelineFilter === "CAR") return event.eventCategory === "car";
        if (timelineFilter !== "all" && event.phase !== timelineFilter) return false;
        if (selectedOperatorId >= 0 && timelineFilter !== "CAR" &&
            event.eventCategory === "operator") {
          return event.operatorId === selectedOperatorId;
        }
        return true;
      });
      const visible = filtered.slice(0, 300);
      const total = Math.max(profile.metrics.elapsedMs,
                             ...all.map(event => event.startMs + event.durationMs), .001);

      $("timeline").innerHTML = visible.length ? visible.map(event => {
        const left = Math.min(99, Math.max(0, event.startMs / total * 100));
        const width = Math.max(.5, event.durationMs / total * 100);
        const isCar = event.eventCategory === "car";
        return `
          <div class="timeline-row">
            <span class="phase${isCar ? " car" : ""}">${event.phase}</span>
            <span class="event-name" title="${event.detail || event.label}">${event.label}</span>
            <span class="track">
              <i class="event-bar${isCar ? " car" : ""}"
                 style="left:${left}%;width:${width}%"></i>
            </span>
            <span class="event-time">${fmt(event.startMs)} ms</span>
          </div>`;
      }).join("") : '<div class="empty">No hay eventos para este filtro.</div>';

      const notices = [];
      if (filtered.length > visible.length) notices.push(`Se muestran 300 de ${filtered.length} eventos.`);
      if (profile.timelineTruncated) notices.push("La captura de eventos de operador alcanzó su límite.");
      if (profile.carEventsTruncated) notices.push("La captura de eventos CAR alcanzó su límite.");
      $("timeline-notice").innerHTML =
        notices.length ? `<div class="notice">${notices.join(" ")}</div>` : "";
    }

    function chips(values, ghost = false) {
      if (!values.length) return '<span class="empty-chip">vacía</span>';
      return values.map(value =>
        `<span class="chip${ghost ? " ghost" : ""}">${value}</span>`).join("");
    }

    function renderCAR() {
      if (!profile.carEvents.length) {
        $("car-layout").innerHTML =
          '<div class="empty">Esta ejecución no estuvo conectada a un Buffer Pool CAR.</div>';
        return;
      }
      carIndex = Math.min(Math.max(carIndex, 0), profile.carEvents.length - 1);
      const event = profile.carEvents[carIndex];
      const state = event.state;
      const accesses = state.hits + state.misses;
      const hitRatio = accesses ? state.hits / accesses * 100 : 0;

      $("car-summary").innerHTML = `
        <div class="car-stat"><small>Objetivo p</small><strong>${fmt(state.p, 2)}</strong>
          <div class="p-track"><span style="width:${state.capacity ? state.p / state.capacity * 100 : 0}%"></span></div>
        </div>
        <div class="car-stat"><small>Capacidad</small><strong>${state.capacity}</strong></div>
        <div class="car-stat"><small>Hits / misses</small><strong>${state.hits} / ${state.misses}</strong></div>
        <div class="car-stat"><small>Hit ratio</small><strong>${fmt(hitRatio, 1)} %</strong></div>`;

      $("car-lists").innerHTML = `
        <div class="car-list"><strong>T1</strong><div class="chips">${chips(state.t1)}</div></div>
        <div class="car-list"><strong>T2</strong><div class="chips">${chips(state.t2)}</div></div>
        <div class="car-list"><strong>B1</strong><div class="chips">${chips(state.b1, true)}</div></div>
        <div class="car-list"><strong>B2</strong><div class="chips">${chips(state.b2, true)}</div></div>`;

      $("car-event-card").innerHTML = `
        <div class="event-type">${event.type}</div>
        <dl>
          <dt>Evento</dt><dd>${carIndex + 1} / ${profile.carEvents.length}</dd>
          <dt>Tiempo</dt><dd>${fmt(event.timestampMs)} ms</dd>
          <dt>Página</dt><dd>${event.pageId >= 0 ? event.pageId : "—"}</dd>
          <dt>Frame</dt><dd>${event.frameId >= 0 ? event.frameId : "—"}</dd>
          <dt>p anterior</dt><dd>${fmt(event.previousP, 2)}</dd>
          <dt>Expulsables</dt><dd>${state.evictableCount}</dd>
        </dl>
        <div class="car-nav">
          <button class="car-btn" id="car-prev" data-testid="car-prev">Anterior</button>
          <button class="car-btn" id="car-next" data-testid="car-next">Siguiente</button>
        </div>
        <input class="car-range" id="car-range" data-testid="car-range"
               aria-label="Evento CAR" type="range" min="0"
               max="${profile.carEvents.length - 1}" value="${carIndex}">`;

      $("car-prev").disabled = carIndex === 0;
      $("car-next").disabled = carIndex === profile.carEvents.length - 1;
      $("car-prev").addEventListener("click", () => { carIndex -= 1; renderCAR(); });
      $("car-next").addEventListener("click", () => { carIndex += 1; renderCAR(); });
      $("car-range").addEventListener("input", eventInput => {
        carIndex = Number(eventInput.target.value);
        renderCAR();
      });
    }

    document.querySelectorAll("[data-filter]").forEach(button => {
      button.addEventListener("click", () => {
        timelineFilter = button.dataset.filter;
        document.querySelectorAll("[data-filter]").forEach(candidate =>
          candidate.classList.toggle("active", candidate === button));
        renderTimeline();
      });
    });

    $("download-json").addEventListener("click", () => {
      const blob = new Blob([JSON.stringify(profile, null, 2)],
                            {type: "application/json"});
      const url = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url;
      link.download = "query_profile.json";
      link.click();
      URL.revokeObjectURL(url);
    });

    renderMetrics();
    renderPlan();
    renderOperators();
    renderTimeline();
    renderCAR();
    window.addEventListener("resize", renderPlan);
  </script>
</body>
</html>
)HTML";
}

}  // namespace

void QueryVisualizer::WriteHtml(const ProfiledQueryResult &result,
                                const std::string &path) {
  if (path.empty()) {
    throw std::invalid_argument(
        "La ruta del visualizador no puede estar vacia.");
  }

  const std::filesystem::path output_path(path);
  if (output_path.has_parent_path()) {
    std::filesystem::create_directories(output_path.parent_path());
  }

  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw std::runtime_error(
        "No se pudo crear el reporte visual: " + path);
  }

  output << HtmlBeforeData() << BuildProfileJson(result) << HtmlAfterData();
  if (!output.good()) {
    throw std::runtime_error(
        "No se pudo completar el reporte visual: " + path);
  }
}

}  // namespace minisgbd
