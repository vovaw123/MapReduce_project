// ---------------------------------------------------------------------------
// word_count_mpi.cpp  –  MPI-backend demo for the MapReduce library
//
// Build:  cmake -DMAPREDUCE_USE_MPI=ON ..  &&  cmake --build .
// Run:    mpirun -np 4 ./mapreduce_mpi_demo --input ../../data/input.txt
//
// The program runs the same word-count job on all three backends
// (serial, threaded, MPI) and compares results + timing.
// Only rank 0 prints output.
// ---------------------------------------------------------------------------

#ifndef MAPREDUCE_USE_MPI
#define MAPREDUCE_USE_MPI
#endif
#include "mapreduce.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <mpi.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Map function: line  ->  [(word, 1), ...]
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::string, int>>
word_map(const std::string& line) {
    std::vector<std::pair<std::string, int>> pairs;
    std::istringstream ss(line);
    std::string token;
    while (ss >> token) {
        // strip leading/trailing non-alphanumeric characters
        while (!token.empty() &&
               !std::isalnum(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        while (!token.empty() &&
               !std::isalnum(static_cast<unsigned char>(token.back())))
            token.pop_back();
        if (!token.empty())
            pairs.push_back({token, 1});
    }
    return pairs;
}

// ---------------------------------------------------------------------------
// Reduce function: (count_a, count_b) -> count_a + count_b
// ---------------------------------------------------------------------------
static int word_reduce(int a, int b) { return a + b; }

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::vector<std::string> load_lines(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) throw std::runtime_error("Cannot open: " + path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) lines.push_back(line);
    return lines;
}

static void print_top(const std::unordered_map<std::string, int>& result,
                      std::size_t top_n = 10) {
    std::vector<std::pair<std::string, int>> sorted(result.begin(), result.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });
    std::cout << "  unique words : " << sorted.size() << "\n";
    const std::size_t n = std::min(top_n, sorted.size());
    for (std::size_t i = 0; i < n; ++i)
        std::cout << "  " << sorted[i].first << " = " << sorted[i].second << "\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);

    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Parse --input argument
    std::string input_path;
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == "--input") {
            input_path = argv[i + 1];
            break;
        }
    }
    if (input_path.empty()) {
        if (rank == 0)
            std::cerr << "Usage: mpirun -np N word_count_mpi --input <file>\n";
        MPI_Finalize();
        return 1;
    }

    // Only rank 0 loads the file; the MPI backend handles distribution
    std::vector<std::string> input;
    if (rank == 0) {
        try {
            input = load_lines(input_path);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    // ------------------------------------------------------------------
    // Run all three backends (rank 0 only for serial and threaded)
    // ------------------------------------------------------------------
    if (rank == 0) {
        // --- Serial ---
        auto t0 = std::chrono::high_resolution_clock::now();
        auto serial_result = mapreduce::run_serial(input, word_map, word_reduce);
        auto t1 = std::chrono::high_resolution_clock::now();
        auto us_serial = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        // --- Threaded ---
        const std::size_t nthreads = 4;
        t0 = std::chrono::high_resolution_clock::now();
        auto threaded_result = mapreduce::run_threaded(input, nthreads, word_map, word_reduce);
        t1 = std::chrono::high_resolution_clock::now();
        auto us_threaded = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        // Machine-readable lines for automation scripts.
        std::cout << "serial_time_us=" << us_serial << "\n";
        std::cout << "threaded_time_us=" << us_threaded << "\n";

        std::cout << "=== serial (" << us_serial << " us) ===\n";
        print_top(serial_result);
        std::cout << "=== threaded x" << nthreads << " (" << us_threaded << " us) ===\n";
        print_top(threaded_result);
    }

    // --- MPI (all ranks participate) ---
    MPI_Barrier(MPI_COMM_WORLD);
    auto t0 = std::chrono::high_resolution_clock::now();
    auto mpi_result = mapreduce::run_mpi(input, word_map, word_reduce);
    MPI_Barrier(MPI_COMM_WORLD);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto us_mpi = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (rank == 0) {
        std::cout << "mpi_ranks=" << size << "\n";
        std::cout << "mpi_time_us=" << us_mpi << "\n";
        std::cout << "=== MPI x" << size << " ranks (" << us_mpi << " us) ===\n";
        print_top(mpi_result);
    }

    MPI_Finalize();
    return 0;
}
