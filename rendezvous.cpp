#include <cassert>             // assert
#include <mutex>               // mutex, unique_lock
#include <condition_variable>  // condition_variable
#include <atomic>              // atomic<unsigned>
#include <semaphore>           // counting_semaphore

#ifndef NDEBUG
#   include <thread> // this_thread::get_id()
#endif

class Distribute_Workload_And_Rendezvous_Later final {
private:

#   ifndef NDEBUG
        std::thread::id const id_main_thread;
#   endif

    unsigned const how_many_worker_threads;

    // A bitmask is used to keep track of which threads have started
    // and which have finished. For example, the following two bitmasks
    // indicate that the 1st, 3rd and 4th thread have started, but only
    // the 3rd has finished:
    //     bitmask_started  = 1101
    //     bitmask_finished = 0100 
    std::atomic<unsigned> bitmask_started{0u}, bitmask_finished{0u};

    inline bool Are_All_Workers_Finished(void) const noexcept
    {
        // For example, if we have 5 worker threads, then:
        //     Step 1: Shift 1 by 5 :       1u << 5u  ==  0b100000
        //     Step 2: Subtract 1   : 0b100000  - 1u  ==   0b11111
        //
        // We know all threads are finished when all bits are set
        assert( bitmask_finished < (1u << how_many_worker_threads) );
        return ((1u << how_many_worker_threads) - 1u) == bitmask_finished;
    }

    std::condition_variable       cv_for_main_thread_waiting;
    std::mutex                    mutex_for_cv;
    std::counting_semaphore<16u>  sem{0u};

public:

    Distribute_Workload_And_Rendezvous_Later(unsigned const arg) : how_many_worker_threads(arg)
#   ifndef NDEBUG
        , id_main_thread(std::this_thread::get_id())
#   endif
    {
        assert(how_many_worker_threads <= 16u); // because of the template
                                                // parameter given to
                                                // std::counting_semaphore
    }

    void Distribute_Workload(void)
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        // The two 'assert' statements on the next lines make sure of
        // two things:
        // (1) The threads that have started == The threads that have finished
        // (2) Either no threads have finished, or all threads have finished
        assert( bitmask_started == bitmask_finished );
        assert( (bitmask_started == 0u) || (bitmask_started == ((1u << how_many_worker_threads) - 1u)) );

        bitmask_started  = 0u;
        bitmask_finished = 0u;
        sem.release(how_many_worker_threads);
    }

    void Rendezvous(void)
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        std::unique_lock<std::mutex> lock(mutex_for_cv);

        while ( false == Are_All_Workers_Finished() )
        {
            cv_for_main_thread_waiting.wait(lock);
        }
    }

    void Worker_Thread_Wait_For_Work(unsigned const thread_id)
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

        // The following 'while' loop accommodates the circumstances in
        // which one thread would acquire the semaphore two times for the
        // same workload. This won't happen if each thread takes
        // milliseconds to execute, and if the thread scheduler takes
        // only microseconds to start another thread going again.
        while ( 0u != (bitmask_started & (1u << thread_id) ) )
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1u));
        }

        sem.acquire();

        bitmask_started |= (1u << thread_id);
        assert( 0u == (bitmask_finished & (1u << thread_id)) );
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id)
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );
        assert( 0u != (bitmask_started  & (1u << thread_id) ) );
        assert( 0u == (bitmask_finished & (1u << thread_id) ) );

        bitmask_finished |= (1u << thread_id);

        if ( Are_All_Workers_Finished() )
        {
            // All workers are now finished, so notify main thread
            cv_for_main_thread_waiting.notify_one();
        }
    }

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    Distribute_Workload_And_Rendezvous_Later(void)                                              = delete;
    Distribute_Workload_And_Rendezvous_Later(Distribute_Workload_And_Rendezvous_Later const & ) = delete;
    Distribute_Workload_And_Rendezvous_Later(Distribute_Workload_And_Rendezvous_Later       &&) = delete;

    // Delete the 2 assignment operators
    Distribute_Workload_And_Rendezvous_Later &operator=(Distribute_Workload_And_Rendezvous_Later const & ) = delete;
    Distribute_Workload_And_Rendezvous_Later &operator=(Distribute_Workload_And_Rendezvous_Later       &&) = delete;
};

/* ======================= TEST CODE IS BELOW THIS LINE ============= */

#include <iostream>  // cout, endl
using std::cout;
using std::endl;

Distribute_Workload_And_Rendezvous_Later sar(3u);  // Three worker threads

void Thread_Entry_Point_A(void)
{
    for (; /* ever */ ;)
    {
        sar.Worker_Thread_Wait_For_Work(0u);

        cout << "A\n";

        sar.Worker_Thread_Report_Work_Finished(0u);
    }
}

void Thread_Entry_Point_B(void)
{
    for (; /* ever */ ;)
    {
        sar.Worker_Thread_Wait_For_Work(1u);

        cout << "B\n";

        sar.Worker_Thread_Report_Work_Finished(1u);
    }
}

void Thread_Entry_Point_C(void)
{
    for (; /* ever */ ;)
    {
        sar.Worker_Thread_Wait_For_Work(2u);

        cout << "C\n";

        sar.Worker_Thread_Report_Work_Finished(2u);
    }
}

int main(void)
{
    std::thread tA(Thread_Entry_Point_A), tB(Thread_Entry_Point_B), tC(Thread_Entry_Point_C);

    for (; /* ever */ ;)
    {
        /* ====== We start off with just one thread working ========= */
        cout << "========================================= START\n";

        /* ====== Next we have 4 threads working in parallel ====== */
        sar.Distribute_Workload();

        cout << "D\n";

        /* ====== Next we go back to just one thread working ======== */
        sar.Rendezvous();

        cout << "========================================= FINISH\n";

        std::this_thread::sleep_for( std::chrono::milliseconds(500u) );
    }
}
