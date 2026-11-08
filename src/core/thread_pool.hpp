#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace enim {

/// @brief Fixed set of worker threads running the items of one parallel loop at a time (view sampler tiles).
struct ThreadPool {
    /// @param workers Threads besides the caller of run(), which works too. 0 runs everything on the caller.
    explicit ThreadPool(std::size_t workers);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// @brief Calls fn(i) for i in [0, count) on the workers and the calling thread, and returns when all are done.
    /// One run() at a time.
    void run(std::size_t count, const std::function<void(std::size_t)>& fn);

    /// @brief Workers for this machine: one per hardware thread, minus the main thread.
    static std::size_t default_workers();

  private:
    std::mutex mutex;
    std::condition_variable work_cv; // a job started, or stopping
    std::condition_variable done_cv; // the last item finished
    const std::function<void(std::size_t)>* job = nullptr;
    std::size_t count = 0;
    std::size_t next = 0;    // next item to take
    std::size_t pending = 0; // items not finished
    std::size_t job_id = 0;
    bool stopping = false;
    std::vector<std::thread> threads;

    void work(std::unique_lock<std::mutex>& lock);
};

} // namespace enim
