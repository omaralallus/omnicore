
#include <omnicore/convert.h>
#include <omnicore/dbtradelist.h>
#include <omnicore/dbtransaction.h>
#include <omnicore/log.h>
#include <omnicore/mdex.h>
#include <omnicore/script.h>
#include <omnicore/sp.h>
#include <omnicore/utilsbitcoin.h>
#include <omnicore/uint256_extensions.h>

#include <chain.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <fs.h>
#include <reverse_iterator.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <validation.h>
#include <tinyformat.h>

#include <univalue.h>

#include <leveldb/iterator.h>
#include <leveldb/slice.h>
#include <leveldb/status.h>

#include <stddef.h>

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using mastercore::GetActiveChain;
using mastercore::isPropertyDivisible;

CMPTradeList::CMPTradeList(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading trades database: %s\n", status.ToString());
}

CMPTradeList::~CMPTradeList()
{
    if (msc_debug_persistence) PrintToLog("CMPTradeList closed\n");
}

struct CTradeMatchKey {
    static constexpr uint8_t prefix = 'm';
    uint32_t block = ~0u;
    uint256 txid1;
    uint256 txid2;

    SERIALIZE_METHODS(CTradeMatchKey, obj) {
        READWRITE(Using<BigEndian32Inv>(obj.block));
        READWRITE(obj.txid1);
        READWRITE(obj.txid2);
    }
};

struct CTradeMatchValue {
    int64_t amount1;
    int64_t amount2;
    int64_t fee;

    SERIALIZE_METHODS(CTradeMatchValue, obj) {
        READWRITE(obj.amount1);
        READWRITE(obj.amount2);
        READWRITE(obj.fee);
    }
};

void CMPTradeList::recordMatchedTrade(const uint256& txid1, const uint256& txid2, int block, int64_t amount1, int64_t amount2, int64_t fee)
{
    bool status = Write(CTradeMatchKey{uint32_t(block), txid1, txid2}, CTradeMatchValue{amount1, amount2, fee});
    ++nWritten;
    if (msc_debug_tradedb) PrintToLog("%s: %s\n", __func__, status ? "OK" : "NOK");
}

struct CBaseTxKey {
    uint256 txid;

    SERIALIZE_METHODS(CBaseTxKey, obj) {
        READWRITE(obj.txid);
    }
};

struct CTxTradeKey : CBaseTxKey {
    static constexpr uint8_t prefix = 't';
    std::string address;
    uint32_t propertyIdForSale = 0;
    uint32_t propertyIdDesired = 0;
    int block = 0;
    uint32_t blockIndex = 0;

    SERIALIZE_METHODS(CTxTradeKey, obj) {
        READWRITEAS(CBaseTxKey, obj);
        READWRITE(obj.address);
        READWRITE(VARINT(obj.propertyIdForSale));
        READWRITE(VARINT(obj.propertyIdDesired));
        READWRITE(obj.block);
        READWRITE(VARINT(obj.blockIndex));
    }
};

void CMPTradeList::recordNewTrade(const uint256& txid, const std::string& address, uint32_t propertyIdForSale, uint32_t propertyIdDesired, int blockNum, int blockIndex)
{
    bool status = Write(CTxTradeKey{txid, address, propertyIdForSale, propertyIdDesired, blockNum, uint32_t(blockIndex)}, "");
    ++nWritten;
    if (msc_debug_tradedb) PrintToLog("%s: %s\n", __func__, status ? "OK" : "NOK");
}

/**
 * This function deletes records of trades above/equal to a specific block from the trade database.
 *
 * Returns the number of records changed.
 */
int CMPTradeList::deleteTransactions(const std::set<uint256>& txs, int block)
{
    unsigned int n_found = 0;
    leveldb::WriteBatch batch;
    std::vector<std::string> vecSTORecords;
    CDBaseIterator it{NewIterator()};
    for (const auto& txid : txs) {
        auto found = n_found;
        for (it.Seek(PartialKey<CTxTradeKey>(CBaseTxKey{txid})); it; ++it) {
            batch.Delete(it.Key());
            n_found++;
        }
        for (it.Seek(CTradeMatchKey{}); it; ++it) {
            auto key = it.Key<CTradeMatchKey>();
            if (key.block < block) break;
            batch.Delete(it.Key());
            n_found++;
        }
    }
    WriteBatch(batch);
    PrintToLog("%s: tradedb n_found= %d\n", __func__, n_found);
    return n_found;
}

void CMPTradeList::printStats()
{
    PrintToLog("CMPTradeList stats: tWritten= %d , tRead= %d\n", nWritten, nRead);
}

