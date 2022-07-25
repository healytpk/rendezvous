#ifndef HEADER_INCLUSION_GUARD_GATE_HPP
#define HEADER_INCLUSION_GUARD_GATE_HPP

#include <mutex>               // mutex, unique_lock
#include <condition_variable>  // condition_variable

class Gate {
private:

    std::mutex m;
    std::condition_variable cv;
    bool is_gate_open = false;

public:

    void open(void)
    {
        m.lock();
        is_gate_open = true;
        m.unlock();
        cv.notify_one();
    }

    void close(void)
    {
        m.lock();
        is_gate_open = false;
        m.unlock();
        cv.notify_one();
    }

    void wait_for_open(void)
    {
        std::unique_lock<std::mutex> lock(m);
        while ( false == is_gate_open ) cv.wait(lock);
    }

    void wait_for_open_and_then_immediately_close_without_notification(void)
    {
        {
            std::unique_lock<std::mutex> lock(m);
            while ( false == is_gate_open ) cv.wait(lock);
        }

        m.lock();
        is_gate_open = false;
        m.unlock();
    }

    void wait_for_close(void)
    {
        std::unique_lock<std::mutex> lock(m);
        while ( is_gate_open ) cv.wait(lock);
    }
};

#endif
