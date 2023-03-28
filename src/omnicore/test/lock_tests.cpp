#include <random.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <util/time.h>

#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace number
{
    int n = 0;
}

namespace locker
{
    RecursiveMutex cs_number;
}

static void plusOneThread(int nIterations)
{
    for (int i = 0; i < nIterations; ++i) {
        LOCK(locker::cs_number);
        int n = number::n;
        int nSleep = GetRand(10);
        UninterruptibleSleep(std::chrono::milliseconds{nSleep});
        number::n = n + 1;
    }
}

BOOST_FIXTURE_TEST_SUITE(omnicore_lock_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(multithread_locking)
{
    int nThreadsNum = 4;
    int nIterations = 20;

    std::vector<std::thread> threadGroup;
    for (int i = 0; i < nThreadsNum; ++i) {
        threadGroup.emplace_back(std::bind(&plusOneThread, nIterations));
    }

    for (auto& thread : threadGroup) {
        thread.join();
    }
    BOOST_CHECK_EQUAL(number::n, (nThreadsNum * nIterations));
}


BOOST_AUTO_TEST_SUITE_END()
