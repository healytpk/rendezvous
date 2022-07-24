#define RENDEZVOUS_32_SEMAPHORES
#include "rendezvous.hpp"

#include <iostream>  // cout, endl
using std::cout;
using std::endl;

Distribute_Workload_And_Rendezvous_Later sar(3u);  // Three worker threads

void Thread_Entry_Point_A(void)
{
    for (; /* ever */ ;)
    {
        if ( false == sar.Worker_Thread_Wait_For_Work(0u) ) return;

        cout << "A\n";

        sar.Worker_Thread_Report_Work_Finished(0u);
    }
}

void Thread_Entry_Point_B(void)
{
    for (; /* ever */ ;)
    {
        if ( false == sar.Worker_Thread_Wait_For_Work(1u) ) return;

        cout << "B\n";

        sar.Worker_Thread_Report_Work_Finished(1u);
    }
}

void Thread_Entry_Point_C(void)
{
    for (; /* ever */ ;)
    {
        if ( false == sar.Worker_Thread_Wait_For_Work(2u) ) return;

        cout << "C\n";

        sar.Worker_Thread_Report_Work_Finished(2u);
    }
}

int main(void)
{
    std::thread tA(Thread_Entry_Point_A), tB(Thread_Entry_Point_B), tC(Thread_Entry_Point_C);

    std::this_thread::sleep_for( std::chrono::milliseconds(500u) );

    for (unsigned i = 0u; i != 12u; ++i)
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

    sar.Finish();

    tA.join();
    tB.join();
    tC.join();
}
