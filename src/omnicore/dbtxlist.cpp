
#include <omnicore/activation.h>
#include <omnicore/convert.h>
#include <omnicore/dbtxlist.h>
#include <omnicore/dbtransaction.h>
#include <omnicore/dex.h>
#include <omnicore/log.h>
#include <omnicore/notifications.h>
#include <omnicore/omnicore.h>
#include <omnicore/tx.h>
#include <omnicore/utilsbitcoin.h>

#include <chain.h>
#include <chainparams.h>
#include <fs.h>
#include <validation.h>
#include <sync.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/system.h>
#include <util/strencodings.h>

#include <leveldb/iterator.h>
#include <leveldb/slice.h>
#include <leveldb/status.h>

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using mastercore::AddAlert;
using mastercore::CheckAlertAuthorization;
using mastercore::CheckExpiredAlerts;
using mastercore::CheckLiveActivations;
using mastercore::DeleteAlerts;
using mastercore::GetActiveChain;
using mastercore::isNonMainNet;
using mastercore::pDbTransaction;

CMPTxList::CMPTxList(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading tx meta-info database: %s\n", status.ToString());
}

CMPTxList::~CMPTxList()
{
    if (msc_debug_persistence) PrintToLog("CMPTxList closed\n");
}

struct CBlockTxKey {
    static constexpr uint8_t prefix = 'b';
    uint32_t block = ~0u;
    uint256 txid;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata32be(s, ~block);
        ::Serialize(s, txid);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        block = ~ser_readdata32be(s);
        ::Unserialize(s, txid);
    }
};

struct CMPTxList::CTxKey {
    static constexpr uint8_t prefix = 't';
    uint256 txid;
    int block = 0;
    uint8_t valid = 0;
    uint32_t type = 0;

    SERIALIZE_METHODS(CTxKey, obj) {
        READWRITE(obj.txid);
        READWRITE(obj.block);
        READWRITE(obj.valid);
        READWRITE(VARINT(obj.type));
    }

    bool operator==(const CTxKey& other) const {
        return txid == other.txid
            && block == other.block
            && valid == other.valid
            && type == other.type;
    }

    bool operator!=(const CTxKey& other) const {
        return !(*this == other);
    }
};

void CMPTxList::recordTX(const uint256 &txid, bool fValid, int nBlock, unsigned int type, uint64_t nValue)
{
    // overwrite detection, we should never be overwriting a tx, as that means we have redone something a second time
    // reorgs delete all txs from levelDB above reorg_chain_height
    int64_t old_value;
    CTxKey old_key, key{txid, nBlock, fValid, type};
    if (getTX(txid, old_key, old_value) && (old_key != key || old_value != nValue)) {
        PrintToLog("LEVELDB TX OVERWRITE DETECTION - %s\n", txid.ToString());
    }

    PrintToLog("%s(%s, valid=%s, block= %d, type= %d, value= %lu)\n",
            __func__, txid.ToString(), fValid ? "YES" : "NO", nBlock, type, nValue);

    Write(CBlockTxKey{uint32_t(nBlock), txid}, "");
    Write(key, nValue);
    ++nWritten;
}

struct CPaymentTxKey {
    static constexpr uint8_t prefix = 'p';
    uint256 txid;
    size_t payments = ~0u;
    int block = 0;
    uint8_t valid = 0;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ::Serialize(s, txid);
        ser_writedata32be(s, ~payments);
        ser_writedata32(s, block);
        ser_writedata8(s, valid);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        ::Unserialize(s, txid);
        payments = ~ser_readdata32be(s);
        block = ser_readdata32(s);
        valid = ser_readdata8(s);
    }
};

struct CPaymentTxValue {
    uint32_t vout;
    std::string buyer;
    std::string seller;
    uint32_t propertyId;
    uint64_t amount;
    uint256 cancelTxId;

