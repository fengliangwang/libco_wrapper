# libco_wrapper

## libco_wrapper

对libco进行的"多线程 + 协程池"的封装实现

1. 多线程数量在初始化时设置，线程之间通过libevent fd的Task分配(目前实现为随机)，无锁
2. 单线程内协程池数量在初始化时设置，协程池为经典的多消费者模型，通过condition variable在std::list上等待Task（协程之间并行执行为线程安全的，所以采用简单的std::list实现队列）
3. 协程初始化时支持设置采用stackfull或stackless(copying stack, 设置共享stack大小)
4. Task通过实现run()函数自定义操作
5. 外部系统通过addTask接口添加任务，中间过程透明

~~~
    // init(分配4个线程，每个线程100个协程，协程共享stack(大小为128k))
    utils::ThreadWorkers thread_workers;
    thread_workers.init(4, 100, 128);

    // 增加Task任务
    for(int i=0; i<10000000; ++i) {
        Task* task = new Task(i);
        thread_workers.addTask(task);
    }

    // running ....
    sleep(10000);

    
    // destroy
    thread_workers.destroy();
~~~

## libco

libco是微信后台大规模使用的c/c++协程库，通过仅有的几个函数接口 co_create/co_resume/co_yield 再配合 co_poll，可以支持同步或者异步的写法，如线程库一样轻松。同时库里面提供了socket族函数的hook，使得后台逻辑服务几乎不用修改逻辑代码就可以完成异步化改造。

https://github.com/Tencent/libco
