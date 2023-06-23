
#include <omnicore/dbaddress.h>
#include <omnicore/dbbase.h>
#include <omnicore/utilsbitcoin.h>

#include <omnicore/errors.h>
#include <omnicore/log.h>

#include <fs.h>
#include <uint256.h>
#include <tinyformat.h>

#include <leveldb/status.h>

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

COmniAddressDB::COmniAddressDB(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading address index database: %s\n", status.ToString());
}

COmniAddressDB::~COmniAddressDB()
{
    if (msc_debug_persistence) PrintToLog("COmniAddressDB closed\n");
}

bool COmniAddressDB::WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount>>& vect)
{
    leveldb::WriteBatch batch;
    for (auto it = vect.begin(); it != vect.end(); it++)
        batch.Put(KeyToString(it->first), ValueToString(it->second));
    return WriteBatch(batch);
}

bool COmniAddressDB::EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount>>& vect)
{
    leveldb::WriteBatch batch;
    for (auto it = vect.begin(); it != vect.end(); it++)
        batch.Delete(KeyToString(it->first));
    return WriteBatch(batch);
}

bool COmniAddressDB::ReadAddressIndex(const uint256& addressHash, unsigned int type,
                                    std::vector<std::pair<CAddressIndexKey, CAmount>>& addressIndex,
                                    int start, int end)
{
    if (start < 0) start = 0;
    const auto checkAddress = !addressHash.IsNull();

    CDBaseIterator it{NewIterator(), CAddressIndexKey{type, addressHash, start}};
    for (; it; ++it) {
        auto key = it.Key<CAddressIndexKey>();
        if (key.type != type || (checkAddress && key.hashBytes != addressHash)) {
            break;
        }
        CAmount value;
        if (it.Value(value)) {
            addressIndex.emplace_back(key, value);
        }
    }
    return true;
}

bool COmniAddressDB::UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& vect)
{
    leveldb::WriteBatch batch;
    for (auto it = vect.begin(); it != vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Delete(KeyToString(it->first));
        } else {
            batch.Put(KeyToString(it->first), ValueToString(it->second));
        }
    }
    return WriteBatch(batch);
}

bool COmniAddressDB::ReadAddressUnspentIndex(const uint256& addressHash, unsigned int type,
                                           std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& unspentOutputs)
{
    const auto checkAddress = !addressHash.IsNull();

    CDBaseIterator it{NewIterator(), CAddressUnspentKey{type, addressHash}};
    for (; it; ++it) {
        auto key = it.Key<CAddressUnspentKey>();
        if (key.type != type || (checkAddress && key.hashBytes != addressHash)) {
            break;
        }
        CAddressUnspentValue value;
        if (it.Value(value)) {
            unspentOutputs.emplace_back(key, value);
        }
    }
    return true;
}

bool COmniAddressDB::WriteTimestampIndex(const CTimestampIndexKey& timestampIndex)
{
    return Write(timestampIndex, "");
}

bool COmniAddressDB::ReadTimestampIndex(const unsigned int high, const unsigned int low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int>>& hashes)
{
    CDBaseIterator it{NewIterator(), CTimestampIndexKey{low}};
    for (; it; ++it) {
        auto key = it.Key<CTimestampIndexKey>();
        if (key.timestamp > high) {
            break;
        }
        hashes.emplace_back(key.blockHash, key.timestamp);
    }
    return true;
}

struct CTimestampBlockIndexKey {
    static constexpr uint8_t prefix = 'z';
    const uint256& hash;
    template<typename Stream>
    void Serialize(Stream& s) const {
        ::Serialize(s, hash);
    }
};

bool COmniAddressDB::WriteTimestampBlockIndex(const uint256& hash, unsigned int logicalts)
{
    return Write(CTimestampBlockIndexKey{hash}, logicalts);
}

bool COmniAddressDB::ReadTimestampBlockIndex(const uint256& hash, unsigned int& ltimestamp)
{
    return Read(CTimestampBlockIndexKey{hash}, ltimestamp);
}

bool COmniAddressDB::ReadSpentIndex(const CSpentIndexKey& key, CSpentIndexValue& value)
{
    return Read(key, value);
}

bool COmniAddressDB::UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue>>& vect)
{
    leveldb::WriteBatch batch;
    for (auto it = vect.begin(); it != vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Delete(KeyToString(it->first));
        } else {
            batch.Put(KeyToString(it->first), ValueToString(it->second));
        }
    }
    return WriteBatch(batch);
}

struct CFlagKey {
    static constexpr uint8_t prefix = 'F';
    const std::string& name;
    template<typename Stream>
    void Serialize(Stream& s) const {
        ::Serialize(s, name);
    }
};

bool COmniAddressDB::WriteFlag(const std::string& name, bool fValue)
{
    return Write(CFlagKey{name}, fValue ? uint8_t{1} : uint8_t{0});
}

bool COmniAddressDB::ReadFlag(const std::string& name, bool& fValue) {
    uint8_t ch;
    if (!Read(CFlagKey{name}, ch))
        return false;
    fValue = ch == uint8_t{1};
    return true;
}