    SERIALIZE_METHODS(CPaymentTxValue, obj) {
        READWRITE(VARINT(obj.vout));
        READWRITE(obj.buyer);
        READWRITE(obj.seller);
        READWRITE(VARINT(obj.propertyId));
        READWRITE(obj.amount);
        if constexpr (ser_action.ForRead()) {
            if (!s.empty()) {
                READWRITE(obj.cancelTxId);
            }
        } else {
            if (!obj.cancelTxId.IsNull()) {
                READWRITE(obj.cancelTxId);
            }
        }
    }
};

void CMPTxList::recordPaymentTX(const uint256& txid, bool fValid, int nBlock, unsigned int vout, unsigned int propertyId, uint64_t nValue, const std::string& buyer, const std::string& seller)
{
    // Prep - setup vars
    uint32_t numberOfPayments = 1;

    // Step 1 - Check TXList to see if this cancel TXID exists
    // Step 2a - If doesn't exist leave number of affected txs & ref set to 1
    // Step 2b - If does exist add +1 to existing ref and set this ref as new number of affected
    CDBaseIterator it{NewIterator(), PartialKey<CPaymentTxKey>(txid)};
    if (it.Valid()) {
        numberOfPayments = it.Key<CPaymentTxKey>().payments + 1;
    }

    // Step 3 - Create new/update master record for payment tx in TXList
    Write(CBlockTxKey{uint32_t(nBlock), txid}, "");
    PrintToLog("DEXPAYDEBUG : Writing master record %s(%s, valid=%s, block= %d, number of payments= %d)\n", __func__, txid.ToString(), fValid ? "YES" : "NO", nBlock, numberOfPayments);

    // Step 4 - Write sub-record with payment details
    CPaymentTxKey key{txid, numberOfPayments, nBlock, fValid};
    CPaymentTxValue value{vout, buyer, seller, propertyId, nValue};
    Write(key, value);
    PrintToLog("DEXPAYDEBUG : Writing sub-record %s-%d with value %d:%s:%s:%d:%d\n", txid.ToString(), numberOfPayments, vout, buyer, seller, propertyId, nValue);
}

struct CDexCancelTxKey {
    static constexpr uint8_t prefix = 'c';
    uint256 txid;
    uint32_t affected = ~0u;
    int block = 0;
    uint8_t valid = 0;

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        ::Serialize(s, txid);
        ser_writedata32be(s, ~affected);
        ser_writedata32(s, block);
        ser_writedata8(s, valid);
    }
    template<typename Stream>
    void Unserialize(Stream& s)
    {
        ::Unserialize(s, txid);
        affected = ~ser_readdata32be(s);
        block = ser_readdata32(s);
        valid = ser_readdata8(s);
    }
};

struct CDexCancelTxValue {
    uint32_t propertyId;
    uint64_t amount;

    SERIALIZE_METHODS(CDexCancelTxValue, obj) {
        READWRITE(VARINT(obj.propertyId));
        READWRITE(obj.amount);
    }
};

void CMPTxList::recordMetaDExCancelTX(const uint256& txid, const uint256& txidSub, bool fValid, int nBlock, unsigned int propertyId, uint64_t nValue)
{
    // Prep - setup vars
    uint32_t numerOfAffected = 1;

    // Step 1 - Check TXList to see if this cancel TXID exists
    // Step 2a - If doesn't exist leave number of affected txs & ref set to 1
    // Step 2b - If does exist add +1 to existing ref and set this ref as new number of affected
    CDBaseIterator it{NewIterator(), PartialKey<CDexCancelTxKey>(txid)};
    if (it.Valid()) {
        numerOfAffected = it.Key<CDexCancelTxKey>().affected + 1;
    }

    // Step 3 - Create new/update master record for cancel tx in TXList
    Write(CBlockTxKey{uint32_t(nBlock), txid}, "");
    PrintToLog("METADEXCANCELDEBUG : Writing master record %s(%s, valid=%s, block= %d, number of affected transactions= %d)\n", __func__, txid.ToString(), fValid ? "YES" : "NO", nBlock, numerOfAffected);

    CDBaseIterator pit{NewIterator(), PartialKey<CPaymentTxKey>(txidSub)};
    if (pit.Valid()) {
        auto value = pit.Value<CPaymentTxValue>();
        value.cancelTxId = txid;
        Write(pit.Key<CPaymentTxKey>(), value);
    } else {
        PrintToLog("METADEXCANCELDEBUG %s: Logic error: %s not found\n", __func__, txidSub.ToString());
    }
    // Step 4 - Write sub-record with cancel details
    CDexCancelTxValue value{propertyId, nValue};
    Write(CDexCancelTxKey{txid, numerOfAffected, nBlock, fValid}, value);
    PrintToLog("METADEXCANCELDEBUG : Writing sub-record %d-%d with value %s:%d:%d\n", txid.ToString(), numerOfAffected, txidSub.ToString(), propertyId, nValue);
}

