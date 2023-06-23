/**
 * @file utilsbitcoin.cpp
 *
 * This file contains certain helpers to access information about Bitcoin.
 */

#include <omnicore/omnicore.h>

#include <chain.h>
#include <chainparams.h>
#include <validation.h>
#include <sync.h>

#include <stdint.h>
#include <string>

namespace mastercore
{

std::optional<std::pair<unsigned int, uint256>> ScriptToUint(const CScript& scriptPubKey)
{
    CTxDestination dest;
    if (!ExtractDestination(scriptPubKey, dest)) {
        return {};
    }
    auto bytes = std::visit(DataVisitor{}, dest);
    if (bytes.empty() || bytes.size() > uint256::size()) {
        return {};
    }
    uint256 addressBytes;
    std::copy(bytes.begin(), bytes.end(), addressBytes.begin());
    return std::make_pair(dest.index(), addressBytes);
}

bool MainNet()
{
    return Params().NetworkIDString() == "main";
}

bool TestNet()
{
    return Params().NetworkIDString() == "test";
}

bool RegTest()
{
    return Params().NetworkIDString() == "regtest";
}

bool UnitTest()
{
    return Params().NetworkIDString() == "unittest";
}

bool isNonMainNet()
{
    return !MainNet() && !UnitTest();
}


} // namespace mastercore
