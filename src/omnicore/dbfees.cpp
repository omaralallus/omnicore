/**
 * @file dbfees.cpp
 *
 * This file contains code for handling Omni fees.
 */

#include <omnicore/dbfees.h>

#include <omnicore/log.h>
#include <omnicore/rules.h>
#include <omnicore/sp.h>
#include <omnicore/sto.h>

#include <validation.h>

#include <leveldb/db.h>

#include <stdint.h>
#include <limits>
#include <map>
#include <string>
#include <vector>

using namespace mastercore;

COmniFeeCache::COmniFeeCache(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading fee cache database: %s\n", status.ToString());
}

COmniFeeCache::~COmniFeeCache()
{
    if (msc_debug_fees) PrintToLog("COmniFeeCache closed\n");
}

struct CDisTresholdKey {
    static constexpr uint8_t prefix = 'd';
    uint32_t propertyId = 0;

    SERIALIZE_METHODS(CDisTresholdKey, obj) {
        READWRITE(VARINT(obj.propertyId));
    }
};

struct CCacheAmountKey {
    static constexpr uint8_t prefix = 'c';
    uint32_t propertyId = 0;
    uint32_t block = ~0u;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ::Serialize(s, VARINT(propertyId));
        ser_writedata32be(s, ~block);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        ::Unserialize(s, VARINT(propertyId));
        block = ~ser_readdata32be(s);
    }
};

// Returns the distribution threshold for a property
int64_t COmniFeeCache::GetDistributionThreshold(const uint32_t &propertyId)
{
    int64_t distributionThreshold;
    return Read(CDisTresholdKey{propertyId}, distributionThreshold) ? distributionThreshold : 0;
}

// Sets the distribution thresholds to total tokens for a property / OMNI_FEE_THRESHOLD
void COmniFeeCache::UpdateDistributionThresholds(uint32_t propertyId)
{
    int64_t distributionThreshold = getTotalTokens(propertyId) / OMNI_FEE_THRESHOLD;
    if (distributionThreshold <= 0) {
        // protect against zero valued thresholds for low token count properties
        distributionThreshold = 1;
    }
    Write(CDisTresholdKey{propertyId}, distributionThreshold);
}

// Gets the current amount of the fee cache for a property
int64_t COmniFeeCache::GetCachedAmount(const uint32_t &propertyId)
{
    // Get the fee history, set is sorted by block so first entry is most recent
    CDBaseIterator it{NewIterator(), PartialKey<CCacheAmountKey>(propertyId)};
    return it.Valid() ? it.Value<int64_t>() : 0;
}

// Zeros a property in the fee cache
void COmniFeeCache::ClearCache(const uint32_t &propertyId, int block)
{
    if (msc_debug_fees) PrintToLog("ClearCache starting (block %d, property ID %d)...\n", block, propertyId);
    bool status = Delete(CCacheAmountKey{propertyId, uint32_t(block)});
    ++nWritten;

    PruneCache(propertyId, block);

    if (msc_debug_fees) PrintToLog("Cleared cache for property %d block %d [%s]\n", propertyId, block, status ? "OK" : "NOK");
}

// Adds a fee to the cache (eg on a completed trade)
void COmniFeeCache::AddFee(const uint32_t &propertyId, int block, const int64_t &amount)
{
    if (msc_debug_fees) PrintToLog("Starting AddFee for prop %d (block %d amount %d)...\n", propertyId, block, amount);

    // Get current cached fee
    int64_t currentCachedAmount = GetCachedAmount(propertyId);
    if (msc_debug_fees) PrintToLog("   Current cached amount %d\n", currentCachedAmount);

    // Add new fee and rewrite record
    if ((currentCachedAmount > 0) && (amount > std::numeric_limits<int64_t>::max() - currentCachedAmount)) {
        // overflow - there is no way the fee cache should exceed the maximum possible number of tokens, not safe to continue
        const std::string& msg = strprintf("Shutting down due to fee cache overflow (block %d property %d current %d amount %d)\n", block, propertyId, currentCachedAmount, amount);
        PrintToLog(msg);
        if (!gArgs.GetBoolArg("-overrideforcedshutdown", false)) {
            fs::path persistPath = gArgs.GetDataDirNet() / "MP_persist";
            if (fs::exists(persistPath)) fs::remove_all(persistPath); // prevent the node being restarted without a reparse after forced shutdown
            BlockValidationState state;
            AbortNode(state, msg);
        }
    }
    int64_t newCachedAmount = currentCachedAmount + amount;

    if (msc_debug_fees) PrintToLog("   New cached amount %d\n", newCachedAmount);
    bool status = Write(CCacheAmountKey{propertyId, uint32_t(block)}, newCachedAmount);
    ++nWritten;
    if (msc_debug_fees) PrintToLog("AddFee completed for property %d [%s]\n", propertyId, status ? "OK" : "NOK");

    // Call for pruning (we only prune when we update a record)
    PruneCache(propertyId, block);

    // Call for cache evaluation (we only need to do this each time a fee cache is increased)
    EvalCache(propertyId, block);

    return;
}

