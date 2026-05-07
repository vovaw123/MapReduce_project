// ---------------------------------------------------------------------------
// test_mapreduce.cpp  -  expanded test suite
// ---------------------------------------------------------------------------

#include <cassert>
#include <cctype>
#include <fstream>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include "mapreduce.hpp"

// ---------------------------------------------------------------------------
// Map / reduce helpers
// ---------------------------------------------------------------------------
static std::vector<std::pair<std::string, int>> classify_map(const std::string& line) {
    bool letters_only = true;
    for (unsigned char ch : line) {
        if (!std::isalpha(ch)) { letters_only = false; break; }
    }
    if (letters_only) return {{"letters_only", 1}};

    for (unsigned char ch : line)
        if (std::isdigit(ch)) return {{"has_digits", 1}};

    return {{"other", 1}};
}

static std::vector<std::pair<std::string, int>> wordcount_map(const std::string& line) {
    std::vector<std::pair<std::string, int>> pairs;
    std::istringstream ss(line);
    std::string token;
    while (ss >> token) {
        while (!token.empty() && !std::isalnum(static_cast<unsigned char>(token.front())))
            token.erase(token.begin());
        while (!token.empty() && !std::isalnum(static_cast<unsigned char>(token.back())))
            token.pop_back();
        if (!token.empty()) pairs.push_back({token, 1});
    }
    return pairs;
}

static int sum_reduce(int a, int b) { return a + b; }

// ---------------------------------------------------------------------------
// File helper
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

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static void test_classify_basic() {
    std::vector<std::string> input = {"abc", "hello!", "x9", "ONLY"};
    auto serial   = mapreduce::run_serial(input, classify_map, sum_reduce);
    auto threaded = mapreduce::run_threaded(input, 2, classify_map, sum_reduce);

    assert(serial["letters_only"] == 2);
    assert(serial["has_digits"]   == 1);
    assert(serial["other"]        == 1);
    assert(threaded == serial);
    std::cout << "[PASS] test_classify_basic\n";
}

static void test_wordcount_basic() {
    std::vector<std::string> input = {"hello world", "world foo", "hello hello"};
    auto serial   = mapreduce::run_serial(input, wordcount_map, sum_reduce);
    auto threaded = mapreduce::run_threaded(input, 2, wordcount_map, sum_reduce);

    assert(serial["hello"] == 3);
    assert(serial["world"] == 2);
    assert(serial["foo"]   == 1);
    assert(threaded == serial);
    std::cout << "[PASS] test_wordcount_basic\n";
}

static void test_empty_input() {
    std::vector<std::string> input = {};
    assert(mapreduce::run_serial(input, classify_map, sum_reduce).empty());
    assert(mapreduce::run_threaded(input, 4, classify_map, sum_reduce).empty());
    assert(mapreduce::run_serial(input, wordcount_map, sum_reduce).empty());
    assert(mapreduce::run_threaded(input, 4, wordcount_map, sum_reduce).empty());
    std::cout << "[PASS] test_empty_input\n";
}

static void test_filter_map_returns_zero_pairs() {
    // map that filters: only emits a pair for strings longer than 10 chars
    auto filter_map = [](const std::string& s) -> std::vector<std::pair<std::string, int>> {
        if (s.size() > 10) return {{"long", 1}};
        return {};
    };
    std::vector<std::string> input = {"hi", "ok", "no"};
    assert(mapreduce::run_serial(input, filter_map, sum_reduce).empty());
    assert(mapreduce::run_threaded(input, 4, filter_map, sum_reduce).empty());
    std::cout << "[PASS] test_filter_map_returns_zero_pairs\n";
}

static void test_threads_exceed_input_size() {
    // 100 threads on 2 items – must not crash and must match serial
    std::vector<std::string> input = {"hello world", "foo bar"};
    auto serial   = mapreduce::run_serial(input, wordcount_map, sum_reduce);
    auto threaded = mapreduce::run_threaded(input, 100, wordcount_map, sum_reduce);
    assert(serial == threaded);
    std::cout << "[PASS] test_threads_exceed_input_size\n";
}

