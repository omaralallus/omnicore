
#include <chain.h>
#include <primitives/transaction.h>
#include <txdb.h>

#include <omnicore/coinscache.h>
#include <omnicore/log.h>

COmniCoinsCache::COmniCoinsCache(CCoinsViewDB& db, const fs::path& path, bool fWipe) : coinsDB(db)
{
    UpdateSnapshot();
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading coins cache database: %s\n", status.ToString());
}

void COmniCoinsCache::UpdateSnapshot()
{
    if (snapshot) {
        coinsDB.m_db->pdb->ReleaseSnapshot(snapshot);
    }
    snapshot = coinsDB.m_db->pdb->GetSnapshot();
}

COmniCoinsCache::~COmniCoinsCache()
{
    // coinsDB is already closed here, snapshot is released
    if (msc_debug_persistence) PrintToLog("COmniCoinsCache closed\n");
}

struct CCoinKey {
    const COutPoint& outpoint;
    static constexpr uint8_t prefix = 'c';
    SERIALIZE_METHODS(CCoinKey, obj) {
        READWRITE(obj.outpoint.hash);
        READWRITE(VARINT(obj.outpoint.n));
    }
};

/** Stores inputs to the database. */
void COmniCoinsCache::AddInputs(const std::vector<CTxIn>& vin)
{
    Coin coin;
    CDBWriteBatch batch;
    for (auto& txin : vin) {
        if (GetCoin(txin.prevout, coin)) {
            batch.Write(CCoinKey{txin.prevout}, coin);
        }
    }
    WriteBatch(batch);
}

/** Stores coins to the cache. */
void COmniCoinsCache::AddCoins(const CTransaction& tx, int block)
{
    for (auto& txin : tx.vin) {
        Uncache(txin.prevout);
    }
    for (auto i = 0u; i < tx.vout.size(); ++i) {
        AddCoin({tx.GetHash(), i}, Coin(tx.vout[i], block, false));
    }
}

/** Stores coin to the cache. */
void COmniCoinsCache::AddCoin(const COutPoint& outpoint, Coin&& coin)
{
    cacheCoins.insert_or_assign(outpoint, std::move(coin));
}

/** Deletes coins from the cache. */
void COmniCoinsCache::Uncache(const COutPoint& outpoint)
{
    cacheCoins.erase(outpoint);
}

struct CCoinEntry {
    const COutPoint& outpoint;
    static constexpr uint8_t DB_COIN{'C'}; // txdb.cpp
    SERIALIZE_METHODS(CCoinEntry, obj) {
        READWRITE(DB_COIN, obj.outpoint.hash);
        READWRITE(VARINT(obj.outpoint.n));
    }
};

/** Returns coin for given outpoint. */
bool COmniCoinsCache::GetCoin(const COutPoint& outpoint, Coin& coin) const
{
    auto it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end() && !it->second.IsSpent()) {
        coin = it->second;
        return true;
    }
    return Read(CCoinKey{outpoint}, coin)
        || coinsDB.m_db->Read(CCoinEntry{outpoint}, coin, snapshot);
}

/** Block is added to the cache. */
void COmniCoinsCache::BlockCached(const CBlockIndex* pindex)
{
    if (pindex->GetBlockHash() == coinsDB.GetBestBlock()) {
        UpdateSnapshot();
        cacheCoins.clear();
    }
}

/** Deletes all entries of the database and the cache. */
void COmniCoinsCache::Clear()
{
    CDBBase::Clear();
    UpdateSnapshot();
    cacheCoins.clear();
}
