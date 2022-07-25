#ifndef HEADER_INCLUSION_GUARD_RENDEZVOUS_HPP
#define HEADER_INCLUSION_GUARD_RENDEZVOUS_HPP

/*
Four Modes
==========
0 = (undefined)
1 = poll atomics
2 = one mutex, one condition variable
3 = two binary_semaphores per working thread
4 = one Gate per thread (i.e. one mutex and one conditional variable per thread)
*/

#ifdef RENDEZVOUS_DEBUG
    static bool constexpr debug_rendezvous = true;
#else
    static bool constexpr debug_rendezvous = false;
#endif

#include <cassert>             // assert
#ifndef NDEBUG
#   include <thread>  // this_thread::get_id()
#endif

class IRendezvous {
public:

    virtual char const *Which_Derived_Class(void) const noexcept = 0;

protected:

    unsigned const how_many_worker_threads;

#ifndef NDEBUG
    std::thread::id const id_main_thread;
#endif

    IRendezvous(unsigned const arg) noexcept
      : how_many_worker_threads(arg)
#ifndef NDEBUG
        , id_main_thread(std::this_thread::get_id())
#endif
    {
        assert(    (how_many_worker_threads >=  2u)
                && (how_many_worker_threads <= 16u) );
    }

public:

    // Three methods to be invoked by Main Thread
    virtual void Distribute_Workload(void) = 0;
    virtual void Rendezvous(void) = 0;
    virtual void Finish(void) = 0;

    // Two methods to be invoked by worker threads
    virtual bool Worker_Thread_Wait_For_Work(unsigned thread_id) = 0;
    virtual void Worker_Thread_Report_Work_Finished(unsigned thread_id) = 0;

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    IRendezvous(void)                 = delete;
    IRendezvous(IRendezvous const & ) = delete;
    IRendezvous(IRendezvous       &&) = delete;

    // Delete the 2 assignment operators
    IRendezvous &operator=(IRendezvous const & ) = delete;
    IRendezvous &operator=(IRendezvous       &&) = delete;
};

#include <string>

static inline std::string MakeStr(char const *const a, unsigned const i, char const *const b)
{
    std::string retval = a;
    retval += std::to_string(i);
    retval += b;
    return retval;
}

#include <cstddef>             // size_t
#include <mutex>               // mutex, unique_lock
#include <condition_variable>  // condition_variable
#include <semaphore>           // counting_semaphore, binary_semaphore
#include <atomic>              // atomic<>

#include <iostream>   // REMOVE THIS -- Just for debugging =========================================================================================
#include <string>     // REMOVE THIS -- Just for debugging =========================================================================================

class Rendezvous_Poll_Atomic final : public IRendezvous {
public:

    char const *Which_Derived_Class(void) const noexcept override { return "Rendezvous_Poll_Atomic"; }

private:

    std::atomic<bool> should_finish{ false };

    // A bitmask is used to keep track of which threads have started
    // and which have finished. For example, the following two bitmasks
    // indicate that the 1st, 3rd and 4th thread have started, but only
    // the 3rd has finished:
    //     bitmask_started  = 1101
    //     bitmask_finished = 0100
    std::atomic<unsigned> bitmask_started { static_cast<unsigned>(-1) },
                          bitmask_finished{ static_cast<unsigned>(-1) };

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

public:

    Rendezvous_Poll_Atomic(unsigned const arg) noexcept : IRendezvous(arg) {}

    void Distribute_Workload(void) override
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