struct CSendAllTxKey {
    static constexpr uint8_t prefix = 's';
    uint256 txid;
    uint32_t num = 0;

    SERIALIZE_METHODS(CSendAllTxKey, obj) {
        READWRITE(obj.txid);
        READWRITE(VARINT(obj.num));
    }
};

struct CSendAllTxValue {
    uint32_t propertyId;
    int64_t amount;

    SERIALIZE_METHODS(CSendAllTxValue, obj) {
        READWRITE(VARINT(obj.propertyId));
        READWRITE(obj.amount);
    }
};

/**
 * Records a "send all" sub record.
 */
void CMPTxList::recordSendAllSubRecord(const uint256& txid, int nBlock, int subRecordNumber, uint32_t propertyId, int64_t nValue)
{
    CSendAllTxKey key{txid, uint32_t(subRecordNumber)};
    bool status = Write(key, CSendAllTxValue{propertyId, nValue});
    Write(CBlockTxKey{uint32_t(nBlock), txid}, "");
    ++nWritten;
    if (msc_debug_txdb) PrintToLog("%s(): store: %s:%d=%d:%d, status: %s\n", __func__, txid.ToString(), subRecordNumber, propertyId, nValue, status ? "OK" : "NOK");
}

uint256 CMPTxList::findMetaDExCancel(const uint256& txid)
{
    CDBaseIterator it{NewIterator(), PartialKey<CPaymentTxKey>(txid)};
    return it.Valid() ? it.Value<CPaymentTxValue>().cancelTxId : uint256();
}

/**
 * Returns the number of sub records.
 */
int CMPTxList::getNumberOfSubRecords(const uint256& txid)
{
    CDBaseIterator it{NewIterator(), PartialKey<CPaymentTxKey>(txid)};
    if (it.Valid()) return it.Value<CPaymentTxKey>().payments;
    it.Seek(PartialKey<CSendAllTxKey>(txid));
    return it.Valid() ? it.Key<CSendAllTxKey>().num : 0;
}

int CMPTxList::getNumberOfMetaDExCancels(const uint256& txid)
{
    CDBaseIterator it{NewIterator(), PartialKey<CDexCancelTxKey>(txid)};
    return it.Valid() ? it.Value<uint32_t>() : 0;
}

bool CMPTxList::getPurchaseDetails(const uint256& txid, int purchaseNumber, std::string* buyer, std::string* seller, uint64_t* vout, uint64_t* propertyId, uint64_t* nValue)
{
    CDBaseIterator it{NewIterator(), PartialKey<CPaymentTxKey>(txid, uint32_t(purchaseNumber))};
    if (it.Valid()) {
        auto value = it.Value<CPaymentTxValue>();
        *vout = value.vout;
        *buyer = value.buyer;
        *seller = value.seller;
        *propertyId = value.propertyId;
        *nValue = value.amount;
        return true;
    }
    return false;
}

/**
 * Retrieves details about a "metadex cancel" record.
 */
bool CMPTxList::getMetaDExCancelDetails(const uint256& txid, int subSend, uint32_t& propertyId, int64_t& amount)
{
    CDexCancelTxValue value;
    if (Read(CDexCancelTxKey{txid, uint32_t(subSend)}, value)) {
        propertyId = value.propertyId;
        amount = value.amount;
        return true;
    }
    return false;
}

/**
 * Retrieves details about a "send all" record.
 */
