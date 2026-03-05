#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "mapreduce.hpp"

namespace {

struct Config {
    std::string input_file;
    std::string mode = "threads";
    std::size_t threads = 4;
};

bool has_digit(const std::string& value) {
    for (char ch : value) {
        if (ch >= '0' && ch <= '9') {
            return true;
        }
    }
    return false;
}

void print_usage() {
    std::cout << "Usage: mapreduce_demo --input <file> [--mode serial|threads] [--threads N]\n";
}

bool parse_args(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            config.input_file = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            config.mode = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            config.threads = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else {
            return false;
        }
    }

    if (config.input_file.empty()) {
        return false;
    }
    if (config.mode != "serial" && config.mode != "threads") {
        return false;
    }
    return true;
}

std::vector<std::string> load_input(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        throw std::runtime_error("Cannot open input file: " + path);
    }

    std::vector<std::string> data;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            data.push_back(line);
        }
    }
    return data;
}

void print_result(const std::unordered_map<std::string, int>& result) {
    const int letters = result.count("letters_only") ? result.at("letters_only") : 0;
    const int alpha_num = result.count("has_digits") ? result.at("has_digits") : 0;
    std::cout << "letters_only=" << letters << "\n";
    std::cout << "has_digits=" << alpha_num << "\n";
}

} 

int main(int argc, char* argv[]) {
    Config config;
    if (!parse_args(argc, argv, config)) {
        print_usage();
        return 1;
    }

    try {
        const auto input = load_input(config.input_file);

        auto map_fn = [](const std::string& item) -> std::pair<std::string, int> {
            if (has_digit(item)) {
                return {"has_digits", 1};
            }
            return {"letters_only", 1};
        };

        auto reduce_fn = [](int left, int right) -> int {
            return left + right;
        };

        const auto start = std::chrono::high_resolution_clock::now();
        std::unordered_map<std::string, int> result;
        std::size_t effective_threads = 1;

        if (config.mode == "serial") {
            result = mapreduce::run_serial(input, map_fn, reduce_fn);
        } else {
            effective_threads = config.threads;
            result = mapreduce::run_threaded(input, config.threads, map_fn, reduce_fn);
        }

        const auto end = std::chrono::high_resolution_clock::now();
        const auto elapsed_us =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "mode=" << config.mode << "\n";
        std::cout << "threads=" << effective_threads << "\n";
        std::cout << "items=" << input.size() << "\n";
        std::cout << "time_us=" << elapsed_us << "\n";
        print_result(result);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    return 0;
}
