#include <cassert>
#include <string>
#include <vector>

#include "mapreduce.hpp"

int main() {
    std::vector<std::string> input = {
        "abc", "z1", "hello", "99b", "world", "x2", "onlyletters"};

    auto map_fn = [](const std::string& item) -> std::pair<std::string, int> {
        bool has_digit = false;
        for (char c : item) {
            if (c >= '0' && c <= '9') {
                has_digit = true;
                break;
            }
        }
        return {has_digit ? "has_digits" : "letters_only", 1};
    };

    auto reduce_fn = [](int left, int right) {
        return left + right;
    };

    auto serial = mapreduce::run_serial(input, map_fn, reduce_fn);
    auto threaded = mapreduce::run_threaded(input, 4, map_fn, reduce_fn);

    assert(serial["letters_only"] == 4);
    assert(serial["has_digits"] == 3);
    assert(threaded["letters_only"] == serial["letters_only"]);
    assert(threaded["has_digits"] == serial["has_digits"]);
    return 0;
}
