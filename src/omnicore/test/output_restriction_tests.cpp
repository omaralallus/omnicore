#include <omnicore/omnicore.h>
#include <omnicore/rules.h>

#include <chainparams.h>
#include <script/standard.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <stdint.h>
#include <limits>

using namespace mastercore;

BOOST_FIXTURE_TEST_SUITE(omnicore_output_restriction_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(input_nonstandard)
{
    BOOST_CHECK(!IsAllowedInputType(TxoutType::NONSTANDARD, 0));
    BOOST_CHECK(!IsAllowedInputType(TxoutType::NONSTANDARD, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(input_pubkey)
{
    BOOST_CHECK(!IsAllowedInputType(TxoutType::PUBKEY, 0));
    BOOST_CHECK(!IsAllowedInputType(TxoutType::PUBKEY, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(input_pubkeyhash)
{
    BOOST_CHECK(IsAllowedInputType(TxoutType::PUBKEYHASH, 0));
    BOOST_CHECK(IsAllowedInputType(TxoutType::PUBKEYHASH, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(input_scripthash)
{
    int P2SH_BLOCK = ConsensusParams().SCRIPTHASH_BLOCK;

    BOOST_CHECK(!IsAllowedInputType(TxoutType::SCRIPTHASH, 0));
    BOOST_CHECK(!IsAllowedInputType(TxoutType::SCRIPTHASH, P2SH_BLOCK-1));
    BOOST_CHECK(IsAllowedInputType(TxoutType::SCRIPTHASH, P2SH_BLOCK));
    BOOST_CHECK(IsAllowedInputType(TxoutType::SCRIPTHASH, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(input_scripthash_testnet)
{
    SelectParams(CBaseChainParams::TESTNET);
    BOOST_CHECK(IsAllowedInputType(TxoutType::SCRIPTHASH, 0));
    BOOST_CHECK(IsAllowedInputType(TxoutType::SCRIPTHASH, std::numeric_limits<int>::max()));
    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_CASE(input_multisig)
{
    BOOST_CHECK(!IsAllowedInputType(TxoutType::MULTISIG, 0));
    BOOST_CHECK(!IsAllowedInputType(TxoutType::MULTISIG, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(input_nulldata)
{
    BOOST_CHECK(!IsAllowedInputType(TxoutType::NULL_DATA, 0));
    BOOST_CHECK(!IsAllowedInputType(TxoutType::NULL_DATA, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_nonstandard)
{
    BOOST_CHECK(!IsAllowedOutputType(TxoutType::NONSTANDARD, 0));
    BOOST_CHECK(!IsAllowedOutputType(TxoutType::NONSTANDARD, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_pubkey)
{
    BOOST_CHECK(!IsAllowedOutputType(TxoutType::PUBKEY, 0));
    BOOST_CHECK(!IsAllowedOutputType(TxoutType::PUBKEY, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_pubkeyhash)
{
    BOOST_CHECK(IsAllowedOutputType(TxoutType::PUBKEYHASH, 0));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::PUBKEYHASH, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_scripthash)
{
    int P2SH_BLOCK = ConsensusParams().SCRIPTHASH_BLOCK;

    BOOST_CHECK(!IsAllowedOutputType(TxoutType::SCRIPTHASH, 0));
    BOOST_CHECK(!IsAllowedOutputType(TxoutType::SCRIPTHASH, P2SH_BLOCK-1));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::SCRIPTHASH, P2SH_BLOCK));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::SCRIPTHASH, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_scripthash_testnet)
{
    SelectParams(CBaseChainParams::TESTNET);
    BOOST_CHECK(IsAllowedOutputType(TxoutType::SCRIPTHASH, 0));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::SCRIPTHASH, std::numeric_limits<int>::max()));
    SelectParams(CBaseChainParams::MAIN);
}

BOOST_AUTO_TEST_CASE(output_multisig)
{
    BOOST_CHECK(IsAllowedOutputType(TxoutType::MULTISIG, 0));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::MULTISIG, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_nulldata)
{
    int OP_RETURN_BLOCK = ConsensusParams().NULLDATA_BLOCK;

    BOOST_CHECK(!IsAllowedOutputType(TxoutType::NULL_DATA, 0));
    BOOST_CHECK(!IsAllowedOutputType(TxoutType::NULL_DATA, OP_RETURN_BLOCK-1));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::NULL_DATA, OP_RETURN_BLOCK));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::NULL_DATA, std::numeric_limits<int>::max()));
}

BOOST_AUTO_TEST_CASE(output_nulldata_testnet)
{
    SelectParams(CBaseChainParams::TESTNET);
    BOOST_CHECK(IsAllowedOutputType(TxoutType::NULL_DATA, 0));
    BOOST_CHECK(IsAllowedOutputType(TxoutType::NULL_DATA, std::numeric_limits<int>::max()));
    SelectParams(CBaseChainParams::MAIN);
}


BOOST_AUTO_TEST_SUITE_END()