static void test_single_item() {
    std::vector<std::string> input = {"one"};
    auto serial   = mapreduce::run_serial(input, wordcount_map, sum_reduce);
    auto threaded = mapreduce::run_threaded(input, 4, wordcount_map, sum_reduce);
    assert(serial["one"] == 1);
    assert(serial == threaded);
    std::cout << "[PASS] test_single_item\n";
}

static void test_punctuation_only_stripped() {
    // wordcount_map on a line of pure punctuation should emit 0 pairs
    std::vector<std::string> input = {"!!! ??? ---"};
    auto result = mapreduce::run_serial(input, wordcount_map, sum_reduce);
    assert(result.empty());
    std::cout << "[PASS] test_punctuation_only_stripped\n";
}

static void test_iterator_api() {
    std::vector<std::string> input = {"hello world", "world foo"};

    // vector overload vs iterator overload must agree
    auto serial_vec = mapreduce::run_serial(input, wordcount_map, sum_reduce);
    auto serial_it  = mapreduce::run_serial(input.begin(), input.end(), wordcount_map, sum_reduce);
    assert(serial_vec == serial_it);

    // threaded iterator overload
    auto threaded_it = mapreduce::run_threaded(input.begin(), input.end(), 2, wordcount_map, sum_reduce);
    assert(serial_vec == threaded_it);

    // works on std::list (not random-access)
    std::list<std::string> lst(input.begin(), input.end());
    auto serial_list = mapreduce::run_serial(lst.begin(), lst.end(), wordcount_map, sum_reduce);
    assert(serial_vec == serial_list);

    // partial range: only first element
    auto partial = mapreduce::run_serial(input.begin(), input.begin() + 1, wordcount_map, sum_reduce);
    assert(partial["hello"] == 1);
    assert(partial["world"] == 1);
    assert(partial.count("foo") == 0);

    std::cout << "[PASS] test_iterator_api\n";
}

static void test_fish_txt_wordcount() {
    const std::string path = MAPREDUCE_DATA_DIR "/fish.txt";
    std::vector<std::string> lines;
    try {
        lines = load_lines(path);
    } catch (...) {
        std::cout << "[SKIP] test_fish_txt_wordcount (file not found: " << path << ")\n";
        return;
    }
    assert(!lines.empty());
    auto serial   = mapreduce::run_serial(lines, wordcount_map, sum_reduce);
    auto threaded = mapreduce::run_threaded(lines, 4, wordcount_map, sum_reduce);
    assert(!serial.empty());
    assert(serial == threaded);
    std::cout << "[PASS] test_fish_txt_wordcount (lines=" << lines.size()
              << " unique_words=" << serial.size() << ")\n";
}

static void test_combine_txt() {
    const std::string path = MAPREDUCE_DATA_DIR "/combine.txt";
    std::vector<std::string> lines;
    try {
        lines = load_lines(path);
    } catch (...) {
        std::cout << "[SKIP] test_combine_txt (file not found: " << path << ")\n";
        return;
    }
    assert(!lines.empty());

    // wordcount: serial == threaded, not empty
    auto wc_serial   = mapreduce::run_serial(lines, wordcount_map, sum_reduce);
    auto wc_threaded = mapreduce::run_threaded(lines, 4, wordcount_map, sum_reduce);
    assert(!wc_serial.empty());
    assert(wc_serial == wc_threaded);

    // classify: file has lines with digits → has_digits key must exist
    auto cl_serial   = mapreduce::run_serial(lines, classify_map, sum_reduce);
    auto cl_threaded = mapreduce::run_threaded(lines, 4, classify_map, sum_reduce);
    assert(cl_serial.count("has_digits") > 0);
    assert(cl_serial == cl_threaded);

    std::cout << "[PASS] test_combine_txt (lines=" << lines.size()
              << " unique_words=" << wc_serial.size() << ")\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    test_classify_basic();
    test_wordcount_basic();
    test_empty_input();
    test_filter_map_returns_zero_pairs();
    test_threads_exceed_input_size();
    test_single_item();
    test_punctuation_only_stripped();
    test_iterator_api();
    test_fish_txt_wordcount();
    test_combine_txt();

    std::cout << "\n[ALL PASS]\n";
    return 0;
}


