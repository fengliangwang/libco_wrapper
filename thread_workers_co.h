
#pragma once

#include <list>
#include <memory>
#include <thread>
#include <atomic>

#include <assert.h>

#include <event2/event.h>

#include "co_routine.h"

//#include "queue.h"

#define MAX_WORKER_SIZE 100

class Task;

namespace utils {

class ThreadWorker {
public:
    int init(int idx, int coroutines=100, int copy_stack_k=128);
    int run(bool detach=false);

    void stop();
    void wait();

    int addTask(Task* task);

protected:
    static void runRoutine(void* arg);
    void runRoutineImp();

    void stopLoop();

private:
    static void newTaskCallback(int fd, short events, void* arg);
    static void exitTaskCallback(int fd, short events, void* arg);
    //static void timerCallback(int fd, short kind, void *userp);
    static int eventLoop(void* args);

    static void* CoConsumer(void* args);
    void done(Task* task);// invoke by CoConsumer

private:
    int idx_{0};
    int coroutines_{0};

    struct event_base *evbase_{nullptr};
    struct event *task_ev_{nullptr};
    struct event *exit_ev_{nullptr};
    struct event *timer_event_{nullptr};

    int task_pipes_[2];
    int exit_pipes_[2];

    std::thread::id thread_id_;
    std::shared_ptr<std::thread> worker_thread_;

    stCoCond_t* task_queue_cond_;
    //SynchronisedQueue<Task*> task_queue_;
    std::list<Task*> task_queue_;   // 同一线程访问
    long task_queue_size_{0};

    int copy_stack_k_{128};
    stShareStack_t* share_stack_ = nullptr;
    std::atomic<long> coroutines_running_{0};
    std::atomic<long> tasks_done_{0};
};

class ThreadWorkers {
public:
    int init(int32_t workers=2, int coroutines=100, int copy_stack_k=128);
    int destroy();

    int addTask(Task* task);

private:
    int32_t workers_n_{0};
    ThreadWorker workers_[MAX_WORKER_SIZE];
};

}

