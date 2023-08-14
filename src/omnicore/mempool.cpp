
#include <omnicore/dbaddress.h>
#include <omnicore/mempool.h>
#include <omnicore/omnicore.h>
#include <omnicore/parsing.h>
#include <omnicore/utilsbitcoin.h>

#include <txmempool.h>

#include <map>

namespace mastercore {

struct CMempoolAddressDeltaKeyCompare
{
    bool operator()(const CMempoolAddressDeltaKey& a, const CMempoolAddressDeltaKey& b) const {
        if (a.type != b.type) {
            return a.type < b.type;
        }
        if (a.addressBytes != b.addressBytes) {
            return a.addressBytes < b.addressBytes;
        }
        if (a.txhash != b.txhash) {
            return a.txhash < b.txhash;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return a.spending < b.spending;
    }
};

struct CSpentIndexKeyCompare
{
    bool operator()(const CSpentIndexKey& a, const CSpentIndexKey& b) const {
        if (a.txid == b.txid) {
            return a.outputIndex < b.outputIndex;
        }
        return a.txid < b.txid;
    }
};

RecursiveMutex cs_mempool;

std::map<uint256, CTransactionRef> mapMempool GUARDED_BY(cs_mempool);

typedef std::map<CMempoolAddressDeltaKey, CMempoolAddressDelta, CMempoolAddressDeltaKeyCompare> addressDeltaMap;
addressDeltaMap mapAddress GUARDED_BY(cs_mempool);

typedef std::map<uint256, std::vector<addressDeltaMap::iterator>> addressDeltaMapInserted;
addressDeltaMapInserted mapAddressInserted GUARDED_BY(cs_mempool);

typedef std::map<CSpentIndexKey, CSpentIndexValue, CSpentIndexKeyCompare> mapSpentIndex;
mapSpentIndex mapSpent GUARDED_BY(cs_mempool);

typedef std::map<uint256, std::vector<mapSpentIndex::iterator>> mapSpentIndexInserted;
mapSpentIndexInserted mapSpentInserted GUARDED_BY(cs_mempool);

static void AddAddressIndexToMempool(CTransactionRef tx, CCoinsViewCache& view)
{
    LOCK(cs_mempool);
    auto time = GetTime();
    std::vector<addressDeltaMap::iterator> inserted;

    const uint256& txhash = tx->GetHash();
    for (unsigned int j = 0; j < tx->vin.size(); j++) {
        const auto& prevout = tx->vin[j].prevout;
        const auto& output = view.GetOutputFor(tx->vin[j]);
        if (auto result = ScriptToUint(output.scriptPubKey)) {
            auto& [index, address] = *result;
            CMempoolAddressDeltaKey key{index, address, txhash, j, 1};
            CMempoolAddressDelta delta{time, output.nValue * -1, prevout.hash, prevout.n};
            if (auto [it, ins] = mapAddress.emplace(key, delta); ins) {
                inserted.push_back(it);
            }
        }
    }

    for (unsigned int k = 0; k < tx->vout.size(); k++) {
        const CTxOut &out = tx->vout[k];
        if (auto result = ScriptToUint(out.scriptPubKey)) {
            auto& [index, address] = *result;
            CMempoolAddressDeltaKey key{index, address, txhash, k};
            CMempoolAddressDelta delta{time, out.nValue};
            if (auto [it, ins] = mapAddress.emplace(key, delta); ins) {
                inserted.push_back(it);
            }
        }
    }
    mapAddressInserted.emplace(txhash, inserted);
}

static void RemoveAddressIndexFromMempool(const uint256& txhash)
{
    LOCK(cs_mempool);
    auto it = mapAddressInserted.find(txhash);
    if (it != mapAddressInserted.end()) {
        auto& keys = (*it).second;
        for (auto mit = keys.begin(); mit != keys.end(); mit++) {
            mapAddress.erase(*mit);
        }
        mapAddressInserted.erase(it);
    }
}

static void AddSpentIndexToMempool(CTransactionRef tx, CCoinsViewCache& view)
{
    LOCK(cs_mempool);
    std::vector<mapSpentIndex::iterator> inserted;

    const uint256& txhash = tx->GetHash();
    for (unsigned int j = 0; j < tx->vin.size(); j++) {
        const auto& prevout = tx->vin[j].prevout;
        const auto& output = view.GetOutputFor(tx->vin[j]);
        const auto& outValue = output.nValue;
        const auto& scriptPubKey = output.scriptPubKey;
        uint256 addressHash;
        int version;
        unsigned int addressType;
        std::vector<unsigned char> addressBytes(32);

        if (scriptPubKey.IsPayToScriptHash()) {
            std::copy(scriptPubKey.begin() + 2, scriptPubKey.begin() + 22, addressBytes.begin());
            addressHash = uint256(addressBytes);
            addressType = 2;
        } else if (scriptPubKey.IsPayToPubkeyHash()) {
            std::copy(scriptPubKey.begin() + 3, scriptPubKey.begin() + 23, addressBytes.begin());
            addressHash = uint256(addressBytes);
            addressType = 1;
        } else if (scriptPubKey.IsPayToPubkey()) {
            std::vector<unsigned char> pubkeyBytes(scriptPubKey.begin() + 1, scriptPubKey.end()-1);
            uint160 hashBytes = Hash160(pubkeyBytes);
            std::copy(hashBytes.begin(), hashBytes.end(), addressBytes.begin());
            addressHash = uint256(addressBytes);
            addressType = 1;
        } else if (scriptPubKey.IsPayToWitnessPubkeyHash()) {
            std::copy(scriptPubKey.begin() + 2, scriptPubKey.end(), addressBytes.begin());
            addressHash = uint256(addressBytes);
            addressType = 4;
        } else if (scriptPubKey.IsWitnessProgram(version, addressBytes)) {
            addressHash = uint256(addressBytes);
            addressType = version == 0 ? 3 : 5;
        } else {
            addressHash.SetNull();
            addressType = 0;
        }

        CSpentIndexKey key = CSpentIndexKey{prevout.hash, prevout.n};
        CSpentIndexValue value = CSpentIndexValue{txhash, j, -1, outValue, addressType, addressHash};

        if (auto [it, ins] = mapSpent.emplace(key, value); ins) {
            inserted.push_back(it);
        }
    }
    mapSpentInserted.emplace(txhash, inserted);
}

static void RemoveSpentIndexFromMempool(const uint256& txhash)
{
    LOCK(cs_mempool);
    auto it = mapSpentInserted.find(txhash);
    if (it != mapSpentInserted.end()) {
        auto& keys = (*it).second;
        for (auto mit = keys.begin(); mit != keys.end(); mit++) {
            mapSpent.erase(*mit);
        }
        mapSpentInserted.erase(it);
    }
}

bool GetSpentIndexFromMempool(const CSpentIndexKey &key, CSpentIndexValue &value)
{
    LOCK(cs_mempool);
    auto it = mapSpent.find(key);
    if (it != mapSpent.end()) {
        value = it->second;
        return true;
    }
    return false;
}

bool GetAddressIndexFromMempool(const std::vector<std::pair<uint256, unsigned>>& addresses, std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>>& results)
{
    LOCK(cs_mempool);
    for (auto it = addresses.begin(); it != addresses.end(); it++) {
        auto ait = mapAddress.lower_bound(CMempoolAddressDeltaKey{(*it).second, (*it).first});
        while (ait != mapAddress.end() && (*ait).first.addressBytes == (*it).first && (*ait).first.type == (*it).second) {
            results.push_back(*ait);
            ait++;
        }
    }
    return true;
}

void AddTransactionToMempool(const CTransactionRef& tx)
{
    LOCK(cs_mempool);
    mapMempool.insert_or_assign(tx->GetHash(), tx);
    if (fAddressIndex) {
        CCoinsViewCacheOnly view;
        FillTxInputCache(*tx, view);
        AddSpentIndexToMempool(tx, view);
        AddAddressIndexToMempool(tx, view);
    }
}

void RemoveTransactionFromMempool(const CTransactionRef& tx)
{
    LOCK(cs_mempool);
    mapMempool.erase(tx->GetHash());
    if (fAddressIndex) {
        RemoveSpentIndexFromMempool(tx->GetHash());
        RemoveAddressIndexFromMempool(tx->GetHash());
    }
}

std::vector<uint256> ClearMempool()
{
    LOCK(cs_mempool);
    std::vector<uint256> hashes;
    MempoolQueryHashes(hashes);
    mapMempool.clear();
    return hashes;
}

void MempoolQueryHashes(std::vector<uint256>& hashes)
{
    LOCK(cs_mempool);
    hashes.clear();
    hashes.reserve(mapMempool.size());
    for (auto& [hash, _] : mapMempool)
        hashes.push_back(hash);
}

CTransactionRef GetMempoolTransaction(const uint256& hash)
{
    LOCK(cs_mempool);
    auto it = mapMempool.find(hash);
    return it != mapMempool.end() ? it->second : nullptr;
}

}
