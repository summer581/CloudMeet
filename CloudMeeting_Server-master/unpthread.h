#ifndef UNPTHREAD_H
#define UNPTHREAD_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include "unp.h"

struct Thread {
    std::thread thread;
};

#endif // UNPTHREAD_H
