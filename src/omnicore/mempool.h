#ifndef BITCOIN_OMNICORE_MEMPOOL_H
#define BITCOIN_OMNICORE_MEMPOOL_H

#include <coins.h>
#include <script/standard.h>
#include <uint256.h>

#include <stdint.h>
#include <vector>

class CSpentIndexKey;
class CSpentIndexValue;

namespace mastercore
{

struct CMempoolAddressDelta
{
    int64_t time = 0;
    CAmount amount = 0;
    uint256 prevhash;
    unsigned int prevout = 0;
};

struct CMempoolAddressDeltaKey
{
    unsigned int type = 0;
    uint256 addressBytes;
    uint256 txhash;
    unsigned int index = 0;
    int spending = 0;
};

std::vector<uint256> ClearMempool();
Coin GetMempoolCoin(const uint256& hash, size_t n);
void MempoolQueryHashes(std::vector<uint256>& hashes);
CTransactionRef GetMempoolTransaction(const uint256& hash);
void AddTransactionToMempool(const CTransactionRef& tx, uint64_t);
void RemoveTransactionFromMempool(const CTransactionRef& tx, uint64_t);
bool GetSpentIndexFromMempool(const CSpentIndexKey& key, CSpentIndexValue& value);
bool GetAddressIndexFromMempool(const std::vector<std::pair<uint256, unsigned>>& addresses, std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>>& results);

}

#endif // BITCOIN_OMNICORE_MEMPOOL_H
