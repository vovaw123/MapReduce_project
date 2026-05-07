# Basic MapReduce (C++)

## Quick start (short)

1. Build without MPI + run demo test:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

2. Run local demo (serial/threaded):

```powershell
python scripts/generate_input.py --output data/input.txt --count 200000
build\mapreduce_demo.exe --input data/input.txt --demo wordcount --mode serial
build\mapreduce_demo.exe --input data/input.txt --demo wordcount --mode threads --threads 4
```

3. Build with MPI + run MPI demo:

```powershell
cmake -S . -B build_mpi -DMAPREDUCE_USE_MPI=ON
cmake --build build_mpi
mpiexec -n 4 build_mpi\mapreduce_mpi_demo.exe --input data/input.txt
```

4. Build two benchmark charts (no-MPI vs MPI):

```powershell
python scripts/benchmark_plot.py --source-root . --build-nompi build_nompi --build-mpi build_mpi --sizes 50000,100000,200000,400000 --threads 4 --mpi-ranks 4 --runs 3 --warmup 1 --out-dir data/plots
```

Charts are saved to:

- `data/plots/benchmark_no_mpi.png`
- `data/plots/benchmark_with_mpi.png`

## What this project covers

- C++ MapReduce core (header-only)
- Custom Map and Reduce functions
- Two local backends: serial and threaded
- Distributed backend: MPI
- One compact demo-style test
- Demo executables and Python benchmarking script with two charts

## What is implemented

- Generic MapReduce API in `include/mapreduce.hpp`
  - `run_serial(vector, map_fn, reduce_fn)`
  - `run_serial(first, last, map_fn, reduce_fn)` ← iterator overload
  - `run_threaded(vector, thread_count, map_fn, reduce_fn)`
  - `run_threaded(first, last, thread_count, map_fn, reduce_fn)` ← iterator overload
  - `run_mpi(...)` (scatter -> local map -> alltoallv shuffle -> local reduce -> gather)
- Demo executable `mapreduce_demo` in `examples/demo_main.cpp`
  - `classify_lines` and `word_count`
- MPI demo executable `mapreduce_mpi_demo` in `examples/word_count_mpi.cpp`
  - prints `verify=MATCH` or `verify=MISMATCH` comparing serial vs MPI result
- Expanded test suite in `tests/test_mapreduce.cpp`
  - basic classify + wordcount with exact asserts
  - empty input → empty result
  - map returning 0 pairs (filter semantics)
  - `thread_count > input.size()` safety
  - single-item input
  - punctuation-only line → wordcount emits 0 pairs
  - iterator API: `std::list`, partial range
  - file-based: `data/fish.txt` and `data/combine.txt` (serial == threaded, non-empty)
- Python benchmark script `scripts/benchmark_plot.py`
  - warmup runs before measurement (discarded, `--warmup N`, default 1)
  - **median** instead of mean across runs
  - MPI runs check `verify=` output and raise on MISMATCH
  - auto-builds no-MPI and MPI variants
  - saves two charts

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

With MPI demo:

```powershell
cmake -S . -B build_mpi -DMAPREDUCE_USE_MPI=ON
cmake --build build_mpi
```

Library is header-only (`mapreduce_lib` as INTERFACE target), so the core is in `include/`.
Demo (`examples/`) and tests (`tests/`) are separate targets.

## Demo workflow

1. Generate input:

```powershell
python scripts/generate_input.py --output data/input.txt --count 200000
```

By default, each run generates different data (no fixed seed). Use `--seed <number>` only when you need reproducible data.

2. Run C++ demo directly:

```powershell
build\mapreduce_demo.exe --input data/input.txt --mode serial
build\mapreduce_demo.exe --input data/input.txt --mode threads --threads 4
```

3. Run MPI demo directly:

```powershell
mpiexec -n 4 build_mpi\mapreduce_mpi_demo.exe --input data/input.txt
```

4. Build and run benchmark script (2 charts):

```powershell
python scripts/benchmark_plot.py --source-root . --build-nompi build_nompi --build-mpi build_mpi --sizes 50000,100000,200000,400000 --threads 4 --mpi-ranks 4 --runs 3 --warmup 1 --out-dir data/plots
```

Outputs:

- `data/plots/benchmark_no_mpi.png`
- `data/plots/benchmark_with_mpi.png`
