# histsim

An open-source human history simulator. A to-scale planet, populations with
demographics, languages and cultures, goods and trade, technology, polities and
climate, all interacting so that history emerges rather than being scripted.

The project is at **milestone M0 (foundations)**: a deterministic simulation
core, a geodesic world grid, a generated test world and a headless runner. The
forager systems in M0 are placeholders that exercise the machinery; real
demography, culture and economy come in later milestones.

## Download

Prebuilt executables for Windows, macOS and Linux are attached to each
release on the [Releases page](https://github.com/djeyprott/sim/releases).
Every push to `main` also produces tested builds: open
[Actions > CI](https://github.com/djeyprott/sim/actions/workflows/ci.yml), pick
the latest run, and download `histsim-windows-msvc` (or the macOS/Linux one)
under **Artifacts**.

Unzip and double-click `histsim` to run a 1,000-year demo. Windows may show a
SmartScreen warning because the executable is not code-signed; choose
**More info**, then **Run anyway**.

To publish a release, use **Releases > Draft a new release** on GitHub, create
a tag such as `v0.1.0`, and publish. The Release workflow then builds, tests
and attaches executables for all three platforms.

## Building

Requires CMake 3.21+ and a C++20 compiler (GCC 11+, Clang 14+, MSVC 2022 or
AppleClang 15+). There are no third-party dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Running

```sh
# Describe the generated test world
./build/cli/histsim world --seed 1 --level 5

# Simulate 1,000 years, reporting every 100
./build/cli/histsim run --seed 1 --level 5 --years 1000 --report-every 100

# Save after 400 years, then resume for 600 more (same result as one run)
./build/cli/histsim run --seed 1 --years 400 --save run.hsim
./build/cli/histsim resume run.hsim --years 600
```

`--level` sets grid resolution: level 5 is 10,242 cells (~50,000 km² each),
level 7 is 163,842 cells (~3,100 km²). Time is shown as years since the run
began; the sim has no calendar years.

## Layout

| Path | Contents |
| --- | --- |
| `libsim/` | The simulation core: grid, world, RNG, scheduler, state, systems, snapshots. No engine or UI code. |
| `cli/` | `histsim`, the headless runner for batch runs and CI. |
| `tests/` | Unit, determinism and replay tests. |
| `tools/` | Offline tools (Earth importer, procedural generator) — from M4. |
| `data/` | Content packs (goods, techs, diseases) — from M2. |
| `docs/` | [Architecture and determinism rules](docs/ARCHITECTURE.md). |

The Godot 4 front end (map modes, inspectors, timeline) will live in `godot/`
and talk to the core only through the Sim API in
[`libsim/include/histsim/simulation.hpp`](libsim/include/histsim/simulation.hpp).

## Determinism

Same seed and inputs give a bit-identical run on every supported platform. CI
checks this on Linux (GCC, Clang), macOS (arm64) and Windows (MSVC) by
comparing state hashes. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#determinism-rules)
before changing simulation code.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).
