#ifndef HEADER_INCLUSION_GUARD_GATE_HPP
#define HEADER_INCLUSION_GUARD_GATE_HPP

#include <mutex>               // mutex, unique_lock
#include <condition_variable>  // condition_variable
#include <atomic>              // atomic<>

class Gate {
private:

    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> is_gate_open{ false };

public:

    void open(void)
    {
        is_gate_open = true;
        cv.notify_all();
    }

    void close(void)
    {
        is_gate_open = false;
        cv.notify_all();
    }

    void wait_for_open(void)
    {
        std::unique_lock<std::mutex> lock(m);
        while ( false == is_gate_open ) cv.wait(lock);
    }

    void wait_for_close(void)
    {
        std::unique_lock<std::mutex> lock(m);
        while ( is_gate_open ) cv.wait(lock);
    }
};

#endif
