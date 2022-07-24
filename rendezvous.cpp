#include "rendezvous.hpp"

#include <iostream>  // cout, endl
using std::cout;
using std::endl;

#include <algorithm>  // max
using std::max;

alignas(
  max({
    alignof(Rendezvous<1u>),
    alignof(Rendezvous<2u>),
    alignof(Rendezvous<3u>),
    alignof(Rendezvous<4u>)
  })
)
char unsigned buf[ max({
    sizeof(Rendezvous<1u>),
    sizeof(Rendezvous<2u>),
    sizeof(Rendezvous<3u>),
    sizeof(Rendezvous<4u>)
}) ];

#include <new>  // launder
IRendezvous &sar = *std::launder(static_cast<IRendezvous*>(static_cast<void*>(buf + 0u)));

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
    std::this_thread::sleep_for( std::chrono::milliseconds(500u) );

    for ( unsigned j = 1u; j <= 4u; ++j )
    {
        switch ( j )
        {
        case 1u: ::new(buf) Rendezvous<1u>(3u); break;
        case 2u: ::new(buf) Rendezvous<2u>(3u); break;
        case 3u: ::new(buf) Rendezvous<3u>(3u); break;
        case 4u: ::new(buf) Rendezvous<4u>(3u); break;
        }

        cout << "================== Class: " << sar.Which_Derived_Class() << " ==================\n";

        std::jthread tA(Thread_Entry_Point_A), tB(Thread_Entry_Point_B), tC(Thread_Entry_Point_C);

        for ( unsigned i = 0u; i != 12u; ++i )
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

        switch ( j )
        {
        case 1u: std::launder(static_cast<Rendezvous<1u>*>(static_cast<void*>(&sar)))->~Rendezvous<1u>(); break;
        case 2u: std::launder(static_cast<Rendezvous<2u>*>(static_cast<void*>(&sar)))->~Rendezvous<2u>(); break;
        case 3u: std::launder(static_cast<Rendezvous<3u>*>(static_cast<void*>(&sar)))->~Rendezvous<3u>(); break;
        case 4u: std::launder(static_cast<Rendezvous<4u>*>(static_cast<void*>(&sar)))->~Rendezvous<4u>(); break;
        }
    }
}