// Rolls back the cache to an earlier state (eg in event of a reorg) - block is *inclusive* (ie entries=block will get deleted)
void COmniFeeCache::RollBackCache(int block)
{
    leveldb::WriteBatch batch;
    uint32_t startBlock = block;
    CDBaseIterator it{NewIterator()};
    for (uint8_t ecosystem = 1; ecosystem <= 2; ecosystem++) {
        uint32_t startPropertyId = (ecosystem == 1) ? 1 : TEST_ECO_PROPERTY_1;
        auto lastPropertyId = mastercore::pDbSpInfo->peekNextSPID(ecosystem);
        for (uint32_t propertyId = startPropertyId; propertyId < lastPropertyId; propertyId++) {
            for (it.Seek(CCacheAmountKey{propertyId, startBlock}); it; --it) {
                auto key = it.Key<CCacheAmountKey>();
                if (key.propertyId != propertyId) break;
                batch.Delete(it.Key());
                PrintToLog("Rolling back fee cache for property %d [%s])\n", propertyId);
            }
        }
    }
    WriteBatch(batch);
}

// Evaluates fee caches for the property against threshold and executes distribution if threshold met
void COmniFeeCache::EvalCache(const uint32_t &propertyId, int block)
{
    if (GetCachedAmount(propertyId) >= GetDistributionThreshold(propertyId)) {
        DistributeCache(propertyId, block);
    }
}

// Performs distribution of fees
void COmniFeeCache::DistributeCache(const uint32_t &propertyId, int block)
{
    LOCK(cs_tally);

    int64_t cachedAmount = GetCachedAmount(propertyId);

    if (cachedAmount == 0) {
        PrintToLog("Aborting fee distribution for property %d, the fee cache is empty!\n", propertyId);
    }

    OwnerAddrType receiversSet;
    if (isTestEcosystemProperty(propertyId)) {
        receiversSet = STO_GetReceivers("FEEDISTRIBUTION", OMNI_PROPERTY_TMSC, cachedAmount);
    } else {
        receiversSet = STO_GetReceivers("FEEDISTRIBUTION", OMNI_PROPERTY_MSC, cachedAmount);
    }

    uint64_t numberOfReceivers = receiversSet.size(); // there will always be addresses holding OMNI, so no need to check size>0
    PrintToLog("Starting fee distribution for property %d to %d recipients...\n", propertyId, numberOfReceivers);

    int64_t sent_so_far = 0;
    std::set<feeHistoryItem> historyItems;
    for (OwnerAddrType::reverse_iterator it = receiversSet.rbegin(); it != receiversSet.rend(); ++it) {
        const std::string& address = it->second;
        int64_t will_really_receive = it->first;
        sent_so_far += will_really_receive;
        if (msc_debug_fees) PrintToLog("  %s receives %d (running total %d of %d)\n", address, will_really_receive, sent_so_far, cachedAmount);
        assert(update_tally_map(address, propertyId, will_really_receive, BALANCE));
        feeHistoryItem recipient(address, will_really_receive);
        historyItems.insert(recipient);
    }

    PrintToLog("Fee distribution completed, distributed %d out of %d\n", sent_so_far, cachedAmount);

    // store the fee distribution
    pDbFeeHistory->RecordFeeDistribution(propertyId, block, sent_so_far, historyItems);

    // final check to ensure the entire fee cache was distributed, then empty the cache
    assert(sent_so_far == cachedAmount);
    ClearCache(propertyId, block);
}

// Prunes entries from the entry for a property
void COmniFeeCache::PruneCache(const uint32_t &propertyId, int block)
{
    if (msc_debug_fees) PrintToLog("Starting PruneCache for prop %d block %d...\n", propertyId, block);

    leveldb::WriteBatch batch;
    uint32_t pruneBlock = block + 1; // prune all lower than
    if (msc_debug_fees) PrintToLog("Removing entries prior to block %d...\n", pruneBlock);
    CDBaseIterator it{NewIterator(), CCacheAmountKey{propertyId, pruneBlock}};
    for (; it; ++it) {
        if (it.Key<CCacheAmountKey>().propertyId != propertyId) break;
        batch.Delete(it.Key());
    }
    if (batch.ApproximateSize() == 0) {
        if (msc_debug_fees) PrintToLog("Ending PruneCache - no matured entries found.\n");
        return;
    }
    bool status = WriteBatch(batch);
    if (msc_debug_fees) PrintToLog("PruneCache completed for property %d [%s]\n", propertyId, status ? "OK" : "NOK");
}

// Show Fee Cache DB statistics
void COmniFeeCache::printStats()
{
    PrintToConsole("COmniFeeCache stats: nWritten= %d , nRead= %d\n", nWritten, nRead);
}

// Show Fee Cache DB records
void COmniFeeCache::printAll()
{
    int count = 0;
    CDBaseIterator it {NewIterator(), CCacheAmountKey{}};
    for(; it; ++it) {
        ++count;
        auto key = it.Key<CCacheAmountKey>();
        PrintToConsole("entry #%8d= %d:%d\n", count, key.propertyId, key.block, it.Value<int64_t>());
    }
}