bool CMPTxList::getSendAllDetails(const uint256& txid, int subSend, uint32_t& propertyId, int64_t& amount)
{
    CSendAllTxValue value;
    if (Read(CSendAllTxKey{txid, uint32_t(subSend)}, value)) {
        propertyId = value.propertyId;
        amount = value.amount;
        return true;
    }
    return false;
}

int CMPTxList::getMPTransactionCountTotal()
{
    int count = 0;
    for (CDBaseIterator it{NewIterator()}; it; ++it) {
        switch(it.Key()[0]) {
            case CTxKey::prefix:
            case CPaymentTxKey::prefix:
            case CDexCancelTxKey::prefix:
            case CSendAllTxKey::prefix:
                ++count;
            break;
        }
    }
    return count;
}

int CMPTxList::getMPTransactionCountBlock(int block)
{
    int count = 0;
    CDBaseIterator it{NewIterator(), PartialKey<CBlockTxKey>(uint32_t(block))};
    for (; it; ++it) ++count;
    return count;
}

/** Returns a list of all Omni transactions in the given block range. */
int CMPTxList::GetOmniTxsInBlockRange(int blockFirst, int blockLast, std::set<uint256>& retTxs)
{
    CDBaseIterator it{NewIterator(), CBlockTxKey{uint32_t(blockLast)}};
    for (; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (key.block < blockFirst) break;
        retTxs.insert(key.txid);
    }
    return retTxs.size();
}

/*
 * Gets the DB version from txlistdb
 *
 * Returns the current version
 */
int CMPTxList::getDBVersion()
{
    uint8_t version;
    bool status = Read("D", version);
    if (msc_debug_txdb) PrintToLog("%s(): dbversion %d status %s\n", __func__, version, status ? "OK" : "NOK");
    return status ? version : 0;
}

/*
 * Sets the DB version for txlistdb
 *
 * Returns the current version after update
 */
int CMPTxList::setDBVersion()
{
    bool status = Write("D", uint8_t(DB_VERSION));
    if (msc_debug_txdb) PrintToLog("%s(): dbversion %d status %s\n", __func__, DB_VERSION, status ? "OK" : "NOK");
    return getDBVersion();
}

struct CNonFugibleKey {
    static constexpr uint8_t prefix = 'n';
    uint256 txid;

    SERIALIZE_METHODS(CNonFugibleKey, obj) {
        READWRITE(obj.txid);
    }
};

std::pair<int64_t,int64_t> CMPTxList::GetNonFungibleGrant(const uint256& txid)
{
    std::pair<int64_t, int64_t> value;
    return Read(CNonFugibleKey{txid}, value) ? value : std::pair<int64_t, int64_t>{0, 0};
}

void CMPTxList::RecordNonFungibleGrant(const uint256& txid, int64_t start, int64_t end)
{
    bool status = Write(CNonFugibleKey{txid}, std::make_pair(start, end));
    PrintToLog("%s(): Writing Non-Fungible Grant range %s:%d-%d (%s)\n", __FUNCTION__, txid.ToString(), start, end, status ? "OK" : "NOK");
}

bool CMPTxList::getTX(const uint256 &txid, CTxKey& key, int64_t& value)
{
    CDBaseIterator it{NewIterator(), PartialKey<CTxKey>(txid)};
    bool status = it.Valid() && (key = it.Key<CTxKey>(), true) && it.Value(value);
    ++nRead;
    return status;
}

// call it like so (variable # of parameters):
// int block = 0;
// ...
// uint64_t nNew = 0;
//
// if (getValidMPTX(txid, &block, &type, &nNew)) // if true -- the TX is a valid MP TX
//
bool CMPTxList::getValidMPTX(const uint256& txid, int* block, unsigned int* type, uint64_t* nAmended)
{
    if (msc_debug_txdb) PrintToLog("%s()\n", __func__);

    CTxKey key;
    int64_t value;
    if (!getTX(txid, key, value)) return false;

    // parse the string returned, find the validity flag/bit & other parameters
    bool validity = key.valid > 0;
    block && (*block = key.block);
    type && (*type = key.type);
    nAmended && (*nAmended = value);
    if (msc_debug_txdb) printStats();
    return validity;
}

