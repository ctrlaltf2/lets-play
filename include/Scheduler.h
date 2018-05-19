#pragma once
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using task_precision = std::chrono::milliseconds;

struct Task {
    std::function<void()> task;
    task_precision period;
    std::chrono::time_point<std::chrono::steady_clock> nextRun;

    template <typename Duration>
    Task(std::function<void()>& t_task, Duration t_period)
        : task{t_task}, period{t_period} {
        nextRun = std::chrono::steady_clock::now() + t_period;
    }

    bool ready(const std::chrono::time_point<std::chrono::steady_clock>&) const;

    void update();
};

class Scheduler {
    std::thread m_RunnerThread;

    std::vector<std::future<void>> m_FuturePool;

    std::vector<Task> m_Tasks;

    std::mutex m_TaskMutex, m_FutureMutex;

   public:
    Scheduler();

    void RunnerThread();

    template <typename Duration>
    void Schedule(std::function<void()>& task, Duration period) {
        {
            std::unique_lock<std::mutex> lk(m_TaskMutex);
            m_Tasks.emplace_back(task, period);
        }
    }

    static bool isReady(std::future<void>&);
};
