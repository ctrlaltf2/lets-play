#include "Scheduler.h"

bool Task::ready(
    const std::chrono::time_point<std::chrono::steady_clock>& now) const {
    return now > nextRun;
}

void Task::update() {
    nextRun = std::chrono::time_point_cast<task_precision>(
                  std::chrono::steady_clock::now()) +
              period;
}

Scheduler::Scheduler() {
    m_RunnerThread = std::thread(&Scheduler::RunnerThread, this);
}

void Scheduler::RunnerThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Add new tasks
        {
            std::unique_lock<std::mutex> lk(m_TaskMutex);
            auto now = std::chrono::steady_clock::now();
            for (Task& t : m_Tasks) {
                if (t.ready(now)) {
                    {
                        std::unique_lock<std::mutex> llk(m_FutureMutex);
                        m_FuturePool.emplace_back(
                            std::async(std::launch::async, t.task));
                    }
                    t.update();
                }
            }
        }
        // Erase old ones
        {
            std::unique_lock<std::mutex> lk(m_FutureMutex);
            auto erasePoint = std::remove_if(
                m_FuturePool.begin(), m_FuturePool.end(),
                [](auto& future) { return Scheduler::isReady(future); });
            m_FuturePool.erase(erasePoint, m_FuturePool.end());
        }
        // std::cout << m_Tasks.size() << ' ' << m_FuturePool.size() <<
        // '\n';
    }
}

bool Scheduler::isReady(std::future<void>& future) {
    return future.wait_for(std::chrono::milliseconds(0)) ==
           std::future_status::ready;
}
