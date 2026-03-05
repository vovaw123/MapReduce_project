# Basic MapReduce (C++)

## Assignment analysis

From `build/requirements.txt`, the required items are:
- C++ language
- Custom Map and Reduce functions
- Parallelization (at least one method for now)
- Tests, demo program, documentation
- Python wrapper for running C++ and plotting serial vs threaded comparison

This repository now contains a very basic implementation that satisfies this scope.

## What is implemented

- Generic MapReduce API in `include/mapreduce.hpp`
  - `run_serial(...)`
  - `run_threaded(..., thread_count, ...)` using `std::thread` + `std::mutex`
- Demo executable `mapreduce_demo` in `examples/demo_main.cpp`
  - Map: classify each string as `letters_only` or `has_digits`
  - Reduce: sum counts per key
- Test executable in `tests/test_mapreduce.cpp`
- Python scripts:
  - `scripts/generate_input.py` for random input generation
  - `scripts/benchmark_plot.py` for serial vs threaded performance chart

## Build

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build
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

3. Plot benchmark:

```powershell
python scripts/benchmark_plot.py --exe build/mapreduce_demo.exe --input data/input.txt --count 200000 --runs 5 --max-threads 4 --output data/benchmark.png
```

`benchmark_plot.py` regenerates fresh input on every run before benchmarking.
