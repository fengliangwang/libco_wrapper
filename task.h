
#pragma once

#include <iostream>

#include "co_routine.h"
#include "logging.h"

struct Task {   // request
public:
    Task() : i_(0) {}
    Task(int i) : i_(i) {}
    int set(int i) { i_ = i; }
    int get() { return i_; }

    const std::string id() {
        return std::to_string(i_);
    }

    void run() {
        co_enable_hook_sys();
        // TODO:
        struct pollfd pf = { 0 };
        pf.fd = -1; 
        poll(&pf, 1, 20);
        LOG_DEBUG<<"Task::run val:"<<get();

    }
private:
    int i_;
};

