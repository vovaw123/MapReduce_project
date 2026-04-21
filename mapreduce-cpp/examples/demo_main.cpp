#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "mapreduce.hpp"

// ---------------------------------------------------------------------------
// Common helpers
// ---------------------------------------------------------------------------
namespace {

struct Config {
    std::string input_file;
    std::string demo    = "classify"; // classify | wordcount
    std::string mode    = "threads";  // serial   | threads
    std::size_t threads = 4;
};

void print_usage() {
    std::cout <<
        "Usage: mapreduce_demo --input <file>\n"
        "                      [--demo classify|wordcount]\n"
        "                      [--mode serial|threads]\n"
        "                      [--threads N]\n";
}

bool parse_args(int argc, char* argv[], Config& cfg) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input"   && i + 1 < argc) cfg.input_file = argv[++i];
        else if (arg == "--demo"    && i + 1 < argc) cfg.demo       = argv[++i];
        else if (arg == "--mode"    && i + 1 < argc) cfg.mode       = argv[++i];
        else if (arg == "--threads" && i + 1 < argc)
            cfg.threads = static_cast<std::size_t>(std::stoul(argv[++i]));
        else return false;
    }
    if (cfg.input_file.empty())                              return false;
    if (cfg.demo != "classify" && cfg.demo != "wordcount")   return false;
    if (cfg.mode != "serial"   && cfg.mode != "threads")     return false;
    return true;
}

std::vector<std::string> load_lines(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) throw std::runtime_error("Cannot open: " + path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) lines.push_back(line);
    return lines;
}

// ---------------------------------------------------------------------------
// Demo 1 – classify_lines
//   "letters_only" : every character satisfies std::isalpha
//   "has_digits"   : at least one character satisfies std::isdigit
// ---------------------------------------------------------------------------
auto make_classify_map() {
    return [](const std::string& line) -> std::vector<std::pair<std::string, int>> {
        bool letters_only = true;
        for (unsigned char ch : line) {
            if (!std::isalpha(ch)) { letters_only = false; break; }
        }
        if (letters_only)
            return {{"letters_only", 1}};

        for (unsigned char ch : line) {
            if (std::isdigit(ch)) return {{"has_digits", 1}};
        }
        return {{"other", 1}};
    };
}

void run_classify(const std::vector<std::string>& input, const Config& cfg) {
    auto map_fn    = make_classify_map();
    auto reduce_fn = [](int a, int b) { return a + b; };

    std::unordered_map<std::string, int> result;
    if (cfg.mode == "serial")
        result = mapreduce::run_serial(input, map_fn, reduce_fn);
    else
        result = mapreduce::run_threaded(input, cfg.threads, map_fn, reduce_fn);

    std::cout << "letters_only=" << result["letters_only"] << "\n";
    std::cout << "has_digits="   << result["has_digits"]   << "\n";
    std::cout << "other="        << result["other"]        << "\n";
}

// ---------------------------------------------------------------------------
// Demo 2 – word_count
//   Splits each line into words; map emits (word, 1) for each word.
// ---------------------------------------------------------------------------
auto make_wordcount_map() {
    return [](const std::string& line) -> std::vector<std::pair<std::string, int>> {
        std::vector<std::pair<std::string, int>> pairs;
        std::istringstream ss(line);
        std::string word;
        while (ss >> word) {
            // strip leading/trailing punctuation
            while (!word.empty() && !std::isalnum(static_cast<unsigned char>(word.front())))
                word.erase(word.begin());
            while (!word.empty() && !std::isalnum(static_cast<unsigned char>(word.back())))
                word.pop_back();
            if (!word.empty()) pairs.push_back({word, 1});
        }
        return pairs;
    };
}

void run_wordcount(const std::vector<std::string>& input, const Config& cfg) {
    auto map_fn    = make_wordcount_map();
    auto reduce_fn = [](int a, int b) { return a + b; };

    std::unordered_map<std::string, int> result;
    if (cfg.mode == "serial")
        result = mapreduce::run_serial(input, map_fn, reduce_fn);
    else
        result = mapreduce::run_threaded(input, cfg.threads, map_fn, reduce_fn);

    // Print top words sorted by count (descending)
    std::vector<std::pair<std::string, int>> sorted(result.begin(), result.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b){ return a.second > b.second; });

    std::cout << "Total unique words: " << sorted.size() << "\n";
    const std::size_t top = std::min<std::size_t>(20, sorted.size());
    for (std::size_t i = 0; i < top; ++i)
        std::cout << sorted[i].first << "=" << sorted[i].second << "\n";
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    Config cfg;
    if (!parse_args(argc, argv, cfg)) { print_usage(); return 1; }

    try {
        const auto input = load_lines(cfg.input_file);

        const auto t0 = std::chrono::high_resolution_clock::now();

        if (cfg.demo == "classify")
            run_classify(input, cfg);
        else
            run_wordcount(input, cfg);

        const auto t1  = std::chrono::high_resolution_clock::now();
        const auto us  = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        std::cout << "demo="    << cfg.demo    << "\n";
        std::cout << "mode="    << cfg.mode    << "\n";
        std::cout << "threads=" << cfg.threads << "\n";
        std::cout << "items="   << input.size() << "\n";
        std::cout << "time_us=" << us           << "\n";
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
    return 0;
}

