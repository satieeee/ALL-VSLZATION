/**
 * @file ThreadPool.h
 * @brief 持久线程池 —— 替代每帧 std::thread 创建/销毁，省 ~1.5ms/帧
 *
 * 用法：
 *   static ThreadPool pool;
 *   for (int i = 0; i < nChunks; ++i)
 *       pool.Enqueue([&]{ RenderChunk(start, end, ...); });
 *   pool.Wait();
 */

#pragma once
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(unsigned nThreads = 0) {
        if (nThreads == 0) {
            nThreads = std::thread::hardware_concurrency();
            if (nThreads < 1) nThreads = 1;
            if (nThreads > 16) nThreads = 16;
        }
        m_running = true;
        for (unsigned i = 0; i < nThreads; ++i)
            m_workers.emplace_back(&ThreadPool::Worker, this);
    }

    ~ThreadPool() {
        m_running = false;
        m_cv.notify_all();
        for (auto& t : m_workers)
            if (t.joinable()) t.join();
    }

    void Enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.push(std::move(task));
        }
        m_cv.notify_one();
    }

    void Wait() {
        // 自旋等待所有任务完成（比加一层 done CV 更轻量）
        while (true) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_tasks.empty() && m_active == 0) break;
        }
    }

    unsigned Size() const { return (unsigned)m_workers.size(); }

private:
    void Worker() {
        while (m_running) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]{ return !m_tasks.empty() || !m_running; });
                if (!m_running) return;
                task = std::move(m_tasks.front());
                m_tasks.pop();
                m_active++;
            }
            task();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_active--;
            }
            m_cv.notify_one();
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_active{0};
};
