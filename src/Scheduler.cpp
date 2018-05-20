#include "Scheduler.h"

Task::Task() {}

void Task::update() {
    nextRun = std::chrono::time_point_cast<task_precision>(
                  std::chrono::steady_clock::now()) +
              period;
}

Scheduler::Scheduler() {
    m_RunnerThread = std::thread(&Scheduler::RunnerThread, this);
}

void Scheduler::RunnerThread() {
    using std::chrono::steady_clock;
    using std::chrono::time_point;

    time_point<steady_clock> nextGarbageCollection = steady_clock::now();
    while (true) {
        std::vector<Task>::iterator nextTask;
        time_point<steady_clock> nextRun;
        {
            std::unique_lock<std::mutex> lk(m_TaskMutex);
            nextTask = std::min_element(m_Tasks.begin(), m_Tasks.end(),
                                        [](const Task& a, const Task& b) {
                                            return a.nextRun < b.nextRun;
                                        });
            nextRun = nextTask->nextRun;
        }

        std::this_thread::sleep_until(nextRun);

        {
            std::unique_lock<std::mutex> lk(m_FutureMutex);
            m_FuturePool.emplace_back(
                std::async(std::launch::async, nextTask->task));
            nextTask->update();
        }

        // Cleanup done futures every 10 seconds
        auto now = steady_clock::now();
        if (now > nextGarbageCollection) {
            // Update garbage collection timepoint
            nextGarbageCollection = now + std::chrono::seconds(10);
            std::unique_lock<std::mutex> lk(m_FutureMutex);
            // Remove done futures
            auto erasePoint = std::remove_if(
                m_FuturePool.begin(), m_FuturePool.end(),
                [](auto& future) { return Scheduler::isReady(future); });
            m_FuturePool.erase(erasePoint, m_FuturePool.end());
        }
    }
}

bool Scheduler::isReady(std::future<void>& future) {
    return future.wait_for(std::chrono::milliseconds(0)) ==
           std::future_status::ready;
}
