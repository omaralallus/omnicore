/**
 * @file utilsbitcoin.cpp
 *
 * This file contains certain helpers to access information about Bitcoin.
 */

#include <omnicore/omnicore.h>
#include <omnicore/validationinterface.h>
#include <omnicore/utilsbitcoin.h>

#include <chain.h>
#include <chainparams.h>
#include <stdexcept>
#include <shutdown.h>
#include <sync.h>

#include <stdint.h>
#include <string>

namespace mastercore
{
static const COmniValidationInterface& EnsureValidationInterface()
{
    if (omniValidationInterface) {
        return *omniValidationInterface;
    }
    throw std::logic_error("COmniValidationInterface isn't initialized");
}

int GetHeight()
{
    return EnsureValidationInterface().LastBlockHeight();
}

const CChainIndex& GetActiveChain()
{
    return EnsureValidationInterface().GetActiveChain();
}

uint32_t GetLatestBlockTime()
{
    return EnsureValidationInterface().LastBlockTime();
}

bool IsInitialBlockDownload()
{
    return EnsureValidationInterface().IsInitialBlockDownload();
}

void MayAbortNode(const std::string& message)
{
    if (!gArgs.GetBoolArg("-overrideforcedshutdown", false)) {
        fs::path persistPath = gArgs.GetDataDirNet() / "MP_persist";
        if (fs::exists(persistPath)) fs::remove_all(persistPath); // prevent the node being restarted without a reparse after forced shutdown
        AbortNode(message);
    }
}

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
