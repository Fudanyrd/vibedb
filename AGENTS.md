# AGENTS.md

BusTub — the CMU 15-445/645 educational relational DBMS. C++17, CMake, Google style. Not production code.

## Academic integrity (important)
This is course work. NEVER make solution code public, push it to a public fork, or paste project source into issues/PRs. Work only in a private repo. See `README.md` and `WALL-OF-SHAME.md`.

## Build
- Always build out-of-source from the existing `build/` dir. Running `cmake` in the repo root is a fatal error by design (root `CMakeLists.txt:31`).
  ```console
  cmake -B build -DCMAKE_BUILD_TYPE=Debug      # config (Debug is also the default)
  cmake --build build -j"$(nproc)"             # or: make -C build -j"$(nproc)"
  ```
- Debug implies `-O0 -ggdb` plus ASAN+LSAN. Override with `-DBUSTUB_SANITIZER=thread` or disable with `-DBUSTUB_SANITIZER=`. Grading runs in Release: `cmake -B build_rel -DCMAKE_BUILD_TYPE=Release`.
- `build/compile_commands.json` is exported automatically; `opencode.json` points clangd at it (with a hardcoded clangd path). Reconfigure if you add files so LSP/tidy see them.

## Layout
- Headers are NOT colocated with sources: `src/include/<module>/foo.h` vs `src/<module>/foo.cpp`. `src/include` is a public include dir, so includes are `"module/foo.h"`.
- Tests live at `test/<module>/*test.cpp`, globbed by `test/CMakeLists.txt`; each file becomes an executable named after the file (no `.cpp`) in `build/test/`.
- Per-project file lists for clang-tidy and `submit-pN` are hardcoded in the root `CMakeLists.txt` (`P0_FILES`, `P1_FILES`, …). Adding a new implementation file to a project does not add it to tidy or the submission zip unless you also list it there.
- Project writeups are in `docs/` (`proj0.md`, `p1-design.md`).

## Tests
- One test target (do not guess a ctest name):
  ```console
  make -C build -j"$(nproc)" count_min_sketch_test
  ./build/test/count_min_sketch_test --gtest_filter=CountMinSketchTest.BasicTest1
  ```
- All GTest: `make -C build build-tests && make -C build check-tests` (both build every `*test.cpp` first). `make check-public-ci-tests` is the CI subset, excluding `SQLLogicTest|Trie|ORSet|SkipList|CountMinSketch|RobinHood`.
- SQLLogic tests: `make -C build test-p3` runs all `SQLLogicTest.*`; a single file is `make -C build p3.01-seqscan_test`.
- Some upstream tests are `DISABLED_`-prefixed and will not run until the prefix is removed.

## Style / required checks
BusTub uses `clang-format-15`, `clang-tidy-15`, and `cpplint`; a project scores zero if these fail.
```console
make -C build format              # clang-format, auto-fixes in place
make -C build check-format        # verify only
make -C build check-lint          # cpplint
make -C build check-clang-tidy    # whole repo (slow); use check-clang-tidy-p0 etc.
```
`.clang-format` = Google, but with `ColumnLimit: 120` and `PointerAlignment: Right`.

## Conventions / gotchas
- `BUSTUB_ASSERT` compiles out in Release; use `BUSTUB_ENSURE` for checks that must always run.
- Ubuntu 24.04 is the supported/grading OS; macOS is dev-only; WSL is unsupported.
