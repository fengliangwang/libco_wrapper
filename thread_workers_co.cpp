
#include "thread_workers_co.h"

#include <vector>
#include <unistd.h>

#include "task.h"
#include "logging.h"

namespace utils {

#define _MIN_(a, b) (a)>=(b)?(b):(a)
#define _MAX_(a, b) (a)>=(b)?(a):(b)

int ThreadWorker::init(int idx, int coroutines, int copy_stack_k) {

    idx_ = idx;
    coroutines_ = _MIN_(_MAX_(coroutines, 1), 1000); // between [1, 1000]
    copy_stack_k_ = _MIN_(_MAX_(copy_stack_k, 0), 10240); // between [0, 10M]

    if (pipe(task_pipes_)) {
        return -1;
    }
    if (pipe(exit_pipes_)) {
        return -1;
    }
    evbase_ = event_base_new();
    //timer_event_ = evtimer_new(evbase_, timerCallback, this);
  
    task_ev_ = event_new(evbase_, task_pipes_[0], EV_READ|EV_PERSIST, newTaskCallback, (void*)this);
    event_add(task_ev_, NULL);

    exit_ev_ = event_new(evbase_, exit_pipes_[0], EV_READ|EV_PERSIST, exitTaskCallback, (void*)this);
    event_add(exit_ev_, NULL);

    return 0;
}

struct stEnv_t {
    int idx;
    ThreadWorker* w;
    stCoRoutine_t* consumer_routine;
};

void* ThreadWorker::CoConsumer(void* args) {
    co_enable_hook_sys();
    
    stEnv_t* env = (stEnv_t*)args;
    assert(env);

    int idx = env->idx;
    ThreadWorker* w = env->w;
    assert(w);

    LOG_INFO<<"coroutine start "<<w->idx_<<"-"<<idx;

    while (true) {
        //if (env->task_queue->size() <= 0) {   
        if (w->task_queue_size_ <= 0) {
            co_cond_timedwait(w->task_queue_cond_, -1);
            continue;
        }
        w->task_queue_size_--;
        Task* task = w->task_queue_.front();
        w->task_queue_.pop_front();
        //Task* task = nullptr;
        //if(env->task_queue->TryDequeue(task) || task == nullptr) {
        if(task == nullptr) {
            continue;
        }

        long cur = w->coroutines_running_.fetch_add(1);
        LOG_DEBUG<<"worker:"<<w->idx_<<" RECV task "<<task->id()<<" runing_coroutines:"<<(cur+1)<<" queue_len:"<<w->task_queue_size_;

        try {
            task->run();
        } catch(...) {
            // LOG_ERROR
        }
        w->done(task);
    }
    delete env;
    co_yield_ct();
    return nullptr;
}

int ThreadWorker::run(bool detach/*=false*/) {

    worker_thread_.reset(new std::thread(ThreadWorker::runRoutine, (void*)this));
    thread_id_ = worker_thread_->get_id();  // 线程ID

    if(detach) {
        worker_thread_->detach();
    }
    return 0;
}

//static
void ThreadWorker::runRoutine(void* arg) {
    ThreadWorker* w = (ThreadWorker*)arg;
    assert(w);
    w->runRoutineImp();
}

void ThreadWorker::runRoutineImp() {
    task_queue_cond_ = co_cond_alloc();

    if(copy_stack_k_ > 0)
        share_stack_ = co_alloc_sharestack(1, 1024 * copy_stack_k_);
    else
        share_stack_ = nullptr;

    // 协程池
    std::vector<stEnv_t*> coroutine_envs;
    for(int i=0; i<coroutines_; i++) {
        stEnv_t* env = new stEnv_t;
        env->w = this;
        env->idx = i;

        coroutine_envs.push_back(env);

        if(share_stack_) {
            stCoRoutineAttr_t attr;
            attr.stack_size = 0;
            attr.share_stack = share_stack_;
            co_create(&env->consumer_routine, &attr, ThreadWorker::CoConsumer, (void*)env);
        } else {
            co_create(&env->consumer_routine, nullptr, ThreadWorker::CoConsumer, (void*)env);
        }
        co_resume(env->consumer_routine);
    }

    co_eventloop(co_get_epoll_ct(), ThreadWorker::eventLoop, (void*)this);

    // 释放协程资源
    for(int i=0; i<coroutine_envs.size(); i++) {
        stEnv_t* env = coroutine_envs[i];
        co_release(env->consumer_routine);
        delete env;
    }

    //event_free(timer_event_);
    event_free(task_ev_);
    event_free(exit_ev_);
    event_base_free(evbase_);

    LOG_INFO<<"worker:"<<idx_<<" exit!";
}

int ThreadWorker::eventLoop(void* args) {
    ThreadWorker* w = (ThreadWorker*)args;
    assert(w);
    event_base_loop(w->evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    return 0;
}

void ThreadWorker::wait() {
    if(worker_thread_) {
        if(worker_thread_->joinable()) {
            worker_thread_->join();
        }
        worker_thread_.reset();
    }
    LOG_DEBUG<<"wait exit worker="<<thread_id_;
}

//static
//void ThreadWorker::timerCallback(int fd, short kind, void *userp) {
//    // TODO: stats
//}

int ThreadWorker::addTask(Task* task) {
    LOG_DEBUG<<"worker:"<<idx_<<" ADD task "<<task->id();
    int res = write(task_pipes_[1], &task, sizeof(task));   // 写入内存地址
    return res==-1?-1:0;
}

struct stEnvNotify_t {
    ThreadWorker* w;
    Task* task;
    stCoRoutine_t* notify_routine;
};

//static
void ThreadWorker::newTaskCallback(int fd, short events, void* arg) {

    ThreadWorker* w = (ThreadWorker*)arg;
    assert(w);

    Task* task = nullptr;
    if(read(fd, &task, sizeof(task)) != sizeof(task)) {
        LOG_ERROR<<"woker:"<<w->idx_<<" task_fd read error fd="<<fd;
        return;
    }
    assert(task);
    //task_queue_.Enqueue(task);
    w->task_queue_.push_back(task);
    w->task_queue_size_++;
    co_cond_signal(w->task_queue_cond_);   // 协程池直接监控队列，进行处理

    long cur = w->coroutines_running_.fetch_add(0); // TODO: load(..)
    LOG_DEBUG<<"worker:"<<w->idx_<<" RECV task "<<task->id()<<" runing_coroutines:"<<(cur+1);
}

void ThreadWorker::done(Task* task) {
    long cur = coroutines_running_.fetch_sub(1);
    long tasks_done = tasks_done_.fetch_add(1);

    LOG_INFO<<"worker:"<<idx_<<" DONE task "<<task->id()<<" done:"<<(tasks_done+1)<<" runing_coroutines:"<<(cur-1)<<" queue_len:"<<task_queue_size_;

    if(task != nullptr)
        delete task;    // TODO: free
}

void ThreadWorker::stop() {
    char buf[1] = {'-'};
    write(exit_pipes_[1], buf, 1);
}

//static
void ThreadWorker::exitTaskCallback(int fd, short events, void* arg) {
    ThreadWorker* w = (ThreadWorker*)arg;
    assert(w);

    char buf[1];
    if(read(fd, buf, 1) != 1) {
        LOG_ERROR<<"woker:"<<w->idx_<<" exit_fd read error fd="<<fd;
        return;
    }
    w->stopLoop();
}
void ThreadWorker::stopLoop() {
    if(evbase_) {
        event_base_loopexit(evbase_, NULL);
        //event_base_loopbreak(evbase_);
    }
}

int ThreadWorkers::init(int32_t workers, int coroutines, int copy_stack_k) {
    workers_n_ = _MIN_(_MAX_(workers, 1), 100); // between [1, 100]
    for(int i=0; i<workers_n_; i++) {
        assert(workers_[i].init(i, coroutines, copy_stack_k) == 0);
        assert(workers_[i].run() == 0);
    }
    LOG_INFO<<"ThreadWorkers start workers "<<workers_n_;
    return 0;
}
int ThreadWorkers::destroy() {
    for(int i = 0; i < workers_n_; i++) {
        workers_[i].stop();
        workers_[i].wait();
    }
    LOG_INFO<<"ThreadWorkers exit";
    return 0;
}

int ThreadWorkers::addTask(Task* task) {

    if(task == nullptr) return -1;

    int worker_idx = rand()%workers_n_;
    ThreadWorker* w = &workers_[worker_idx];
    assert(w);

    return w->addTask(task);
}

}

