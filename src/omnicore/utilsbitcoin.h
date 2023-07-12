#ifndef BITCOIN_OMNICORE_UTILSBITCOIN_H
#define BITCOIN_OMNICORE_UTILSBITCOIN_H

#include <omnicore/validationinterface.h>

#include <stdint.h>
#include <optional>

#include <uint256.h>

class CScript;

namespace mastercore
{
/** Returns the current chain length. */
int GetHeight();
/** Returns the timestamp of the latest block. */
uint32_t GetLatestBlockTime();
/** Used to inform the node is in initial block download. */
bool IsInitialBlockDownload();
/** Returns the active chain. */
const CChainIndex& GetActiveChain();
/** Abort the node. */
void MayAbortNode(const std::string& message);

std::optional<std::pair<unsigned int, uint256>> ScriptToUint(const CScript& scriptPubKey);

bool MainNet();
bool TestNet();
bool RegTest();
bool UnitTest();
bool isNonMainNet();
}

#endif // BITCOIN_OMNICORE_UTILSBITCOIN_H
