#ifndef BITCOIN_OMNICORE_COINSCACHE_H
#define BITCOIN_OMNICORE_COINSCACHE_H

#include <omnicore/dbbase.h>

#include <util/hasher.h>

#include <unordered_map>

class CBlockIndex;
class CCoinsViewDB;
class Coin;
class COutPoint;
class CTransaction;

/** LevelDB snapshot based storage for storing coins cache.
 */
class COmniCoinsCache : public CDBBase
{
    CCoinsViewDB& coinsDB;
    const leveldb::Snapshot* snapshot = nullptr;
    std::unordered_map<COutPoint, Coin, SaltedOutpointHasher> cacheCoins;

    /** Release old snapshot and take new one. */
    void UpdateSnapshot();

public:
    COmniCoinsCache(CCoinsViewDB& db, const fs::path& path, bool fWipe);
    virtual ~COmniCoinsCache();

    /** Deletes all entries of the database and the cache. */
    void Clear();

    /** Stores inputs to the database. */
    void AddInputs(const std::vector<CTxIn>& vin);

    /** Stores coins to the cache. */
    void AddCoins(const CTransaction& tx, int block);

    /** Stores coin to the cache. */
    void AddCoin(const COutPoint& outpoint, Coin&& coin);

    /** Deletes coins from the cache. */
    void Uncache(const COutPoint& outpoint);

    /** Returns coin for given outpoint. */
    bool GetCoin(const COutPoint& outpoint, Coin& coin) const;

    /** Block is added to the cache. */
    void BlockCached(const CBlockIndex* pindex);
};

namespace mastercore
{
    //! LevelDB snapshot based storage for storing coins cache.
    extern COmniCoinsCache* pCoinsCache;
}

#endif // BITCOIN_OMNICORE_COINSCACHE_H

