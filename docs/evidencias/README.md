# Evidencias reproducibles

- `cli_sesion_operaciones.txt`: inserción, `IndexScan`,
  `Filter + SeqScan`, proyección, `UPDATE` y `DELETE`.
- `cli_reinicio_persistencia.txt`: segunda ejecución sobre el mismo archivo.
- `cli_operaciones.png`: captura visual de la primera sesión.
- `cli_persistencia.png`: captura visual del reinicio y el tamaño físico.
- `benchmark.png`: tabla y gráfica del benchmark estadístico.
- `visual_query_profiler.png`: grafo físico, costo por operador, línea
  temporal y estado CAR para una consulta real.
- `car_demo.png`: demostración reproducible con B1/B2 y adaptación de `p`.
- `../experimentos.md`: protocolo, resultados e interpretación.
- `../visualizacion.md`: arquitectura y uso del perfil visual.

La base usada para las capturas contiene el esquema
`personas(id, nombre, ciudad, profesion)` y se creó en un directorio
aislado de `build/`. Su tamaño final fue 16,384 bytes, exactamente cuatro
páginas de 4 KB.
