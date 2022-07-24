#ifdef RENDEZVOUS_DEBUG
static bool constexpr debug_rendezvous = true;
#else
static bool constexpr debug_rendezvous = false;
#endif

#include <cassert>             // assert
#include <cstddef>             // size_t

#ifdef RENDEZVOUS_JUST_SPIN
#   warning "Rendezvous.hpp is being compiled to just spin on atomics"
    /* Nothing */
#elif defined(RENDEZVOUS_32_SEMAPHORES)
#   warning "Rendezvous.hpp is being compiled to use 32 semaphores"
#   include <semaphore>           // counting_semaphore
#else
#   warning "Rendezvous.hpp is being compiled to use a condition variable"
#   include <mutex>               // mutex, unique_lock
#   include <condition_variable>  // condition_variable
#   include <semaphore>           // counting_semaphore
#endif

#include <atomic>              // atomic<>

#ifndef NDEBUG
#   include <thread>  // this_thread::get_id()
#endif

#include <iostream>   // REMOVE THIS -- Just for debugging =========================================================================================
#include <string>     // REMOVE THIS -- Just for debugging =========================================================================================

#ifndef RENDEZVOUS_32_SEMAPHORES

class Distribute_Workload_And_Rendezvous_Later final {
private:

    std::atomic<bool> should_finish{ false };

    unsigned const how_many_worker_threads;

#ifndef NDEBUG
    std::thread::id const id_main_thread;
#endif

    // A bitmask is used to keep track of which threads have started
    // and which have finished. For example, the following two bitmasks
    // indicate that the 1st, 3rd and 4th thread have started, but only
    // the 3rd has finished:
    //     bitmask_started  = 1101
    //     bitmask_finished = 0100 
    std::atomic<unsigned> bitmask_started{ static_cast<unsigned>(-1) }, bitmask_finished{ static_cast<unsigned>(-1) };

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

#ifdef RENDEZVOUS_JUST_SPIN
    /* Nothing */
#else
    std::condition_variable       cv_for_main_thread_waiting;
    std::mutex                    mutex_for_cv;
    std::counting_semaphore<16u>  sem{0u};
#endif

public:

    Distribute_Workload_And_Rendezvous_Later(unsigned const arg) noexcept
    : how_many_worker_threads(arg)
#ifndef NDEBUG
       , id_main_thread(std::this_thread::get_id())
#endif
    {
        assert(    (how_many_worker_threads >= 2u)
                && (how_many_worker_threads <= 16u) );
        // Max is 16 because of the template parameter
        // given to std::counting_semaphore
    }

    void Distribute_Workload(void)
    {
        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Distribute_Workload\n";

        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        // The two 'assert' statements on the next lines make sure of
        // two things:
        // (1) The threads that have started == The threads that have finished
        // (2) Either no threads have finished, or all threads have finished
        assert( bitmask_started == bitmask_finished );
        assert(    (bitmask_started == static_cast<unsigned>(-1))
                || (bitmask_started == 0u)
                || (bitmask_started == ((1u << how_many_worker_threads) - 1u)) );

        bitmask_started  = 0u;  // This is the line that starts the spinners going again
        bitmask_finished = 0u;

#ifdef RENDEZVOUS_JUST_SPIN
        /* Nothing */
#else
        sem.release(how_many_worker_threads);
#endif
    }

    void Rendezvous(void)
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

#ifdef RENDEZVOUS_JUST_SPIN
        while ( false == Are_All_Workers_Finished() );
#else
        std::unique_lock<std::mutex> lock(mutex_for_cv);

        while ( false == Are_All_Workers_Finished() )
        {
            cv_for_main_thread_waiting.wait(lock);
        }
#endif

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Rendezvous\n";
    }

    void Finish(void)
    {
        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Finish (this will invoke Distribute_Workload)\n";
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        should_finish = true;
        Distribute_Workload();
    }

    inline std::string WorkStr(char const *const a, unsigned i, char const *const b)
    {
        std::string retval = a;
        retval += std::to_string(i);
        retval += b;
        return retval;
    }

    bool Worker_Thread_Wait_For_Work(unsigned const thread_id)
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

#ifdef RENDEZVOUS_JUST_SPIN
        while ( 0u != (bitmask_started & (1u << thread_id)) );