        bitmask_finished = 0u;
        bitmask_started  = 0u;  // This is the line that starts the spinners going again
    }

    void Rendezvous(void) override
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        while ( false == Are_All_Workers_Finished() );

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Rendezvous\n";
    }

    void Finish(void) override
    {
        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Finish (this will invoke Distribute_Workload)\n";

        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        should_finish = true;
        Distribute_Workload();
    }

    bool Worker_Thread_Wait_For_Work(unsigned const thread_id) override
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

        while ( 0u != (bitmask_started & (1u << thread_id)) );

        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, should_finish ? ": Shutting down\n" : ": Received work\n");

        assert( 0u == (bitmask_started & (1u << thread_id) ) );

        bitmask_started |= (1u << thread_id);
        assert( 0u == (bitmask_finished & (1u << thread_id)) );

        return false == should_finish;
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id) override
    {
        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, ": Reporting its own work done\n");

        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );
        assert( 0u != (bitmask_started  & (1u << thread_id) ) );
        assert( 0u == (bitmask_finished & (1u << thread_id) ) );

        if ( (bitmask_finished | (1u << thread_id)) == ((1u << how_many_worker_threads) - 1u) )
        {
            if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, ": - - - REPORTING ALL WORK DONE - - -\n");
        }

        bitmask_finished |= (1u << thread_id);  // For the last thread to finish, this will unspin the main thread
    }

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    Rendezvous_Poll_Atomic(void)                            = delete;
    Rendezvous_Poll_Atomic(Rendezvous_Poll_Atomic const & ) = delete;
    Rendezvous_Poll_Atomic(Rendezvous_Poll_Atomic       &&) = delete;

    // Delete the 2 assignment operators
    Rendezvous_Poll_Atomic &operator=(Rendezvous_Poll_Atomic const & ) = delete;
    Rendezvous_Poll_Atomic &operator=(Rendezvous_Poll_Atomic       &&) = delete;
};

#include "gate.hpp"  // Gate

class Rendezvous_One_Condition_Variable final : public IRendezvous {
public:

    char const *Which_Derived_Class(void) const noexcept override { return "Rendezvous_One_Condition_Variable"; }

private:

    std::atomic<bool> should_finish{ false };

    // A bitmask is used to keep track of which threads have started
    // and which have finished. For example, the following two bitmasks
    // indicate that the 1st, 3rd and 4th thread have started, but only
    // the 3rd has finished:
    //     bitmask_started  = 1101
    //     bitmask_finished = 0100
    std::atomic<unsigned> bitmask_started { static_cast<unsigned>(-1) },
                          bitmask_finished{ static_cast<unsigned>(-1) };

    Gate gate;
    std::counting_semaphore<16u>  sem{0u};

public:

    Rendezvous_One_Condition_Variable(unsigned const arg) noexcept : IRendezvous(arg) {}

    void Distribute_Workload(void) override
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

        bitmask_finished = 0u;
        bitmask_started  = 0u;  // This is the line that starts the spinners going again

        sem.release(how_many_worker_threads);
    }

    void Rendezvous(void) override
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        gate.wait_for_open_and_then_immediately_close_without_notification();

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Rendezvous\n";
    }

    void Finish(void) override
    {
        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Finish (this will invoke Distribute_Workload)\n";

        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        should_finish = true;
        Distribute_Workload();
    }

    bool Worker_Thread_Wait_For_Work(unsigned const thread_id) override
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

        for (; /* ever */ ;)
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
                std::cerr << MakeStr("= = = = = = = = = = = = The same worker thread (", thread_id,
                                     ") acquired a semaphore more"
                                    " than once for the same workload. Now releasing"
                                    " and sleeping for 1 millisecond = = = = = = = = = = = =\n");

                std::this_thread::sleep_for(std::chrono::milliseconds(1u));
            }
        }

        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, should_finish ? ": Shutting down\n" : ": Received work\n");

        assert( 0u == (bitmask_started & (1u << thread_id) ) );

        bitmask_started |= (1u << thread_id);
        assert( 0u == (bitmask_finished & (1u << thread_id)) );

        return false == should_finish;
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id) override
    {
        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, ": Reporting its own work done\n");

        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );
        assert( 0u != (bitmask_started  & (1u << thread_id) ) );
        assert( 0u == (bitmask_finished & (1u << thread_id) ) );

        if ( (bitmask_finished | (1u << thread_id)) == ((1u << how_many_worker_threads) - 1u) )
        {
            if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, ": - - - REPORTING ALL WORK DONE - - -\n");
        }

        unsigned const tmp = bitmask_finished.fetch_or(1u << thread_id);

        if ( tmp == ( ((1u << how_many_worker_threads) - 1u) - (1u << thread_id) ) )
        {
            gate.open();
        }
    }

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    Rendezvous_One_Condition_Variable(void)                                       = delete;
    Rendezvous_One_Condition_Variable(Rendezvous_One_Condition_Variable const & ) = delete;
    Rendezvous_One_Condition_Variable(Rendezvous_One_Condition_Variable       &&) = delete;

    // Delete the 2 assignment operators
    Rendezvous_One_Condition_Variable &operator=(Rendezvous_One_Condition_Variable const & ) = delete;
    Rendezvous_One_Condition_Variable &operator=(Rendezvous_One_Condition_Variable       &&) = delete;
};

