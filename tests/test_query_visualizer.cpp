#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "query/car_visualizer_demo.h"
#include "query/query_executor.h"
#include "query/query_profiler.h"
#include "query/tuple.h"

using namespace minisgbd;

namespace {

int failures = 0;

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FALLO] " << message << '\n';
    ++failures;
  }
}

void ExpectContains(const std::string &text, const std::string &expected,
                    const std::string &message) {
  Expect(text.find(expected) != std::string::npos, message);
}

void TestInteractiveHtmlReport() {
  const std::string report_file = "test_query_visualizer.html";
  std::remove(report_file.c_str());

  const std::vector<Tuple> tuples = {
      Tuple{1, "Ana", "Lima", "Ingeniera"},
      Tuple{2, "Bruno", "Arequipa", "Medico"},
      Tuple{3, "Carla", "Arequipa", "Arquitecta"},
  };
  QueryExecutor executor("personas", tuples);
  QueryProfiler profiler(&executor);
  profiler.SetVisualizationPath(report_file);

  const ProfiledQueryResult result =
      profiler.Execute(
          "SELECT nombre, profesion FROM personas "
          "WHERE ciudad = 'Arequipa';");

  Expect(result.visualization_path == report_file,
         "El resultado debe informar la ruta visual.");

  std::ifstream input(report_file, std::ios::binary);
  Expect(input.is_open(), "El profiler debe crear el reporte HTML.");
  const std::string html((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());

  ExpectContains(html, "Visual Query Profiler",
                 "El reporte debe incluir el titulo principal.");
  ExpectContains(html, "data-testid=\"plan-stage\"",
                 "El reporte debe incluir el grafo del plan.");
  ExpectContains(html, "data-testid=\"timeline\"",
                 "El reporte debe incluir la linea temporal.");
  ExpectContains(html, "data-testid=\"car-layout\"",
                 "El reporte debe incluir el panel CAR.");
  ExpectContains(html, "\"name\":\"Projection\"",
                 "El JSON debe incluir Projection.");
  ExpectContains(html, "\"name\":\"Filter\"",
                 "El JSON debe incluir Filter.");
  ExpectContains(html, "\"name\":\"SeqScan\"",
                 "El JSON debe incluir SeqScan.");
  ExpectContains(html, "columnas=nombre, profesion",
                 "El plan debe describir la proyeccion del nuevo esquema.");
  ExpectContains(html, "ciudad = 'Arequipa'",
                 "El plan debe mostrar el filtro textual.");
  ExpectContains(html, "\"phase\":\"Open\"",
                 "La traza debe contener Open.");
  ExpectContains(html, "\"phase\":\"Next\"",
                 "La traza debe contener Next.");
  ExpectContains(html, "\"phase\":\"Close\"",
                 "La traza debe contener Close.");

  input.close();
  std::remove(report_file.c_str());
}

void TestCARDemoReport() {
  const std::string report_file = "test_car_demo.html";
  std::remove(report_file.c_str());

  GenerateCARDemoReport(report_file);
  std::ifstream input(report_file, std::ios::binary);
  Expect(input.is_open(), "El demo CAR debe generar su reporte.");
  const std::string html((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());

  ExpectContains(html, "\"planName\":\"CAR Demo\"",
                 "El reporte debe identificarse como demo CAR.");
  ExpectContains(html, "\"type\":\"MISS_B1\"",
                 "La traza debe demostrar un hit fantasma en B1.");
  ExpectContains(html, "\"type\":\"MISS_B2\"",
                 "La traza debe demostrar un hit fantasma en B2.");
  ExpectContains(html, "\"type\":\"EVICT_T2_B2\"",
                 "La traza debe demostrar una expulsion hacia B2.");
  ExpectContains(html, "\"p\":3.000000",
                 "La traza debe mostrar el crecimiento adaptativo de p.");

  input.close();
  std::remove(report_file.c_str());
}

}  // namespace

int main() {
  std::cout << "=== PRUEBAS DEL VISUAL QUERY PROFILER ===\n";

  TestInteractiveHtmlReport();
  TestCARDemoReport();

  if (failures != 0) {
    std::cerr << failures << " prueba(s) fallaron.\n";
    return 1;
  }

  std::cout << "Todas las pruebas del visualizador pasaron correctamente.\n";
  return 0;
}
