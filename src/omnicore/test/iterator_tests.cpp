
#include <boost/test/tools/old/interface.hpp>
#include <cstdint>
#include <omnicore/dbbase.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

struct CTestDB : public CDBBase
{
    CTestDB(const fs::path& path)
    {
        BOOST_REQUIRE(Open(path, true).ok());
    }
    using CDBBase::Write;
    template<typename T>
    CDBaseIterator CreateIterator(const T& key)
    {
        return CDBaseIterator{NewIterator(), key};
    }
};

struct CTestAscOrder {
    static constexpr uint8_t prefix = 'A';
    uint32_t idx = 0; // asc
    uint32_t i2 = ~0u; // desc

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, idx);
        ser_writedata32be(s, ~i2);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        idx = ser_readdata32be(s);
        i2 = ~ser_readdata32be(s);
    }
};

struct CTestDescOrder {
    static constexpr uint8_t prefix = 'A';
    uint32_t idx = ~0u; // desc
    uint32_t i2 = 0; // asc

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, ~idx);
        ser_writedata32be(s, i2);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        idx = ~ser_readdata32be(s);
        i2 = ser_readdata32be(s);
    }
};

BOOST_FIXTURE_TEST_SUITE(omnicore_iterator_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(iterator_asc_order)
{
    auto testdb = std::make_shared<CTestDB>(gArgs.GetDataDirNet() / "OMNI_testdb");
    testdb->Write(CTestAscOrder{10, 2}, "");
    testdb->Write(CTestAscOrder{6, 3}, "");
    testdb->Write(CTestAscOrder{6, 8}, "");
    testdb->Write(CTestAscOrder{5, 4}, "");
    testdb->Write(CTestAscOrder{1, 2}, "");
    testdb->Write(CTestAscOrder{4, 3}, "");

    int i;
    CTestAscOrder ordered[] = {{1, 2}, {4, 3}, {5, 4}, {6, 8}, {6, 3}, {10, 2}};
    auto it = testdb->CreateIterator(CTestAscOrder{});
    for (i = 0; it; ++it, ++i) {
        auto key = it.Key<CTestAscOrder>();
        BOOST_CHECK_EQUAL(key.idx, ordered[i].idx);
        BOOST_CHECK_EQUAL(key.i2, ordered[i].i2);
    }
    BOOST_REQUIRE_EQUAL(i, 6);
    BOOST_REQUIRE(!it.Valid());

    // get iterator to element equal or greater than
    it = testdb->CreateIterator(CTestAscOrder{3});
    BOOST_REQUIRE(it.Valid());
    auto key = it.Key<CTestAscOrder>();
    BOOST_CHECK_EQUAL(key.idx, 4);
    BOOST_CHECK_EQUAL(key.i2, 3);

    // i2 is in desc order it seek to next
    it = testdb->CreateIterator(CTestAscOrder{6, 0});
    BOOST_REQUIRE(it.Valid());
    key = it.Key<CTestAscOrder>();
    BOOST_CHECK_EQUAL(key.idx, 10);
    BOOST_CHECK_EQUAL(key.i2, 2);

    // parial key iterate over keys start with '6'
    it = testdb->CreateIterator(PartialKey<CTestAscOrder>(6u));
    BOOST_REQUIRE(it.Valid());
    key = it.Key<CTestAscOrder>();
    BOOST_CHECK_EQUAL(key.idx, 6);
    BOOST_CHECK_EQUAL(key.i2, 8);
    ++it;
    BOOST_REQUIRE(it.Valid());
    key = it.Key<CTestAscOrder>();
    BOOST_CHECK_EQUAL(key.idx, 6);
    BOOST_CHECK_EQUAL(key.i2, 3);
    ++it;
    BOOST_REQUIRE(!it.Valid());
}

BOOST_AUTO_TEST_CASE(iterator_desc_order)
{
    auto testdb = std::make_shared<CTestDB>(gArgs.GetDataDirNet() / "OMNI_testdb");
    testdb->Write(CTestDescOrder{2, 5}, "");
    testdb->Write(CTestDescOrder{2, 4}, "");
    testdb->Write(CTestDescOrder{6, 1}, "");
    testdb->Write(CTestDescOrder{5, 3}, "");
    testdb->Write(CTestDescOrder{1, 2}, "");
    testdb->Write(CTestDescOrder{4, 6}, "");

    int i;
    CTestDescOrder ordered[] = {{6, 1}, {5, 3}, {4, 6}, {2, 4}, {2, 5}, {1, 2}};
    auto it = testdb->CreateIterator(CTestDescOrder{});
    for (i = 0; it; ++it, ++i) {
        auto key = it.Key<CTestDescOrder>();
        BOOST_CHECK_EQUAL(key.idx, ordered[i].idx);
        BOOST_CHECK_EQUAL(key.i2, ordered[i].i2);
    }
    BOOST_REQUIRE_EQUAL(i, 6);
    BOOST_REQUIRE(!it.Valid());

    // get iterator to element equal or lower than
    it = testdb->CreateIterator(CTestDescOrder{3});
    BOOST_REQUIRE(it.Valid());
    auto key = it.Key<CTestDescOrder>();
    BOOST_CHECK_EQUAL(key.idx, 2);
    BOOST_CHECK_EQUAL(key.i2, 4);

    // i2 is in asc order it seek to first
    it = testdb->CreateIterator(CTestDescOrder{2, 0});
    BOOST_REQUIRE(it.Valid());
    key = it.Key<CTestDescOrder>();
    BOOST_CHECK_EQUAL(key.idx, 2);
    BOOST_CHECK_EQUAL(key.i2, 4);

    // parial key iterate over keys start with '2'
    it = testdb->CreateIterator(PartialKey<CTestDescOrder>(2u));
    BOOST_REQUIRE(it.Valid());
    key = it.Key<CTestDescOrder>();
    BOOST_CHECK_EQUAL(key.idx, 2);
    BOOST_CHECK_EQUAL(key.i2, 4);
    ++it;
    BOOST_REQUIRE(it.Valid());
    key = it.Key<CTestDescOrder>();
    BOOST_CHECK_EQUAL(key.idx, 2);
    BOOST_CHECK_EQUAL(key.i2, 5);
    ++it;
    BOOST_REQUIRE(!it.Valid());
}

BOOST_AUTO_TEST_SUITE_END()