class Rendezvous_32_Semaphores final : public IRendezvous {
public:

    char const *Which_Derived_Class(void) const noexcept override { return "Rendezvous_32_Semaphores"; }

private:

    std::atomic<bool> should_finish{ false };

    std::binary_semaphore sems_start [16u],
                          sems_finish[16u];

    typedef std::binary_semaphore bs;

public:

    Rendezvous_32_Semaphores(unsigned const arg) noexcept
      : IRendezvous(arg),
        sems_start {bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u)},
        sems_finish{bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u),bs(0u)}
    {
        /* Nothing to do in here */
    }

    void Distribute_Workload(void) override
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

    void Rendezvous(void) override
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

    void Finish(void) override
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Finish (this will invoke Distribute_Workload)\n";

        should_finish = true;
        Distribute_Workload();
    }

    bool Worker_Thread_Wait_For_Work(unsigned const thread_id) override
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

        sems_start[thread_id].acquire();

        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, should_finish ? ": Shutting down\n" : ": Received work\n");

        return false == should_finish;
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id) override
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, ": Reporting its own work done\n");

        sems_finish[thread_id].release();
    }

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    Rendezvous_32_Semaphores(void)                              = delete;
    Rendezvous_32_Semaphores(Rendezvous_32_Semaphores const & ) = delete;
    Rendezvous_32_Semaphores(Rendezvous_32_Semaphores       &&) = delete;

    // Delete the 2 assignment operators
    Rendezvous_32_Semaphores &operator=(Rendezvous_32_Semaphores const & ) = delete;
    Rendezvous_32_Semaphores &operator=(Rendezvous_32_Semaphores       &&) = delete;
};

class Rendezvous_16_Gates final : public IRendezvous {
public:

    char const *Which_Derived_Class(void) const noexcept override { return "Rendezvous_16_Gates"; }

private:

    std::atomic<bool> should_finish{ false };

    Gate gates[16u];

    typedef std::binary_semaphore bs;

public:

    Rendezvous_16_Gates(unsigned const arg) noexcept : IRendezvous(arg) {}

    void Distribute_Workload(void) override
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Distribute_Workload\n";

        for ( unsigned i = 0u; i != how_many_worker_threads; ++i )
        {
            gates[i].open();
        }
    }

    void Rendezvous(void) override
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        for ( unsigned i = 0u; i != how_many_worker_threads; ++i )
        {
            gates[i].wait_for_close();
        }

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Rendezvous\n";
    }

    void Finish(void) override
    {
        // The 'assert' on the next line makes sure that this method is
        // only invoked from the main thread.
        assert( std::this_thread::get_id() == id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << "Main thread: Finish (this will invoke Distribute_Workload)\n";

        should_finish = true;
        Distribute_Workload();
    }

    bool Worker_Thread_Wait_For_Work(unsigned const thread_id) override
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        assert( thread_id < how_many_worker_threads );

        gates[thread_id].wait_for_open();

        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, should_finish ? ": Shutting down\n" : ": Received work\n");

        return false == should_finish;
    }

    void Worker_Thread_Report_Work_Finished(unsigned const thread_id) override
    {
        // The 'assert' on the next line makes sure that this method is
        // NOT invoked from the main thread.
        assert( std::this_thread::get_id() != id_main_thread );

        if constexpr ( debug_rendezvous ) std::cerr << MakeStr("Worker thread ", thread_id, ": Reporting its own work done\n");

        gates[thread_id].close();
    }

    // Delete the 3 constructors: no parameters, copy-construct, move-construct
    Rendezvous_16_Gates(void)                         = delete;
    Rendezvous_16_Gates(Rendezvous_16_Gates const & ) = delete;
    Rendezvous_16_Gates(Rendezvous_16_Gates       &&) = delete;

    // Delete the 2 assignment operators
    Rendezvous_16_Gates &operator=(Rendezvous_16_Gates const & ) = delete;
    Rendezvous_16_Gates &operator=(Rendezvous_16_Gates       &&) = delete;
};

#include <type_traits>  // conditional

template<unsigned mode>
using Rendezvous =
  std::conditional_t<
    1u == mode,
    Rendezvous_Poll_Atomic,
    std::conditional_t<2u == mode,
      Rendezvous_One_Condition_Variable,
      std::conditional_t<3u == mode, Rendezvous_32_Semaphores,Rendezvous_16_Gates> > >;

#endif  // header inclusion guards
