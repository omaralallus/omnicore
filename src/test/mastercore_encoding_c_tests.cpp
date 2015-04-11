#include "omnicore_encoding.h"

#include "mastercore_script.h"

#include "script/script.h"
#include "utilstrencodings.h"

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

// Is resetted to a norm value in each test
extern unsigned nMaxDatacarrierBytes;

BOOST_AUTO_TEST_SUITE(omnicore_encoding_c_tests)

BOOST_AUTO_TEST_CASE(class_c_marker)
{
    // Store initial data carrier size
    unsigned nMaxDatacarrierBytesOriginal = nMaxDatacarrierBytes;

    nMaxDatacarrierBytes = 40; // byte

    std::vector<unsigned char> vchMarker;
    vchMarker.push_back(0x6f); // "o"
    vchMarker.push_back(0x6d); // "m"

    std::vector<unsigned char> vchPayload = ParseHex(
        "00000000000000010000000006dac2c0");

    std::vector<std::pair<CScript, int64_t> > vecOutputs;
    BOOST_CHECK(OmniCore_Encode_ClassC(vchPayload, vecOutputs));

    // One output was created
    BOOST_CHECK_EQUAL(vecOutputs.size(), 1);

    // Extract the embedded data
    std::pair<CScript, int64_t> pairOutput = vecOutputs.front();
    CScript scriptData = pairOutput.first;

    std::vector<std::string> vstrEmbeddedData;
    BOOST_CHECK(GetScriptPushes(scriptData, vstrEmbeddedData));

    BOOST_CHECK_EQUAL(vstrEmbeddedData.size(), 1);
    std::string strEmbeddedData = vstrEmbeddedData.front();
    std::vector<unsigned char> vchEmbeddedData = ParseHex(strEmbeddedData);

    // The embedded data has a size of the payload plus marker
    BOOST_CHECK_EQUAL(
            vchEmbeddedData.size(),
            vchMarker.size() + vchPayload.size());

    // The output script really starts with the marker
    for (size_t n = 0; n < vchMarker.size(); ++n) {
        BOOST_CHECK_EQUAL(vchMarker[n], vchEmbeddedData[n]);
    }

    // The output script really ends with the payload
    std::vector<unsigned char> vchEmbeddedPayload(
        vchEmbeddedData.begin() + vchMarker.size(),
        vchEmbeddedData.end());

    BOOST_CHECK_EQUAL(HexStr(vchEmbeddedPayload), HexStr(vchPayload));

    // Restore original data carrier size settings
    nMaxDatacarrierBytes = nMaxDatacarrierBytesOriginal;
}

BOOST_AUTO_TEST_SUITE_END()