std::set<int> CMPTxList::GetSeedBlocks(int startHeight, int endHeight)
{
    std::set<int> setSeedBlocks;
    CDBaseIterator it{NewIterator(), CBlockTxKey{uint32_t(endHeight)}};
    for (; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (key.block < startHeight) break;
        setSeedBlocks.insert(key.block);
    }
    return setSeedBlocks;
}

using activation = std::pair<std::pair<int, uint32_t>, uint256>;

static void ProcessActivations(const std::vector<activation>& activations, std::function<void(CMPTransaction&)> callback)
{
    CCoinsViewCacheOnly view;
    for (const auto& [hidx, hash] : activations) {
        int blockHeight;
        CTransactionRef wtx;
        CMPTransaction mp_obj;

        if (!GetTransaction(hash, wtx, Params().GetConsensus(), blockHeight)) {
            PrintToLog("ERROR: While loading activation transaction %s: tx in levelDB but does not exist.\n", hash.GetHex());
            continue;
        }
        if (0 != ParseTransaction(view, *wtx, hidx.first, 0, mp_obj)) {
            PrintToLog("ERROR: While loading activation transaction %s: failed ParseTransaction.\n", hash.GetHex());
            continue;
        }
        if (!mp_obj.interpret_Transaction()) {
            PrintToLog("ERROR: While loading activation transaction %s: failed interpret_Transaction.\n", hash.GetHex());
            continue;
        }
        callback(mp_obj);
    }
}

void CMPTxList::LoadAlerts(int blockHeight)
{
    std::vector<activation> loadOrder;
    CDBaseIterator tx_it{NewIterator()};
    CDBaseIterator it{NewIterator(), CBlockTxKey{uint32_t(blockHeight)}};
    for (; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (tx_it.Seek(PartialKey<CTxKey>(key.txid)); !tx_it) continue;
        auto txkey = tx_it.Key<CTxKey>();
        if (txkey.type != OMNICORE_MESSAGE_TYPE_ALERT || !txkey.valid) continue; // not a valid alert
        loadOrder.emplace_back(std::make_pair(key.block, 0), key.txid);
    }

    std::sort(loadOrder.begin(), loadOrder.end());

    ProcessActivations(loadOrder, [&](CMPTransaction& mp_obj) {
        if (OMNICORE_MESSAGE_TYPE_ALERT != mp_obj.getType()) {
            PrintToLog("ERROR: While loading alert %s: levelDB type mismatch, not an alert.\n", mp_obj.getHash().GetHex());
            return;
        }
        if (!CheckAlertAuthorization(mp_obj.getSender())) {
            PrintToLog("ERROR: While loading alert %s: sender is not authorized to send alerts.\n", mp_obj.getHash().GetHex());
            return;
        }
        if (mp_obj.getAlertType() == 65535) { // set alert type to FFFF to clear previously sent alerts
            DeleteAlerts(mp_obj.getSender());
        } else {
            AddAlert(mp_obj.getSender(), mp_obj.getAlertType(), mp_obj.getAlertExpiry(), mp_obj.getAlertMessage());
        }
    });

    if (auto pBlockIndex = GetActiveChain()[blockHeight - 1]) {
        CheckExpiredAlerts(blockHeight, pBlockIndex->GetBlockTime());
    }
}