void CMPTradeList::printAll()
{
    int count = 0;
    for (CDBaseIterator it{NewIterator()}; it; ++it) {
        std::string skey, svalue;
        switch(it.Key()[0]) {
            case CTxTradeKey::prefix: {
                auto key = it.Key<CTxTradeKey>();
                skey = key.txid.ToString();
                svalue = strprintf("%s:%d:%d:%d:%d", key.address, key.propertyIdForSale, key.propertyIdDesired, key.block, key.blockIndex);
            } break;
            case CTradeMatchKey::prefix: {
                auto key = it.Key<CTradeMatchKey>();
                skey = strprintf("%s:%s", key.txid1.ToString(), key.txid2.ToString());
                auto [amount1, amount2, fee] = it.Value<CTradeMatchValue>();
                svalue = strprintf("%d:%d:%d", amount1, amount2, fee);
            } break;
            default: continue;
        }
        PrintToConsole("entry #%8d= %s:%s\n", ++count, skey, svalue);
    }
}

bool CMPTradeList::getMatchingTrades(const uint256& txid, uint32_t propertyId, UniValue& tradeArray, int64_t& totalSold, int64_t& totalReceived)
{
    int count = 0;
    totalReceived = totalSold = 0;
    CDBaseIterator tx_it{NewIterator(), PartialKey<CTxTradeKey>(CBaseTxKey{txid})};
    if (!tx_it.Valid()) return false;
    auto tx1key = tx_it.Key<CTxTradeKey>();
    for (CDBaseIterator it{NewIterator(), CTradeMatchKey{}}; it; ++it) {
        // search key to see if this is a matching trade
        auto key = it.Key<CTradeMatchKey>();
        auto mtxid = key.txid1 == txid ? &key.txid2 : key.txid2 == txid ? &key.txid1 : nullptr;
        if (!mtxid) continue;
        auto [amount1, amount2, tradingFee] = it.Value<CTradeMatchValue>();

        tx_it.Seek(PartialKey<CTxTradeKey>(*mtxid));
        if (!tx_it.Valid()) continue;
        auto tx2key = tx_it.Key<CTxTradeKey>();
        const auto& t1key = mtxid == &key.txid2 ? tx1key : tx2key;
        const auto& t2key = mtxid == &key.txid2 ? tx2key : tx1key;
        // populate trade object and add to the trade array, correcting for orientation of trade
        UniValue trade(UniValue::VOBJ);
        trade.pushKV("txid", mtxid->ToString());
        trade.pushKV("block", key.block);
        if (auto pBlockIndex = GetActiveChain()[key.block]) {
            trade.pushKV("blocktime", pBlockIndex->GetBlockTime());
        }
        if (t1key.propertyIdDesired == propertyId) {
            trade.pushKV("address", TryEncodeOmniAddress(t1key.address));
            trade.pushKV("amountsold", FormatMP(t1key.propertyIdDesired, amount1));
            trade.pushKV("amountreceived", FormatMP(t1key.propertyIdForSale, amount2));
            trade.pushKV("tradingfee", FormatMP(t1key.propertyIdForSale, tradingFee));
            totalReceived += amount2;
            totalSold += amount1;
        } else {
            trade.pushKV("address", TryEncodeOmniAddress(t2key.address));
            trade.pushKV("amountsold", FormatMP(t2key.propertyIdDesired, amount2 + tradingFee));
            trade.pushKV("amountreceived", FormatMP(t2key.propertyIdForSale, amount1));
            trade.pushKV("tradingfee", FormatMP(t2key.propertyIdForSale, 0)); // not the liquidity taker so no fee for this participant - include attribute for standardness
            totalReceived += amount1;
            totalSold += amount2;
        }
        tradeArray.push_back(trade);
        ++count;
    }
    return count > 0;
}


// obtains a vector of txids where the supplied address participated in a trade (needed for gettradehistory_MP)
// optional property ID parameter will filter on propertyId transacted if supplied
// sorted by block then index
void CMPTradeList::getTradesForAddress(const std::string& address, std::vector<uint256>& vecTransactions, uint32_t propertyIdFilter)
{
    std::map<std::pair<int, int>, uint256> mapTrades;
    for (CDBaseIterator it{NewIterator(), CTxTradeKey{}}; it; ++it) {
        auto key = it.Key<CTxTradeKey>();
        if (key.address != address) continue;
        if (propertyIdFilter != 0 && propertyIdFilter != key.propertyIdForSale && propertyIdFilter != key.propertyIdDesired) continue;
        mapTrades.emplace(std::make_pair(key.block, key.blockIndex), key.txid);
    }

    for (auto& [_, txid] : mapTrades) {
        vecTransactions.push_back(txid);
    }
}

