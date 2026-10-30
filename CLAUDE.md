# Notes for AI coding assistants

- Read docs/ARCHITECTURE.md before changing anything under libsim/. The
  determinism rules there are hard requirements: no libm transcendental
  functions, `<random>` distributions or unordered-container iteration in code
  that affects simulation state.
- Build and test: `cmake -S . -B build -G Ninja -DHISTSIM_WARNINGS_AS_ERRORS=ON && cmake --build build && ctest --test-dir build --output-on-failure`.
- Check determinism across compilers locally by building with both GCC and
  Clang and comparing `histsim run --seed 1 --level 6 --years 500 --hash-only`.
- If simulation output changes on purpose, update the golden hash in
  tests/test_simulation.cpp and mention it in the commit message.
- Systems are stateless; all changing data goes in SimState and must be saved
  in snapshots (bump kSnapshotVersion when the layout changes) and hashed.
- Content lives in data/*.json and is compiled in; it is part of the world
  hash, so changing it changes the golden hash too. Number values must be
  exactly readable (≤15 significant digits).