void CMPTxList::LoadActivations(int blockHeight)
{
    PrintToLog("Loading feature activations from levelDB\n");

    std::vector<activation> loadOrder;
    CDBaseIterator tx_it{NewIterator()};
    CDBaseIterator it{NewIterator(), CBlockTxKey{uint32_t(blockHeight)}};
    for (; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (tx_it.Seek(PartialKey<CTxKey>(key.txid)); !tx_it) continue;
        auto txkey = tx_it.Key<CTxKey>();
        if (txkey.type != OMNICORE_MESSAGE_TYPE_ACTIVATION || !txkey.valid) continue; // not a valid alert
        loadOrder.emplace_back(std::make_pair(key.block, 0), key.txid);
    }

    std::sort(loadOrder.begin(), loadOrder.end());

    ProcessActivations(loadOrder, [&](CMPTransaction& mp_obj) {
        if (OMNICORE_MESSAGE_TYPE_ACTIVATION != mp_obj.getType()) {
            PrintToLog("ERROR: While loading activation transaction %s: levelDB type mismatch, not an activation.\n", mp_obj.getHash().GetHex());
            return;
        }
        mp_obj.unlockLogic();
        if (0 != mp_obj.interpretPacket()) {
            PrintToLog("ERROR: While loading activation transaction %s: non-zero return from interpretPacket\n", mp_obj.getHash().GetHex());
            return;
        }
    });

    CheckLiveActivations(blockHeight);

    // This alert never expires as long as custom activations are used
    if (gArgs.IsArgSet("-omniactivationallowsender") || gArgs.IsArgSet("-omniactivationignoresender")) {
        AddAlert("omnicore", ALERT_CLIENT_VERSION_EXPIRY, std::numeric_limits<uint32_t>::max(),
                "Authorization for feature activation has been modified.  Data provided by this client should not be trusted.");
    }
}

bool CMPTxList::LoadFreezeState(int blockHeight)
{
    PrintToLog("Loading freeze state from levelDB\n");

    std::vector<activation> loadOrder;
    CDBaseIterator tx_it{NewIterator()};
    CDBaseIterator it{NewIterator(), CBlockTxKey{uint32_t(blockHeight)}};
    for (; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (tx_it.Seek(PartialKey<CTxKey>(key.txid)); !tx_it) continue;
        auto txkey = tx_it.Key<CTxKey>();
        if (!txkey.valid) continue;
        if (txkey.type != MSC_TYPE_FREEZE_PROPERTY_TOKENS && txkey.type != MSC_TYPE_UNFREEZE_PROPERTY_TOKENS
        && txkey.type != MSC_TYPE_ENABLE_FREEZING && txkey.type != MSC_TYPE_DISABLE_FREEZING) continue;
        int txPosition = pDbTransaction->FetchTransactionPosition(key.txid);
        loadOrder.emplace_back(std::make_pair(key.block, txPosition), key.txid);
    }

    std::sort(loadOrder.begin(), loadOrder.end());

    int txnsLoaded = 0;
    ProcessActivations(loadOrder, [&](CMPTransaction& mp_obj) {
        if (MSC_TYPE_FREEZE_PROPERTY_TOKENS != mp_obj.getType() && MSC_TYPE_UNFREEZE_PROPERTY_TOKENS != mp_obj.getType() &&
                MSC_TYPE_ENABLE_FREEZING != mp_obj.getType() && MSC_TYPE_DISABLE_FREEZING != mp_obj.getType()) {
            PrintToLog("ERROR: While loading freeze transaction %s: levelDB type mismatch, not a freeze transaction.\n", mp_obj.getHash().GetHex());
            return;
        }
        mp_obj.unlockLogic();
        if (0 != mp_obj.interpretPacket()) {
            PrintToLog("ERROR: While loading freeze transaction %s: non-zero return from interpretPacket\n", mp_obj.getHash().GetHex());
            return;
        }
        txnsLoaded++;
    });

    if (blockHeight > 497000 && !isNonMainNet()) {
        assert(txnsLoaded >= 2); // sanity check against a failure to properly load the freeze state
    }
    return true;
}

bool CMPTxList::CheckForFreezeTxs(int blockHeight)
{
    CDBaseIterator tx_it{NewIterator()};
    CDBaseIterator it{NewIterator(), PartialKey<CBlockTxKey>(uint32_t(blockHeight))};
    for (; it; ++it) {
        if (tx_it.Seek(PartialKey<CTxKey>(it.Key<CBlockTxKey>().txid)); !tx_it) continue;
        auto txtype = tx_it.Key<CTxKey>().type;
        if (txtype == MSC_TYPE_FREEZE_PROPERTY_TOKENS || txtype == MSC_TYPE_UNFREEZE_PROPERTY_TOKENS ||
            txtype == MSC_TYPE_ENABLE_FREEZING || txtype == MSC_TYPE_DISABLE_FREEZING) {
            return true;
        }
    }
    return false;
}

