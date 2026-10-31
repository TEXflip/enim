#include "thread_pool.hpp"

#include <algorithm>

namespace enim {

ThreadPool::ThreadPool(std::size_t workers) {
    for (std::size_t i = 0; i < workers; ++i)
        this->threads.emplace_back([this] {
            std::unique_lock lock(this->mutex);
            std::size_t seen = 0;
            while (true) {
                this->work_cv.wait(lock, [&] { return this->stopping || (this->job && this->job_id != seen); });
                if (this->stopping)
                    return;
                seen = this->job_id;
                this->work(lock);
            }
        });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard lock(this->mutex);
        this->stopping = true;
    }
    this->work_cv.notify_all();
    for (std::thread& thread : this->threads)
        thread.join();
}

void ThreadPool::work(std::unique_lock<std::mutex>& lock) {
    while (this->job && this->next < this->count) {
        std::size_t i = this->next++;
        const auto& fn = *this->job;
        lock.unlock();
        fn(i);
        lock.lock();
        if (--this->pending == 0)
            this->done_cv.notify_all();
    }
}

void ThreadPool::run(std::size_t count, const std::function<void(std::size_t)>& fn) {
    if (count == 0)
        return;
    std::unique_lock lock(this->mutex);
    this->job = &fn;
    this->count = count;
    this->next = 0;
    this->pending = count;
    ++this->job_id;
    this->work_cv.notify_all();

    this->work(lock);
    this->done_cv.wait(lock, [&] { return this->pending == 0; });
    this->job = nullptr;
}

std::size_t ThreadPool::default_workers() {
    unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 1 ? hardware - 1 : 0;
}

} // namespace enim
