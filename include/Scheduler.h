#pragma once
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

using task_precision = std::chrono::milliseconds;

struct Task {
    /*
     * The task to execute
     */
    std::function<void()> task;

    /*
     * How often to execute it
     */
    task_precision period;

    /*
     * The time point of when the task should be run next
     */
    std::chrono::time_point<std::chrono::steady_clock> nextRun;

    /*
     * Main constructor for a task
     * @param t_task The function to store
     * @param t_period A std::chrono::duration type of how often to execute the
     * task. Gets converted to task_precision implicitly
     */
    template <typename Duration>
    Task(std::function<void()>& t_task, Duration t_period)
        : task{t_task}, period{t_period} {
        nextRun = std::chrono::steady_clock::now() + t_period;
    }

    Task();

    /*
     * Update the nextRun member to be the current time + the period
     */
    void update();
};

class Scheduler {
    /*
     * Main thread that runs the scheduler process
     */
    std::thread m_RunnerThread;

    /* Futures for running tasks stored here, futures come and go as tasks start
     * and finish */
    std::vector<std::future<void>> m_FuturePool;

    /*
     * A vector that stores the list of tasks to be executed
     */
    std::vector<Task> m_Tasks;

    /*
     * Mutex for accessing m_Tasks
     */
    std::mutex m_TaskMutex;

    /*
     * Mutex for accessing m_FuturePool
     */
    std::mutex m_FutureMutex;

   public:
    Scheduler();

    /*
     * The main thread (function) that manages the execution of tasks
     */
    void RunnerThread();

    /*
     * Function for scheduling a new task
     * @param task The task to periodically execute
     * @period A duration type convertable to
     * std::chrono::time_point<std::chrono::steady_clock> that signifies
     * approximately how often task should be executed
     */
    template <typename Duration>
    void Schedule(std::function<void()>& task, Duration period) {
        {
            std::unique_lock<std::mutex> lk(m_TaskMutex);
            m_Tasks.emplace_back(task, period);
        }
    }

    /*
     * Helper function for determining if a future is ready
     */
    static bool isReady(std::future<void>&);
};