#else
        for (;;)
        {
            //std::cerr << "Acquiring semaphore...\n";
            //std::this_thread::sleep_for(std::chrono::milliseconds(5u));
            sem.acquire();
            //std::cerr << "Semaphore acquired...\n";

            // The following line accommodates the circumstances in
            // which one thread would acquire the semaphore two times for the
            // same workload. This won't happen if each thread takes
            // milliseconds to execute, and if the thread scheduler takes
            // only microseconds to start another thread going again.
            if ( 0u == (bitmask_started & (1u << thread_id) ) ) break;

            sem.release();

            if constexpr ( debug_rendezvous )
            {
                std::cerr << WorkStr("= = = = = = = = = = = = The same worker thread (", thread_id,
                                     ") acquired a semaphore more"
                                    " than once for the same workload. Now releasing"
                                    " and sleeping for 1 millisecond = = = = = = = = = = = =\n");

                std::this_thread::sleep_for(std::chrono::milliseconds(1u));
            }
        }
#endif

        if constexpr ( debug_rendezvous ) std::cerr << WorkStr("Worker thread ", thread_id, should_finish ? ": Shutting down\n" : ": Received work\n");

        assert( 0u == (bitmask_started & (1u << thread_id) ) );

        bitmask_started |= (1u << thread_id);
        assert( 0u == (bitmask_finished & (1u << thread_id)) );

        return false == should_finish;
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id)
    {
        if constexpr ( debug_rendezvous ) std::cerr << WorkStr("Worker thread ", thread_id, ": Reporting its own work done\n");

        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );
        assert( 0u != (bitmask_started  & (1u << thread_id) ) );
        assert( 0u == (bitmask_finished & (1u << thread_id) ) );

        if ( (bitmask_finished | (1u << thread_id)) == ((1u << how_many_worker_threads) - 1u) )
        {
            if constexpr ( debug_rendezvous ) std::cerr << WorkStr("Worker thread ", thread_id, ": - - - REPORTING ALL WORK DONE - - -\n");            
        }

        bitmask_finished |= (1u << thread_id);  // For the last thread to finish, this will unspin the main thread

        if ( Are_All_Workers_Finished() )
        {
#ifdef RENDEZVOUS_JUST_SPIN
            /* Nothing to do */
#else
            // All workers are now finished, so notify main thread
            cv_for_main_thread_waiting.notify_one();
#endif
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

// =====================================================================
#else
// =====================================================================

class Distribute_Workload_And_Rendezvous_Later final {
private:

    std::atomic<bool> should_finish{ false };

    unsigned const how_many_worker_threads;

#ifndef NDEBUG
    std::thread::id const id_main_thread;
#endif

    std::binary_semaphore sems_start [16u],
                          sems_finish[16u];

    typedef std::binary_semaphore bs;

public:

    Distribute_Workload_And_Rendezvous_Later(unsigned const arg) noexcept
    : how_many_worker_threads(arg)
#ifndef NDEBUG
       , id_main_thread(std::this_thread::get_id())
#endif
       , sems_start {bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u)}
       , sems_finish{bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u)}
    {
        assert(    (how_many_worker_threads >= 2u)
                && (how_many_worker_threads <= 16u) );
        // Max is 16 because of the template parameter
        // given to std::counting_semaphore
    }

    void Distribute_Workload(void)
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Distribute_Workload\n";

        for ( unsigned i = 0u; i != how_many_worker_threads; ++i )
        {
            sems_start[i].release();
        }
    }

    void Rendezvous(void)
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        for ( unsigned i = 0u; i != how_many_worker_threads; ++i )
        {
            sems_finish[i].acquire();
        }

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Rendezvous\n";
    }

    void Finish(void)
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Finish (this will invoke Distribute_Workload)\n";

        should_finish = true;
        Distribute_Workload();
    }

    static inline std::string WorkStr(char const *const a, unsigned i, char const *const b)
    {
        std::string retval = a;
        retval += std::to_string(i);
        retval += b;
        return retval;
    }

    bool Worker_Thread_Wait_For_Work(unsigned const thread_id)
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

        sems_start[thread_id].acquire();

        if constexpr ( debug_rendezvous ) std::cerr << WorkStr("Worker thread ", thread_id, should_finish ? ": Shutting down\n" : ": Received work\n");

        return false == should_finish;
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id)
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << WorkStr("Worker thread ", thread_id, ": Reporting its own work done\n");

        sems_finish[thread_id].release();
    }

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    Distribute_Workload_And_Rendezvous_Later(void)                                              = delete;
    Distribute_Workload_And_Rendezvous_Later(Distribute_Workload_And_Rendezvous_Later const & ) = delete;
    Distribute_Workload_And_Rendezvous_Later(Distribute_Workload_And_Rendezvous_Later       &&) = delete;

    // Delete the 2 assignment operators
    Distribute_Workload_And_Rendezvous_Later &operator=(Distribute_Workload_And_Rendezvous_Later const & ) = delete;
    Distribute_Workload_And_Rendezvous_Later &operator=(Distribute_Workload_And_Rendezvous_Later       &&) = delete;
};

#endif