// Return a set containing fee cache history items
std::set<feeCacheItem> COmniFeeCache::GetCacheHistory(const uint32_t &propertyId)
{
    std::set<feeCacheItem> sCacheHistoryItems;
    CDBaseIterator it {NewIterator(), PartialKey<CCacheAmountKey>(propertyId)};
    for (; it; ++it) {
        auto key = it.Key<CCacheAmountKey>();
        sCacheHistoryItems.emplace(key.block, it.Value<int64_t>());
    }
    return sCacheHistoryItems;
}

COmniFeeHistory::COmniFeeHistory(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading fee history database: %s\n", status.ToString());
}

COmniFeeHistory::~COmniFeeHistory()
{
    if (msc_debug_fees) PrintToLog("COmniFeeHistory closed\n");
}

// Show Fee History DB statistics
void COmniFeeHistory::printStats()
{
    PrintToConsole("COmniFeeHistory stats: nWritten= %d , nRead= %d\n", nWritten, nRead);
}

struct CDistributionKey {
    static constexpr uint8_t prefix = 'd';
    uint32_t id = ~0u;
    uint32_t block = ~0u;
    uint32_t propertyId = 0;
    int64_t total = 0;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, ~id);
        ser_writedata32be(s, ~block);
        ::Serialize(s, VARINT(propertyId));
        ::Serialize(s, total);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        id = ~ser_readdata32be(s);
        block = ~ser_readdata32be(s);
        ::Unserialize(s, VARINT(propertyId));
        ::Unserialize(s, total);
    }
};

struct CDistributionPropertyKey {
    static constexpr uint8_t prefix = 'p';
    uint32_t propertyId = 0;
    uint32_t id = 0;

    SERIALIZE_METHODS(CDistributionPropertyKey, obj) {
        READWRITE(obj.propertyId);
        READWRITE(obj.id);
    }
};

// Show Fee History DB records
void COmniFeeHistory::printAll()
{
    CDBaseIterator it{NewIterator(), CDistributionKey{}};
    for(; it; ++it) {
        std::string svalue;
        for (const auto& rec : it.Value<std::set<feeHistoryItem>>()) {
            svalue += strprintf("[%s=%d]", rec.first, rec.second);
        }
        auto key = it.Key<CDistributionKey>();
        auto skey = strprintf("%d,%d,%d", key.propertyId, key.block, key.total);
        PrintToConsole("entry #%8d= %s-%s\n", key.id, skey, svalue);
    }
}

// Roll back history in event of reorg, block is inclusive
void COmniFeeHistory::RollBackHistory(int block)
{
    leveldb::WriteBatch batch;
    CDBaseIterator it{NewIterator(), CDistributionKey{}};
    for (; it; ++it) {
        auto key = it.Key<CDistributionKey>();
        if (key.block < block) break;
        PrintToLog("%s() deleting from fee history DB: (%d, %d, %d)\n", __FUNCTION__, key.id, key.block, key.propertyId);
        batch.Delete(it.Key());
        batch.Delete(KeyToString(CDistributionPropertyKey{key.propertyId, key.id}));
    }
    WriteBatch(batch);
}

// Retrieve fee distributions for a property
std::set<int> COmniFeeHistory::GetDistributionsForProperty(const uint32_t &propertyId)
{
    std::set<int> sDistributions;
    CDBaseIterator it{NewIterator(), PartialKey<CDistributionPropertyKey>(propertyId)};
    for (; it; ++it) {
        auto key = it.Key<CDistributionPropertyKey>();
        sDistributions.insert(key.id);
    }
    return sDistributions;
}

// Populate data about a fee distribution
bool COmniFeeHistory::GetDistributionData(int id, uint32_t *propertyId, int *block, int64_t *total)
{
    CDBaseIterator it{NewIterator(), PartialKey<CDistributionKey>(uint32_t(id))};
    if (!it) return {};
    auto key = it.Key<CDistributionKey>();
    *block = key.block;
    *propertyId = key.propertyId;
    *total = key.total;
    return true;
}

// Retrieve the recipients for a fee distribution
std::set<feeHistoryItem> COmniFeeHistory::GetFeeDistribution(int id)
{
    CDBaseIterator it{NewIterator(), PartialKey<CDistributionKey>(uint32_t(id))};
    if (!it) return {};
    auto key = it.Key<CDistributionKey>();
    return it.Value<std::set<feeHistoryItem>>();
}

// Record a fee distribution
void COmniFeeHistory::RecordFeeDistribution(const uint32_t &propertyId, int block, int64_t total, const std::set<feeHistoryItem>& feeRecipients)
{
    uint32_t id = 1;
    CDBaseIterator it{NewIterator(), CDistributionKey{}};
    if (it) {
        id = it.Value<CDistributionKey>().id + 1;
    }
    bool status = Write(CDistributionPropertyKey{propertyId, id}, "")
        && Write(CDistributionKey{id, uint32_t(block), propertyId, total}, feeRecipients);
    if (msc_debug_fees) PrintToLog("Added fee distribution to feeCacheHistory - key=%d, property=%d, block=%d, total=%d, [%s]\n", id, propertyId, block, total, status ? "OK" : "NOK");
}