// obtains an array of matching trades with pricing and volume details for a pair sorted by blocknumber
void CMPTradeList::getTradesForPair(uint32_t propertyIdSideA, uint32_t propertyIdSideB, UniValue& responseArray, uint64_t count)
{
    if (!count) return;
    std::vector<std::pair<int64_t, UniValue> > vecResponse;
    CDBaseIterator tx1_it{NewIterator()}, tx2_it{NewIterator()};
    bool propertyIdSideAIsDivisible = isPropertyDivisible(propertyIdSideA);
    bool propertyIdSideBIsDivisible = isPropertyDivisible(propertyIdSideB);
    for (CDBaseIterator it{NewIterator(), CTradeMatchKey{}}; it; ++it) {
        auto key = it.Key<CTradeMatchKey>();
        tx1_it.Seek(PartialKey<CTxTradeKey>(CBaseTxKey{key.txid1}));
        tx2_it.Seek(PartialKey<CTxTradeKey>(CBaseTxKey{key.txid2}));
        if (!tx1_it || !tx2_it) continue;
        auto tx1_key = tx1_it.Key<CTxTradeKey>();
        auto tx2_key = tx2_it.Key<CTxTradeKey>();
        uint256 sellerTxid, matchingTxid;
        std::string sellerAddress, matchingAddress;
        int64_t amountReceived = 0, amountSold = 0;
        if (tx1_key.propertyIdDesired == propertyIdSideA && tx1_key.propertyIdForSale == propertyIdSideB) {
            sellerTxid = key.txid2;
            sellerAddress = tx2_key.address;
            matchingTxid = key.txid1;
            matchingAddress = tx1_key.address;
            auto value = it.Value<CTradeMatchValue>();
            amountSold = value.amount1;
            amountReceived = value.amount2;
        } else if (tx1_key.propertyIdDesired == propertyIdSideB && tx1_key.propertyIdForSale == propertyIdSideA) {
            sellerTxid = key.txid1;
            sellerAddress = tx1_key.address;
            matchingTxid = key.txid2;
            matchingAddress = tx2_key.address;
            auto value = it.Value<CTradeMatchValue>();
            amountSold = value.amount2;
            amountReceived = value.amount1;
        } else {
            continue;
        }

        rational_t unitPrice(amountReceived, amountSold);
        rational_t inversePrice(amountSold, amountReceived);
        if (propertyIdSideAIsDivisible && !propertyIdSideBIsDivisible) {
            unitPrice *= COIN;
            inversePrice /= COIN;
        }
        if (!propertyIdSideAIsDivisible && propertyIdSideBIsDivisible) {
            unitPrice /= COIN;
            inversePrice *= COIN;
        }
        std::string unitPriceStr = xToString(unitPrice); // TODO: not here!
        std::string inversePriceStr = xToString(inversePrice);

        int64_t blockNum = key.block;

        UniValue trade(UniValue::VOBJ);
        trade.pushKV("block", blockNum);
        if (auto pBlockIndex = GetActiveChain()[blockNum]) {
            trade.pushKV("blocktime", pBlockIndex->GetBlockTime());
        }
        trade.pushKV("unitprice", unitPriceStr);
        trade.pushKV("inverseprice", inversePriceStr);
        trade.pushKV("sellertxid", sellerTxid.GetHex());
        trade.pushKV("selleraddress", TryEncodeOmniAddress(sellerAddress));
        trade.pushKV("amountsold", FormatMP(propertyIdSideA, amountSold));
        trade.pushKV("amountreceived", FormatMP(propertyIdSideB, amountReceived));
        trade.pushKV("matchingtxid", matchingTxid.GetHex());
        trade.pushKV("matchingaddress", TryEncodeOmniAddress(matchingAddress));
        vecResponse.emplace_back(blockNum, trade);
    }

    std::sort(vecResponse.begin(), vecResponse.end(), [](auto& lhs, auto& rhs) {
        return lhs.first > rhs.first;
    });
    if (vecResponse.size() > count) {
        vecResponse.resize(count);
    }
    for (auto& [_, value] : reverse_iterate(vecResponse)) {
        responseArray.push_back(std::move(value));
    }
}

int CMPTradeList::getMPTradeCountTotal()
{
    int count = 0;
    for (CDBaseIterator it{NewIterator(), CTxTradeKey{}}; it; ++it) {
        ++count;
    }
    return count;
}
