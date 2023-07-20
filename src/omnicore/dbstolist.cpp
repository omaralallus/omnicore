
#include <omnicore/convert.h>
#include <omnicore/dbstolist.h>
#include <omnicore/log.h>
#include <omnicore/script.h>
#include <omnicore/sp.h>
#include <omnicore/uint256_extensions.h>
#include <omnicore/walletutils.h>

#include <fs.h>
#include <interfaces/wallet.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <tinyformat.h>

#include <univalue.h>

#include <leveldb/iterator.h>
#include <leveldb/slice.h>
#include <leveldb/status.h>

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

using mastercore::IsMyAddress;

CMPSTOList::CMPSTOList(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading send-to-owners database: %s\n", status.ToString());
}

CMPSTOList::~CMPSTOList()
{
    if (msc_debug_persistence) PrintToLog("CMPSTOList closed\n");
}

struct CTxAddressKey {
    static constexpr uint8_t prefix = 'h';
    uint256 hash;
    std::string address;
    int block = 0;
    uint32_t propertyId = 0;

    SERIALIZE_METHODS(CTxAddressKey, obj) {
        READWRITE(obj.hash);
        READWRITE(obj.address);
        READWRITE(obj.block);
        READWRITE(VARINT(obj.propertyId));
    }
};

void CMPSTOList::getRecipients(const uint256& txid, const std::string& filterAddress, UniValue* recipientArray, uint64_t* total, uint64_t* numRecipients, interfaces::Wallet* iWallet)
{
    bool filter = true; //default
    bool filterByWallet = true; //default
    bool filterByAddress = false; //default

    if (filterAddress == "*") filter = false;
    if (!filterAddress.empty() && filter) {
        filterByWallet = false;
        filterByAddress = true;
    }

    // the fee is variable based on version of STO - provide number of recipients and allow calling function to work out fee
    *numRecipients = 0;

    CDBaseIterator it{NewIterator(), CTxAddressKey{txid, filterByAddress ? filterAddress : ""}};
    for (; it; ++it) {
        auto key = it.Key<CTxAddressKey>();
        if (key.hash != txid) break;
        auto& recipientAddress = key.address;
        // see if txid is in the data
        ++(*numRecipients);
        // the txid exists inside the data, this address was a recipient of this STO, check filter and add the details
        if (filter && filterByAddress && filterAddress != recipientAddress) continue;
        if (filter && filterByWallet && !IsMyAddress(recipientAddress, iWallet)) continue;
        UniValue recipient(UniValue::VOBJ);
        auto amount = it.Value<uint64_t>();
        recipient.pushKV("address", TryEncodeOmniAddress(recipientAddress));
        recipient.pushKV("amount", FormatMP(key.propertyId, amount));
        *total += amount;
        recipientArray->push_back(recipient);
    }
}

struct CBlockTxKey {
    static constexpr uint8_t prefix = 'b';
    uint32_t block = ~0u;
    uint8_t chash[4];

    SERIALIZE_METHODS(CBlockTxKey, obj) {
        READWRITE(Using<BigEndian32Inv>(obj.block));
        READWRITE(obj.chash);
    }
};

CPartialKey PartialTxId(const CBlockTxKey& key)
{
    std::string pkey;
    CStringWriter{SER_DISK, CLIENT_VERSION, pkey} << CTxAddressKey::prefix << key.chash;
    return CPartialKey{std::move(pkey)};
}

std::unordered_map<int, uint256> CMPSTOList::getMySTOReceipts(const std::string& filterAddress, int startBlock, int endBlock, interfaces::Wallet &iWallet)
{
    uint32_t block = endBlock;
    CDBaseIterator tx_it{NewIterator()};
    std::unordered_map<int, uint256> mySTOReceipts;
    for (CDBaseIterator it{NewIterator(), CBlockTxKey{block}}; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (key.block < startBlock) break;
        for (tx_it.Seek(PartialTxId(key)); tx_it; ++tx_it) {
            auto tx_key = tx_it.Key<CTxAddressKey>();
            if (tx_key.block != key.block) continue;
            if (!filterAddress.empty() && tx_key.address != filterAddress) continue;
            if (!IsMyAddress(tx_key.address, &iWallet)) continue;
            mySTOReceipts.emplace(key.block, tx_key.hash);
            break;
        }
    }
    return mySTOReceipts;
}

/**
 * This function deletes records of STO receivers above/equal to a specific block from the STO database.
 *
 * Returns the number of records changed.
 */
int CMPSTOList::deleteAboveBlock(int blockNum)
{
    leveldb::WriteBatch batch;
    unsigned int n_found = 0;
    std::vector<std::string> vecSTORecords;
    CDBaseIterator tx_it{NewIterator()};
    for (CDBaseIterator it{NewIterator(), CBlockTxKey{}}; it; ++it) {
        auto key = it.Key<CBlockTxKey>();
        if (key.block < blockNum) break;
        batch.Delete(it.Key());
        for (tx_it.Seek(PartialTxId(key)); tx_it; ++tx_it) {
            auto tx_key = tx_it.Key<CTxAddressKey>();
            if (tx_key.block != key.block) continue;
            ++n_found;
            batch.Delete(tx_it.Key());
        }
    }
    WriteBatch(batch);
    PrintToLog("%s(%d); stodb updated records= %d\n", __func__, blockNum, n_found);
    return n_found;
}

void CMPSTOList::printStats()
{
    PrintToLog("CMPSTOList stats: tWritten= %d , tRead= %d\n", nWritten, nRead);
}

void CMPSTOList::printAll()
{
    int count = 0;
    for (CDBaseIterator it {NewIterator(), CTxAddressKey{}}; it; ++it) {
        ++count;
        auto key = it.Key<CTxAddressKey>();
        auto amount = it.Value<uint64_t>();
        auto value = strprintf("%s:%d:%d:%d", key.hash.ToString(), key.block, key.propertyId, amount);
        PrintToConsole("entry #%8d= %s:%s\n", count, key.address, value);
    }
}

void CMPSTOList::recordSTOReceive(const std::string& address, const uint256 &txid, int nBlock, uint32_t propertyId, uint64_t amount)
{
    CBlockTxKey key{uint32_t(nBlock)};
    std::copy(txid.begin(), txid.begin() + sizeof(key.chash), key.chash);
    bool status = Write(key, "") && Write(CTxAddressKey{txid, address, nBlock, propertyId}, amount);
    PrintToLog("%s(%d): add record: (%s) \n", __func__, nBlock, status ? "OK" : "NOK");
}
