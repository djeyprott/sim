# Contributing

Thanks for helping build histsim.

## Before you start

- Read [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), especially the
  determinism rules. A change that makes runs differ between platforms will
  fail CI.
- For anything larger than a bug fix, open an issue first so the approach can
  be agreed on.

## Workflow

1. Branch from `main`.
2. Build and run the tests locally:
   ```sh
   cmake -S . -B build -DHISTSIM_WARNINGS_AS_ERRORS=ON
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```
3. Add tests for new behavior. New systems need at least a conservation test
   and a save/resume test.
4. If you change simulation behavior on purpose, update the golden hash
   (see "When behavior changes on purpose" in the architecture doc) and say so
   in the commit message.
5. Open a pull request. CI builds on Linux, macOS and Windows and checks that
   all platforms produce the same results.

## Style

- C++20, formatted with the repository's `.clang-format`.
- Keep systems stateless; put changing data in `SimState`.
- Prefer plain data and free functions over class hierarchies.
- Comments explain why, not what.

## Content and sensitive history

The simulator models conquest, slavery and forced labor, genocide and religious
conflict because they shaped history. Model them as mechanisms, label them
plainly and accurately, and avoid glorifying or trivializing them.

## License

By contributing you agree that your contributions are licensed under the
Apache License 2.0.
