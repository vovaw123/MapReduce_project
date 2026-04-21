// ---------------------------------------------------------------------------
// test_mapreduce.cpp  -  single demo-style test
// ---------------------------------------------------------------------------

#include <cassert>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "mapreduce.hpp"

static std::vector<std::pair<std::string, int>> classify_map(const std::string& line) {
    bool letters_only = true;
    for (unsigned char ch : line) {
        if (!std::isalpha(ch)) {
            letters_only = false;
            break;
        }
    }
    if (letters_only) return {{"letters_only", 1}};

    for (unsigned char ch : line) {
        if (std::isdigit(ch)) return {{"has_digits", 1}};
    }
    return {{"other", 1}};
}

static std::vector<std::pair<std::string, int>> wordcount_map(const std::string& line) {
    std::vector<std::pair<std::string, int>> pairs;
    std::istringstream ss(line);
    std::string token;
    while (ss >> token) {
        while (!token.empty() && !std::isalnum(static_cast<unsigned char>(token.front()))) {
            token.erase(token.begin());
        }
        while (!token.empty() && !std::isalnum(static_cast<unsigned char>(token.back()))) {
            token.pop_back();
        }
        if (!token.empty()) pairs.push_back({token, 1});
    }
    return pairs;
}

static int sum_reduce(int a, int b) { return a + b; }

int main() {
    // One compact demo test: strict classify_lines + word_count, and serial==threaded.
    {
        std::vector<std::string> input = {"abc", "hello!", "x9", "ONLY"};
        auto serial = mapreduce::run_serial(input, classify_map, sum_reduce);
        auto threaded = mapreduce::run_threaded(input, 2, classify_map, sum_reduce);

        assert(serial["letters_only"] == 2);
        assert(serial["has_digits"] == 1);
        assert(serial["other"] == 1);
        assert(threaded["letters_only"] == serial["letters_only"]);
        assert(threaded["has_digits"] == serial["has_digits"]);
        assert(threaded["other"] == serial["other"]);
    }

    {
        std::vector<std::string> input = {"hello world", "world foo", "hello hello"};
        auto serial = mapreduce::run_serial(input, wordcount_map, sum_reduce);
        auto threaded = mapreduce::run_threaded(input, 2, wordcount_map, sum_reduce);

        assert(serial["hello"] == 3);
        assert(serial["world"] == 2);
        assert(serial["foo"] == 1);
        assert(threaded["hello"] == serial["hello"]);
        assert(threaded["world"] == serial["world"]);
        assert(threaded["foo"] == serial["foo"]);
    }

    std::cout << "[PASS] demo test\n";
    return 0;
}


