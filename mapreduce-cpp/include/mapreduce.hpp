#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mapreduce {

template <typename Input, typename MapFn>
using MapOutputT = std::invoke_result_t<MapFn, const Input&>;

template <typename PairType>
using KeyT = std::decay_t<decltype(std::declval<PairType>().first)>;

template <typename PairType>
using ValueT = std::decay_t<decltype(std::declval<PairType>().second)>;

template <typename Input, typename MapFn, typename ReduceFn>
auto run_serial(const std::vector<Input>& input, MapFn map_fn, ReduceFn reduce_fn)
    -> std::unordered_map<KeyT<MapOutputT<Input, MapFn>>, ValueT<MapOutputT<Input, MapFn>>> {
    using PairType = MapOutputT<Input, MapFn>;
    using Key = KeyT<PairType>;
    using Value = ValueT<PairType>;

    std::unordered_map<Key, Value> result;
    for (const auto& item : input) {
        auto mapped = map_fn(item);
        auto it = result.find(mapped.first);
        if (it == result.end()) {
            result.emplace(mapped.first, mapped.second);
        } else {
            it->second = reduce_fn(it->second, mapped.second);
        }
    }
    return result;
}

template <typename Input, typename MapFn, typename ReduceFn>
auto run_threaded(const std::vector<Input>& input, std::size_t thread_count, MapFn map_fn, ReduceFn reduce_fn)
    -> std::unordered_map<KeyT<MapOutputT<Input, MapFn>>, ValueT<MapOutputT<Input, MapFn>>> {
    using PairType = MapOutputT<Input, MapFn>;
    using Key = KeyT<PairType>;
    using Value = ValueT<PairType>;

    if (thread_count == 0) {
        thread_count = 1;
    }
    thread_count = std::min<std::size_t>(thread_count, std::max<std::size_t>(1, input.size()));

    std::unordered_map<Key, Value> shared_result;
    std::mutex result_mutex;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    const std::size_t chunk_size = (input.size() + thread_count - 1) / thread_count;

    for (std::size_t index = 0; index < thread_count; ++index) {
        const std::size_t start = index * chunk_size;
        const std::size_t end = std::min(input.size(), start + chunk_size);

        workers.emplace_back([&, start, end]() {
            std::unordered_map<Key, Value> local_result;
            for (std::size_t pos = start; pos < end; ++pos) {
                auto mapped = map_fn(input[pos]);
                auto it = local_result.find(mapped.first);
                if (it == local_result.end()) {
                    local_result.emplace(mapped.first, mapped.second);
                } else {
                    it->second = reduce_fn(it->second, mapped.second);
                }
            }

            std::lock_guard<std::mutex> lock(result_mutex);
            for (const auto& [key, value] : local_result) {
                auto it = shared_result.find(key);
                if (it == shared_result.end()) {
                    shared_result.emplace(key, value);
                } else {
                    it->second = reduce_fn(it->second, value);
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    return shared_result;
}

}
