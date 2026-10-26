# tools

Offline tools that produce world files for the simulation core. Planned for M4:

- **Earth importer:** resamples real elevation, rivers, soils and paleoclimate
  onto the grid. Ships as scripts that download source data, so datasets with
  redistribution limits are never bundled.
- **Procedural generator:** plate tectonics, erosion, hydrology and climate
  spin-up, writing the same format as the Earth importer.

Until then, `generate_test_world()` in `libsim` provides a simple generated world.
