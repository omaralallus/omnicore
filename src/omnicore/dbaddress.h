#ifndef BITCOIN_OMNICORE_DBADDRESS_H
#define BITCOIN_OMNICORE_DBADDRESS_H

#include <omnicore/dbbase.h>

#include <fs.h>
#include <script/standard.h>
#include <uint256.h>

#include <stdint.h>

#include <string>
#include <vector>

struct CTimestampIndexKey {
    static constexpr uint8_t prefix = 'S';
    unsigned int timestamp = 0;
    uint256 blockHash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 36;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata32be(s, timestamp);
        blockHash.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        timestamp = ser_readdata32be(s);
        blockHash.Unserialize(s);
    }
};

struct CAddressUnspentKey {
    static constexpr uint8_t prefix = 'u';
    unsigned int type = 0;
    uint256 hashBytes;
    uint256 txhash;
    size_t index = 0;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 69;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
        txhash.Serialize(s);
        ser_writedata32(s, index);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        txhash.Unserialize(s);
        index = ser_readdata32(s);
    }
};

struct CAddressUnspentValue {
    CAmount satoshis = -1;
    CScript script;
    int blockHeight = 0;
    bool coinBase = false;

    SERIALIZE_METHODS(CAddressUnspentValue, obj) {
        READWRITE(obj.satoshis);
        READWRITEAS(CScriptBase, obj.script);
        READWRITE(obj.blockHeight);
        READWRITE(obj.coinBase);
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

struct CAddressIndexKey {
    static constexpr uint8_t prefix = 'a';
    unsigned int type = 0;
    uint256 hashBytes;
    int blockHeight = 0;
    unsigned int txindex = 0;
    uint256 txhash;
    size_t index = 0;
    bool spending = false;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 78;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        txhash.Serialize(s);
        ser_writedata32(s, index);
        uint8_t f = spending;
        ser_writedata8(s, f);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        txhash.Unserialize(s);
        index = ser_readdata32(s);
        uint8_t f = ser_readdata8(s);
        spending = f;
    }
};

struct CSpentIndexKey {
    static constexpr uint8_t prefix = 'p';
    uint256 txid;
    unsigned int outputIndex = 0;

    SERIALIZE_METHODS(CSpentIndexKey, obj) {
        READWRITE(obj.txid, obj.outputIndex);
    }
};

struct CSpentIndexValue {
    uint256 txid;
    unsigned int inputIndex = 0;
    int blockHeight = 0;
    CAmount satoshis = 0;
    unsigned int addressType = 0;
    uint256 addressHash;

    SERIALIZE_METHODS(CSpentIndexValue, obj) {
        READWRITE(obj.txid, obj.inputIndex, obj.blockHeight,
                  obj.satoshis, obj.addressType, obj.addressHash);
    }

    bool IsNull() const {
        return txid.IsNull();
    }
};

/** LevelDB based storage for storing Omni address index data.
 */
class COmniAddressDB : public CDBBase
{
public:
    COmniAddressDB(const fs::path& path, bool fWipe);
    virtual ~COmniAddressDB();

    // Block explorer database functions
    bool WriteFlag(const std::string& name, bool fValue);
    bool ReadFlag(const std::string& name, bool& fValue);
    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount>>& vect);
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount>>& vect);
    bool ReadAddressIndex(const uint256& addressHash, unsigned int type,
                          std::vector<std::pair<CAddressIndexKey, CAmount>>& addressIndex,
                          int start = 0, int end = 0);
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& vect);
    bool ReadAddressUnspentIndex(const uint256& addressHash, unsigned int type,
                                 std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>>& vect);
    bool WriteTimestampIndex(const CTimestampIndexKey& timestampIndex);
    bool ReadTimestampIndex(const unsigned int high, const unsigned int low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int>>& vect);
    bool WriteTimestampBlockIndex(const uint256& blockhashIndex, unsigned int logicalts);
    bool ReadTimestampBlockIndex(const uint256& hash, unsigned int& logicalTS);
    bool ReadSpentIndex(const CSpentIndexKey& key, CSpentIndexValue& value);
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue>>& vect);
};

namespace mastercore
{
    //! LevelDB based storage for storing Omni transaction validation and position in block data
    extern COmniAddressDB* pDbAddress;
}

#endif // BITCOIN_OMNICORE_DBADDRESS_H

