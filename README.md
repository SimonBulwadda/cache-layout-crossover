# The Object Size Crossover: packed storage vs pointer indirection in C++

Code, raw data and figures for the paper "The Object Size Crossover: When Packed Storage Loses Its Cache Advantage in C++ Container Layouts" (Bulwadda, Kibirige and Tyagi).

The benchmark compares three container layouts (packed `std::vector`, pointer-indirected with ordinary allocation, pointer-indirected with scrambled allocation order) across 12 object sizes (32 to 65536 bytes), sequential and random access, and cache-resident and memory-resident working sets.

## Files

- `main.cpp`: the benchmark (Google Benchmark)
- `results.csv`: raw benchmark output (5 repetitions per configuration)
- `derived_metrics.csv`, `prefetch_contribution.csv`: metrics computed from the raw results
- `make_figures.py`: regenerates the figures from the CSV data
- `fig1_crossover_gap_ratio.png`, `fig2_absolute_time_floor.png`, `fig3_prefetch_contribution.png`: paper figures
- `perf_probe.cpp`: hardware counter probe used in the page fault check

## Requirements

- g++ with C++17 support
- Google Benchmark (`libbenchmark`)
- Python 3 with matplotlib (and pandas, if the script imports it)

## Build

```
g++ -O2 main.cpp -lbenchmark -lpthread -o test
```

## Run

```
./test --benchmark_repetitions=5 --benchmark_display_aggregates_only=true --benchmark_out=results.csv --benchmark_out_format=csv
```

## Regenerate the figures

```
python3 make_figures.py
```

## Notes

- Results depend on the machine. Cache line size and cache capacities were measured on each machine and are reported in the paper.
- Compiled binaries are not included. Build them with the command above.

## Paper

Link to be added once the preprint is posted.