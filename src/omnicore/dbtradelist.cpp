
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
using mastercore::pDbTransaction;
using mastercore::Uint256;

CMPTradeList::CMPTradeList(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading trades database: %s\n", status.ToString());
}

CMPTradeList::~CMPTradeList()
{
    if (msc_debug_persistence) PrintToLog("CMPTradeList closed\n");
}

struct CBlockTxKey {
    static constexpr uint8_t prefix = 'b';
    uint32_t block = ~0u;
    uint8_t chash[4];

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, ~block);
        ::Serialize(s, chash);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        block = ~ser_readdata32be(s);
        ::Unserialize(s, chash);
    }
};

struct CTradeMatchKey {
    static constexpr uint8_t prefix = 'm';
    uint256 txid1;
    uint256 txid2;
    std::string address1;
    std::string address2;
    uint32_t prop1 = 0;
    uint32_t prop2 = 0;
    int block = 0;

    SERIALIZE_METHODS(CTradeMatchKey, obj) {
        READWRITE(obj.txid1);
        READWRITE(obj.txid2);
        READWRITE(obj.address1);
        READWRITE(obj.address2);
        READWRITE(VARINT(obj.prop1));
        READWRITE(VARINT(obj.prop2));
        READWRITE(obj.block);
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

void CMPTradeList::recordMatchedTrade(const uint256& txid1, const uint256& txid2, const std::string& address1, const std::string& address2, uint32_t prop1, uint32_t prop2, int64_t amount1, int64_t amount2, int blockNum, int64_t fee)
{
    CBlockTxKey txkey{uint32_t(blockNum)};
    std::copy(txid1.begin(), txid1.begin() + sizeof(txkey.chash), txkey.chash);
    bool status = Write(txkey, "") && Write(CTradeMatchKey{txid1, txid2, address1, address2, prop1, prop2, blockNum}, CTradeMatchValue{amount1, amount2, fee});
    ++nWritten;
    if (msc_debug_tradedb) PrintToLog("%s: %s\n", __func__, status ? "OK" : "NOK");
}

struct CTxTradeKey {
    static constexpr uint8_t prefix = 't';
    uint256 txid;
    std::string address;
    uint32_t propertyIdForSale = 0;
    uint32_t propertyIdDesired = 0;
    int block = 0;
    uint32_t blockIndex = 0;

    SERIALIZE_METHODS(CTxTradeKey, obj) {
        READWRITE(obj.txid);
        READWRITE(obj.address);
        READWRITE(VARINT(obj.propertyIdForSale));
        READWRITE(VARINT(obj.propertyIdDesired));
        READWRITE(obj.block);
        READWRITE(VARINT(obj.blockIndex));
    }
};

void CMPTradeList::recordNewTrade(const uint256& txid, const std::string& address, uint32_t propertyIdForSale, uint32_t propertyIdDesired, int blockNum, int blockIndex)
{
    CBlockTxKey txkey{uint32_t(blockNum)};
    std::copy(txid.begin(), txid.begin() + sizeof(txkey.chash), txkey.chash);
    bool status = Write(txkey, "") && Write(CTxTradeKey{txid, address, propertyIdForSale, propertyIdDesired, blockNum, uint32_t(blockIndex)}, "");
    ++nWritten;
    if (msc_debug_tradedb) PrintToLog("%s: %s\n", __func__, status ? "OK" : "NOK");
}

template<typename T>
uint32_t DeleteToBatch(leveldb::WriteBatch& batch, CDBaseIterator& it, const CBlockTxKey& key)
{
    uint32_t found = 0;
    for (it.Seek(PartialKey<T>(Uint256(key.chash))); it; ++it) {
        if (it.Key<T>().block != key.block) continue;
        ++found;
        batch.Delete(it.Key());
    }
    return found;
}

/**
 * This function deletes records of trades above/equal to a specific block from the trade database.
 *
 * Returns the number of records changed.
 */
int CMPTradeList::deleteAboveBlock(int blockNum)
{
    unsigned int n_found = 0;
    leveldb::WriteBatch batch;
    uint32_t block = blockNum;
    std::vector<std::string> vecSTORecords;
    CDBaseIterator tx_it{NewIterator()};
    for (CDBaseIterator it{NewIterator(), CBlockTxKey{block}}; it; --it) {
        batch.Delete(it.Key());
        auto key = it.Key<CBlockTxKey>();
        n_found += DeleteToBatch<CTxTradeKey>(batch, tx_it, key);
        n_found += DeleteToBatch<CTradeMatchKey>(batch, tx_it, key);
    }
    WriteBatch(batch);
    PrintToLog("%s(%d); tradedb n_found= %d\n", __func__, blockNum, n_found);
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
                svalue = strprintf("%s:%s:%d:%d:%d:%d:%d:%d", key.address1, key.address2, key.prop1, key.prop2, amount1, amount2, key.block, fee);
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
    for (CDBaseIterator it{NewIterator(), CTradeMatchKey{}}; it; ++it) {
        // search key to see if this is a matching trade
        auto key = it.Key<CTradeMatchKey>();
        if (key.txid1 != txid && key.txid2 != txid) continue;
        auto [amount1, amount2, tradingFee] = it.Value<CTradeMatchValue>();
        std::string strAmount1 = FormatMP(key.prop1, amount1);

        // populate trade object and add to the trade array, correcting for orientation of trade
        UniValue trade(UniValue::VOBJ);
        trade.pushKV("txid", txid.ToString());
        trade.pushKV("block", key.block);
        if (auto pBlockIndex = GetActiveChain()[key.block]) {
            trade.pushKV("blocktime", pBlockIndex->GetBlockTime());
        }
        if (key.prop1 == propertyId) {
            trade.pushKV("address", TryEncodeOmniAddress(key.address1));
            trade.pushKV("amountsold", strAmount1);
            trade.pushKV("amountreceived", FormatMP(key.prop2, amount2));
            trade.pushKV("tradingfee", FormatMP(key.prop2, tradingFee));
            totalReceived += amount2;
            totalSold += amount1;
        } else {
            trade.pushKV("address", TryEncodeOmniAddress(key.address2));
            trade.pushKV("amountsold", FormatMP(key.prop2, amount2 + tradingFee));
            trade.pushKV("amountreceived", strAmount1);
            trade.pushKV("tradingfee", FormatMP(key.prop1, 0)); // not the liquidity taker so no fee for this participant - include attribute for standardness
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
    bool propertyIdSideAIsDivisible = isPropertyDivisible(propertyIdSideA);
    bool propertyIdSideBIsDivisible = isPropertyDivisible(propertyIdSideB);
    for (CDBaseIterator it{NewIterator(), CTradeMatchKey{}}; it; ++it) {
        auto key = it.Key<CTradeMatchKey>();
        uint256 sellerTxid, matchingTxid;
        std::string sellerAddress, matchingAddress;
        int64_t amountReceived = 0, amountSold = 0;
        if (key.prop1 == propertyIdSideA && key.prop2 == propertyIdSideB) {
            sellerTxid = key.txid2;
            sellerAddress = key.address2;
            matchingTxid = key.txid1;
            matchingAddress = key.address1;
            auto value = it.Value<CTradeMatchValue>();
            amountSold = value.amount1;
            amountReceived = value.amount2;
        } else if (key.prop2 == propertyIdSideA && key.prop1 == propertyIdSideB) {
            sellerTxid = key.txid1;
            sellerAddress = key.address1;
            matchingTxid = key.txid2;
            matchingAddress = key.address2;
            auto value = it.Value<CTradeMatchValue>();
            amountReceived = value.amount1;
            amountSold = value.amount2;
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
    for (CDBaseIterator it{NewIterator()}; it; ++it) {
        ++count;
    }
    return count;
}
