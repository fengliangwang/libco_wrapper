
#include <unistd.h>

#include "thread_workers_co.h"
#include "task.h"
#include "logging.h"

int main(int argc, char** arv) {

    logging_init("./log/", true);

    utils::ThreadWorkers thread_workers;
    thread_workers.init(4, 100, 128);

    for(int i=0; i<10000000; ++i) {
        Task* task = new Task(i);
        thread_workers.addTask(task);
    }

    sleep(10000);

    thread_workers.destroy();
}
