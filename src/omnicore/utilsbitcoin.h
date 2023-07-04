#ifndef BITCOIN_OMNICORE_UTILSBITCOIN_H
#define BITCOIN_OMNICORE_UTILSBITCOIN_H

class CBlockIndex;
class uint256;

#include <stdint.h>
#include <optional>

#include <uint256.h>

class CScript;
class CChainIndex;

namespace mastercore
{
/** Returns the current chain length. */
int GetHeight();
/** Returns the timestamp of the latest block. */
uint32_t GetLatestBlockTime();
/** Returns the active chain. */
const CChainIndex& GetActiveChain();

std::optional<std::pair<unsigned int, uint256>> ScriptToUint(const CScript& scriptPubKey);

bool MainNet();
bool TestNet();
bool RegTest();
bool UnitTest();
bool isNonMainNet();
}

#endif // BITCOIN_OMNICORE_UTILSBITCOIN_H