void CMPTxList::printStats()
{
    PrintToLog("CMPTxList stats: nWritten= %d , nRead= %d\n", nWritten, nRead);
}

void CMPTxList::printAll()
{
    int count = 0;
    std::string skey, svalue;
    for (CDBaseIterator it{NewIterator()}; it; ++it) {
        switch(it.Key()[0]) {
            case CTxKey::prefix: {
                auto key = it.Key<CTxKey>();
                skey = key.txid.ToString();
                auto value = it.Value<int64_t>();
                svalue = strprintf("%d:%d:%d:%d", key.block, key.valid, key.type, value);
            } break;
            case CPaymentTxKey::prefix: {
                auto key = it.Key<CPaymentTxKey>();
                skey = strprintf("%s-%d", key.txid.ToString(), key.payments);
                auto value = it.Value<CPaymentTxValue>();
                svalue = strprintf("%d:%d:%d:%s:%s:%d:%d", key.block, key.valid, value.vout, value.buyer, value.seller, value.propertyId, value.amount);
            } break;
            case CDexCancelTxKey::prefix: {
                auto key = it.Key<CDexCancelTxKey>();
                skey = strprintf("%s-%d", key.txid.ToString(), key.affected);
                auto value = it.Value<CDexCancelTxValue>();
                svalue = strprintf("%d:%d:%d:%d", key.block, key.valid, value.propertyId, value.amount);
            } break;
            case CSendAllTxKey::prefix: {
                auto key = it.Key<CSendAllTxKey>();
                skey = strprintf("%s-%d", key.txid.ToString(), key.num);
                auto value = it.Value<CSendAllTxValue>();
                svalue = strprintf("%d:%d", value.propertyId, value.amount);
            } break;
            default:
                continue;
        }
        PrintToConsole("entry #%8d= %s:%s\n", ++count, skey, svalue);
    }
}

template<typename T>
bool DeleteToBatch(leveldb::WriteBatch& batch, CDBaseIterator& it, const CBlockTxKey& key)
{
    bool found = false;
    for (it.Seek(PartialKey<T>(key.txid)); it; ++it) {
        found = true;
        batch.Delete(it.Key());
    }
    return found;
}

// figure out if there was at least 1 Master Protocol transaction within the block range, or a block if starting equals ending
// block numbers are inclusive
// pass in bDeleteFound = true to erase each entry found within the block range
bool CMPTxList::isMPinBlockRange(int starting_block, int ending_block, bool bDeleteFound)
{
    unsigned int n_found = 0;
    leveldb::WriteBatch batch;
    CDBaseIterator tx_it{NewIterator()};
    std::set<uint256> paymentTxs, cancelTxs;
    CDBaseIterator it{NewIterator(), CBlockTxKey{uint32_t(ending_block)}};
    for (; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (key.block < starting_block) break;
        if (bDeleteFound) {
            DeleteToBatch<CTxKey>(batch, tx_it, key) ||
            DeleteToBatch<CSendAllTxKey>(batch, tx_it, key) ||
            (DeleteToBatch<CPaymentTxKey>(batch, tx_it, key) && paymentTxs.insert(key.txid).second) ||
            (DeleteToBatch<CDexCancelTxKey>(batch, tx_it, key) && cancelTxs.insert(key.txid).second);
            PrintToLog("%s() DELETING: %d=%s\n", __func__, key.block, key.txid.ToString());
        }
        ++n_found;
    }
    if (bDeleteFound && n_found) {
        for (it.Seek(CPaymentTxKey{}); it && !cancelTxs.empty(); ++it) {
            auto value = it.Value<CPaymentTxValue>();
            // if both cancel and payment txs are deleted don't put back payment one
            if (cancelTxs.erase(value.cancelTxId)
            && !paymentTxs.count(it.Key<CPaymentTxKey>().txid)) {
                value.cancelTxId.SetNull();
                batch.Put(it.Key(), ValueToString(value));
            }
        }
        WriteBatch(batch);
    }
    PrintToLog("%s(%d, %d); n_found= %d\n", __func__, starting_block, ending_block, n_found);
    return (n_found);
}
