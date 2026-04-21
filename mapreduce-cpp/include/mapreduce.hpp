#pragma once

// ---------------------------------------------------------------------------
// mapreduce.hpp  –  header-only MapReduce library
//
// Supported backends
//   mapreduce::run_serial   – single-threaded
//   mapreduce::run_threaded – std::thread pool (shared memory, 1 machine)
//   mapreduce::run_mpi      – MPI (distributed, multiple machines)
//                             compiled only when MAPREDUCE_USE_MPI is defined
//
// Map contract
//   MapFn : (const Input&) -> std::vector<std::pair<Key, Value>>
//   The function may return 0, 1, or many pairs per input element.
//
// Reduce contract
//   ReduceFn : (Value, Value) -> Value   (associative combiner)
// ---------------------------------------------------------------------------

#include <algorithm>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef MAPREDUCE_USE_MPI
#include <cstring>
#include <mpi.h>
#endif

namespace mapreduce {

// ---------------------------------------------------------------------------
// Internal type helpers
// ---------------------------------------------------------------------------
namespace detail {

template <typename Input, typename MapFn>
using VecPairT = std::invoke_result_t<MapFn, const Input&>; // std::vector<pair<K,V>>

template <typename VecPair>
using PairT = typename VecPair::value_type;

template <typename VecPair>
using KeyT = std::decay_t<decltype(std::declval<PairT<VecPair>>().first)>;

template <typename VecPair>
using ValueT = std::decay_t<decltype(std::declval<PairT<VecPair>>().second)>;

// Merge a local unordered_map into a global one, applying reduce_fn on collision.
template <typename Key, typename Value, typename ReduceFn>
void merge_into(std::unordered_map<Key, Value>& dst,
                const std::unordered_map<Key, Value>& src,
                ReduceFn reduce_fn) {
    for (const auto& [key, value] : src) {
        auto it = dst.find(key);
        if (it == dst.end()) {
            dst.emplace(key, value);
        } else {
            it->second = reduce_fn(it->second, value);
        }
    }
}

// Apply map_fn to one item and fold the resulting pairs into dst.
template <typename Input, typename Key, typename Value, typename MapFn, typename ReduceFn>
void map_and_fold(std::unordered_map<Key, Value>& dst,
                  const Input& item,
                  MapFn map_fn,
                  ReduceFn reduce_fn) {
    for (const auto& [key, val] : map_fn(item)) {
        auto it = dst.find(key);
        if (it == dst.end()) {
            dst.emplace(key, val);
        } else {
            it->second = reduce_fn(it->second, val);
        }
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// Backend 1 – Serial
// ---------------------------------------------------------------------------
template <typename Input, typename MapFn, typename ReduceFn>
auto run_serial(const std::vector<Input>& input, MapFn map_fn, ReduceFn reduce_fn) {
    using VecPair = detail::VecPairT<Input, MapFn>;
    using Key     = detail::KeyT<VecPair>;
    using Value   = detail::ValueT<VecPair>;

    std::unordered_map<Key, Value> result;
    for (const auto& item : input) {
        detail::map_and_fold(result, item, map_fn, reduce_fn);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Backend 2 – Threaded (shared memory, single machine)
// ---------------------------------------------------------------------------
template <typename Input, typename MapFn, typename ReduceFn>
auto run_threaded(const std::vector<Input>& input,
                  std::size_t thread_count,
                  MapFn map_fn,
                  ReduceFn reduce_fn) {
    using VecPair = detail::VecPairT<Input, MapFn>;
    using Key     = detail::KeyT<VecPair>;
    using Value   = detail::ValueT<VecPair>;

    if (thread_count == 0) thread_count = 1;
    thread_count = std::min<std::size_t>(thread_count, std::max<std::size_t>(1, input.size()));

    std::unordered_map<Key, Value> shared_result;
    std::mutex result_mutex;
    std::vector<std::thread> workers;
    workers.reserve(thread_count);

    const std::size_t chunk_size = (input.size() + thread_count - 1) / thread_count;

    for (std::size_t index = 0; index < thread_count; ++index) {
        const std::size_t seg_start = index * chunk_size;
        const std::size_t seg_end   = std::min(input.size(), seg_start + chunk_size);

        workers.emplace_back([&, seg_start, seg_end]() {
            std::unordered_map<Key, Value> local;
            for (std::size_t pos = seg_start; pos < seg_end; ++pos) {
                detail::map_and_fold(local, input[pos], map_fn, reduce_fn);
            }
            std::lock_guard<std::mutex> lock(result_mutex);
            detail::merge_into(shared_result, local, reduce_fn);
        });
    }

    for (auto& w : workers) w.join();
    return shared_result;
}

// ---------------------------------------------------------------------------
// Backend 3 – MPI  (multi-machine distributed)
//   Compiled only when MAPREDUCE_USE_MPI is defined.
//   Algorithm:
//     1. Scatter  – rank 0 distributes input lines to all ranks
//     2. Local map – each rank maps its slice (0..N pairs per item)
//     3. Shuffle  – MPI_Alltoallv routes each pair to rank = hash(key) % P
//     4. Local reduce – each rank reduces the pairs it owns
//     5. Gather   – rank 0 collects partial results (no re-reduce needed
//                   because hash routing gives each key a unique owner)
//
//   Constraints for MPI backend:
//     - Input  : std::vector<std::string>
//     - Key    : std::string
//     - Value  : int
//   Call MPI_Init / MPI_Finalize in the application, not in this function.
//   Only rank 0 returns a non-empty result.
// ---------------------------------------------------------------------------
#ifdef MAPREDUCE_USE_MPI

namespace detail {

// Pack a vector of strings into a flat byte buffer:
//   [int32 len][bytes...] repeated
inline std::vector<char> pack_strings(const std::vector<std::string>& strs) {
    std::vector<char> buf;
    for (const auto& s : strs) {
        auto len = static_cast<int32_t>(s.size());
        const char* lp = reinterpret_cast<const char*>(&len);
        buf.insert(buf.end(), lp, lp + 4);
        buf.insert(buf.end(), s.begin(), s.end());
    }
    return buf;
}

inline std::vector<std::string> unpack_strings(const char* data, int bytes) {
    std::vector<std::string> result;
    const char* ptr = data;
    const char* end = data + bytes;
    while (ptr + 4 <= end) {
        int32_t len = 0;
        std::memcpy(&len, ptr, 4);
        ptr += 4;
        if (ptr + len > end) break;
        result.emplace_back(ptr, ptr + len);
        ptr += len;
    }
    return result;
}

// Pack a vector of (string, int) pairs:
//   [int32 klen][key bytes][int32 value] repeated
inline std::vector<char> pack_pairs(const std::vector<std::pair<std::string, int>>& pairs) {
    std::vector<char> buf;
    for (const auto& [key, val] : pairs) {
        auto klen = static_cast<int32_t>(key.size());
        const char* lp = reinterpret_cast<const char*>(&klen);
        buf.insert(buf.end(), lp, lp + 4);
        buf.insert(buf.end(), key.begin(), key.end());
        const char* vp = reinterpret_cast<const char*>(&val);
        buf.insert(buf.end(), vp, vp + 4);
    }
    return buf;
}

inline std::vector<std::pair<std::string, int>> unpack_pairs(const char* data, int bytes) {
    std::vector<std::pair<std::string, int>> result;
    const char* ptr = data;
    const char* end = data + bytes;
    while (ptr + 4 <= end) {
        int32_t klen = 0;
        std::memcpy(&klen, ptr, 4);
        ptr += 4;
        if (ptr + klen + 4 > end) break;
        std::string key(ptr, ptr + klen);
        ptr += klen;
        int val = 0;
        std::memcpy(&val, ptr, 4);
        ptr += 4;
        result.emplace_back(std::move(key), val);
    }
    return result;
}

} // namespace detail

template <typename MapFn, typename ReduceFn>
std::unordered_map<std::string, int>
run_mpi(const std::vector<std::string>& input, MapFn map_fn, ReduceFn reduce_fn) {
    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // ------------------------------------------------------------------
    // 1. SCATTER – rank 0 sends each rank its slice of input
    // ------------------------------------------------------------------
    std::vector<std::string> local_input;

    if (size == 1) {
        local_input = input;
    } else {
        int total = static_cast<int>(input.size());
        MPI_Bcast(&total, 1, MPI_INT, 0, MPI_COMM_WORLD);

        const int chunk = (total + size - 1) / size;

        if (rank == 0) {
            // Keep own slice
            int my_end = std::min(chunk, total);
            local_input = std::vector<std::string>(input.begin(), input.begin() + my_end);

            // Send slices to other ranks
            for (int r = 1; r < size; ++r) {
                int r_start = std::min(r * chunk, total);
                int r_end   = std::min(r_start + chunk, total);
                std::vector<std::string> slice(input.begin() + r_start,
                                               input.begin() + r_end);
                auto buf   = detail::pack_strings(slice);
                auto bytes = static_cast<int>(buf.size());
                MPI_Send(&bytes,    1,     MPI_INT,  r, 0, MPI_COMM_WORLD);
                if (bytes > 0)
                    MPI_Send(buf.data(), bytes, MPI_BYTE, r, 1, MPI_COMM_WORLD);
            }
        } else {
            int bytes = 0;
            MPI_Recv(&bytes, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (bytes > 0) {
                std::vector<char> buf(bytes);
                MPI_Recv(buf.data(), bytes, MPI_BYTE, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                local_input = detail::unpack_strings(buf.data(), bytes);
            }
        }
    }

    // ------------------------------------------------------------------
    // 2. LOCAL MAP – each rank maps its slice (may emit 0..N pairs/item)
    // ------------------------------------------------------------------
    std::vector<std::pair<std::string, int>> mapped_pairs;
    for (const auto& item : local_input) {
        for (auto& p : map_fn(item))
            mapped_pairs.push_back(std::move(p));
    }

    // ------------------------------------------------------------------
    // 3. SHUFFLE – route each pair to owner rank = hash(key) % P
    //    using MPI_Alltoallv on serialized byte buffers
    // ------------------------------------------------------------------
    // Build per-rank send buffers
    std::vector<std::vector<char>> send_bufs(static_cast<std::size_t>(size));
    for (const auto& [key, val] : mapped_pairs) {
        int target = static_cast<int>(
            std::hash<std::string>{}(key) % static_cast<std::size_t>(size));
        auto klen = static_cast<int32_t>(key.size());
        auto& buf = send_bufs[static_cast<std::size_t>(target)];
        const char* lp = reinterpret_cast<const char*>(&klen);
        buf.insert(buf.end(), lp, lp + 4);
        buf.insert(buf.end(), key.begin(), key.end());
        const char* vp = reinterpret_cast<const char*>(&val);
        buf.insert(buf.end(), vp, vp + 4);
    }

    std::vector<int> send_counts(static_cast<std::size_t>(size));
    std::vector<int> send_displs(static_cast<std::size_t>(size));
    for (int r = 0; r < size; ++r)
        send_counts[static_cast<std::size_t>(r)] =
            static_cast<int>(send_bufs[static_cast<std::size_t>(r)].size());
    send_displs[0] = 0;
    for (int r = 1; r < size; ++r)
        send_displs[static_cast<std::size_t>(r)] =
            send_displs[static_cast<std::size_t>(r) - 1] +
            send_counts[static_cast<std::size_t>(r) - 1];

    int total_send = send_displs[static_cast<std::size_t>(size - 1)] +
                     send_counts[static_cast<std::size_t>(size - 1)];
    std::vector<char> send_flat(static_cast<std::size_t>(total_send));
    for (int r = 0; r < size; ++r) {
        std::copy(send_bufs[static_cast<std::size_t>(r)].begin(),
                  send_bufs[static_cast<std::size_t>(r)].end(),
                  send_flat.begin() + send_displs[static_cast<std::size_t>(r)]);
    }

    // Exchange counts, then data
    std::vector<int> recv_counts(static_cast<std::size_t>(size));
    MPI_Alltoall(send_counts.data(), 1, MPI_INT,
                 recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);

    std::vector<int> recv_displs(static_cast<std::size_t>(size));
    recv_displs[0] = 0;
    for (int r = 1; r < size; ++r)
        recv_displs[static_cast<std::size_t>(r)] =
            recv_displs[static_cast<std::size_t>(r) - 1] +
            recv_counts[static_cast<std::size_t>(r) - 1];
    int total_recv = recv_displs[static_cast<std::size_t>(size - 1)] +
                     recv_counts[static_cast<std::size_t>(size - 1)];
    std::vector<char> recv_flat(static_cast<std::size_t>(std::max(total_recv, 1)));

    MPI_Alltoallv(send_flat.data(), send_counts.data(), send_displs.data(), MPI_BYTE,
                  recv_flat.data(), recv_counts.data(), recv_displs.data(), MPI_BYTE,
                  MPI_COMM_WORLD);

    // ------------------------------------------------------------------
    // 4. LOCAL REDUCE – each rank reduces the pairs it owns
    // ------------------------------------------------------------------
    auto received = detail::unpack_pairs(recv_flat.data(), total_recv);
    std::unordered_map<std::string, int> local_result;
    for (const auto& [key, val] : received) {
        auto it = local_result.find(key);
        if (it == local_result.end())
            local_result.emplace(key, val);
        else
            it->second = reduce_fn(it->second, val);
    }

    // ------------------------------------------------------------------
    // 5. GATHER – rank 0 collects results from all ranks
    //    Because each key is owned by exactly one rank (hash routing),
    //    no second reduce is needed – just union the maps.
    // ------------------------------------------------------------------
    std::unordered_map<std::string, int> final_result;

    if (rank == 0) {
        final_result = std::move(local_result);
        for (int r = 1; r < size; ++r) {
            int bytes = 0;
            MPI_Recv(&bytes, 1, MPI_INT, r, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            if (bytes > 0) {
                std::vector<char> buf(static_cast<std::size_t>(bytes));
                MPI_Recv(buf.data(), bytes, MPI_BYTE, r, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                for (const auto& [key, val] : detail::unpack_pairs(buf.data(), bytes))
                    final_result[key] = val;
            }
        }
    } else {
        auto pairs = std::vector<std::pair<std::string, int>>(
            local_result.begin(), local_result.end());
        auto buf   = detail::pack_pairs(pairs);
        auto bytes = static_cast<int>(buf.size());
        MPI_Send(&bytes,    1,     MPI_INT,  0, 2, MPI_COMM_WORLD);
        if (bytes > 0)
            MPI_Send(buf.data(), bytes, MPI_BYTE, 0, 3, MPI_COMM_WORLD);
    }

    return final_result; // non-empty only on rank 0
}

#endif // MAPREDUCE_USE_MPI

} // namespace mapreduce
