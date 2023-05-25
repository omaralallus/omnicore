#ifndef BITCOIN_OMNICORE_OMNICORE_H
#define BITCOIN_OMNICORE_OMNICORE_H

class CBlockIndex;
class CCoinsView;
class CCoinsViewCache;
class CTransaction;
class Coin;

#include <omnicore/log.h>
#include <omnicore/tally.h>

#include <node/blockstorage.h>
#include <node/context.h>
#include <policy/feerate.h>
#include <script/standard.h>
#include <sync.h>
#include <uint256.h>
#include <util/system.h>

#include <univalue.h>

#include <stdint.h>

#include <map>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>

// Store the state every 100 blocks to handle reorg
int const STORE_EVERY_N_BLOCK = 100;
// Store the state every 10000 blocks during initial block download
int const STORE_EVERY_N_BLOCK_IDB = 10000;
// Don't store the state every block on mainnet until block 770000
// was reached, can be set with -omniskipstoringstate.
int const DONT_STORE_MAINNET_STATE_UNTIL = 770000;

#define TEST_ECO_PROPERTY_1 (0x80000003UL)

// increment this value to force a refresh of the state (similar to --startclean)
#define DB_VERSION 8

// could probably also use: int64_t maxInt64 = std::numeric_limits<int64_t>::max();
// maximum numeric values from the spec:
#define MAX_INT_8_BYTES (9223372036854775807UL)

// maximum size of string fields
#define SP_STRING_FIELD_LEN 256

// Omni Layer Transaction (Packet) Version
#define MP_TX_PKT_V0  0
#define MP_TX_PKT_V1  1


// Transaction types, from the spec
enum TransactionType {
  MSC_TYPE_SIMPLE_SEND                =  0,
  MSC_TYPE_RESTRICTED_SEND            =  2,
  MSC_TYPE_SEND_TO_OWNERS             =  3,
  MSC_TYPE_SEND_ALL                   =  4,
  MSC_TYPE_SEND_NONFUNGIBLE           =  5,
  MSC_TYPE_SEND_TO_MANY               =  7,
  MSC_TYPE_SAVINGS_MARK               = 10,
  MSC_TYPE_SAVINGS_COMPROMISED        = 11,
  MSC_TYPE_RATELIMITED_MARK           = 12,
  MSC_TYPE_AUTOMATIC_DISPENSARY       = 15,
  MSC_TYPE_TRADE_OFFER                = 20,
  MSC_TYPE_ACCEPT_OFFER_BTC           = 22,
  MSC_TYPE_METADEX_TRADE              = 25,
  MSC_TYPE_METADEX_CANCEL_PRICE       = 26,
  MSC_TYPE_METADEX_CANCEL_PAIR        = 27,
  MSC_TYPE_METADEX_CANCEL_ECOSYSTEM   = 28,
  MSC_TYPE_NOTIFICATION               = 31,
  MSC_TYPE_OFFER_ACCEPT_A_BET         = 40,
  MSC_TYPE_CREATE_PROPERTY_FIXED      = 50,
  MSC_TYPE_CREATE_PROPERTY_VARIABLE   = 51,
  MSC_TYPE_PROMOTE_PROPERTY           = 52,
  MSC_TYPE_CLOSE_CROWDSALE            = 53,
  MSC_TYPE_CREATE_PROPERTY_MANUAL     = 54,
  MSC_TYPE_GRANT_PROPERTY_TOKENS      = 55,
  MSC_TYPE_REVOKE_PROPERTY_TOKENS     = 56,
  MSC_TYPE_CHANGE_ISSUER_ADDRESS      = 70,
  MSC_TYPE_ENABLE_FREEZING            = 71,
  MSC_TYPE_DISABLE_FREEZING           = 72,
  MSC_TYPE_ADD_DELEGATE               = 73,
  MSC_TYPE_REMOVE_DELEGATE            = 74,
  MSC_TYPE_FREEZE_PROPERTY_TOKENS     = 185,
  MSC_TYPE_UNFREEZE_PROPERTY_TOKENS   = 186,
  MSC_TYPE_ANYDATA                    = 200,
  MSC_TYPE_NONFUNGIBLE_DATA           = 201,
  OMNICORE_MESSAGE_TYPE_DEACTIVATION  = 65533,
  OMNICORE_MESSAGE_TYPE_ACTIVATION    = 65534,
  OMNICORE_MESSAGE_TYPE_ALERT         = 65535
};

#define MSC_PROPERTY_TYPE_INDIVISIBLE             1
#define MSC_PROPERTY_TYPE_DIVISIBLE               2
#define MSC_PROPERTY_TYPE_NONFUNGIBLE             5
#define MSC_PROPERTY_TYPE_INDIVISIBLE_REPLACING   65
#define MSC_PROPERTY_TYPE_DIVISIBLE_REPLACING     66
#define MSC_PROPERTY_TYPE_INDIVISIBLE_APPENDING   129
#define MSC_PROPERTY_TYPE_DIVISIBLE_APPENDING     130

#define PKT_RETURNED_OBJECT    (1000)

#define PKT_ERROR             ( -9000)
#define DEX_ERROR_SELLOFFER   (-10000)
#define DEX_ERROR_ACCEPT      (-20000)
#define DEX_ERROR_PAYMENT     (-30000)
// Smart Properties
#define PKT_ERROR_SP          (-40000)
#define PKT_ERROR_CROWD       (-45000)
// Send To Owners
#define PKT_ERROR_STO         (-50000)
#define PKT_ERROR_SEND        (-60000)
#define PKT_ERROR_TRADEOFFER  (-70000)
#define PKT_ERROR_METADEX     (-80000)
#define METADEX_ERROR         (-81000)
#define PKT_ERROR_TOKENS      (-82000)
#define PKT_ERROR_SEND_ALL    (-83000)
#define PKT_ERROR_ANYDATA     (-84000)
#define PKT_ERROR_NFT         (-85000)
#define PKT_ERROR_SEND_MANY   (-86000)

#define OMNI_PROPERTY_BTC       0
#define OMNI_PROPERTY_MSC       1
#define OMNI_PROPERTY_TMSC      2
#define OMNI_PROPERTY_EMAID     3  // MaidSafeCoin
#define OMNI_PROPERTY_USDT      31 // Tether USD

/** Number formatting related functions. */
std::string FormatDivisibleMP(int64_t amount, bool fSign = false);
std::string FormatDivisibleShortMP(int64_t amount);
std::string FormatIndivisibleMP(int64_t amount);
std::string FormatByType(int64_t amount, uint16_t propertyType);
// Note: require initialized state to get divisibility.
std::string FormatMP(uint32_t propertyId, int64_t amount, bool fSign = false);
std::string FormatShortMP(uint32_t propertyId, int64_t amount);

/** Returns the Exodus address. */
const CTxDestination ExodusAddress();

/** Returns the Exodus crowdsale address. */
const CTxDestination ExodusCrowdsaleAddress(int nBlock = 0);

/** Returns the marker for class C transactions. */
const std::vector<unsigned char> GetOmMarker();

//! Used to indicate, whether to automatically commit created transactions
extern bool autoCommit;

//! Global lock for state objects
extern RecursiveMutex cs_tally;

//! Available balances of wallet properties
extern std::map<uint32_t, int64_t> global_balance_money;
//! Reserved balances of wallet propertiess
extern std::map<uint32_t, int64_t> global_balance_reserved;
//! Vector containing a list of properties relative to the wallet
extern std::set<uint32_t> global_wallet_property_list;

extern std::set<std::string> wallet_addresses;

extern CFeeRate minRelayTxFee;

extern bool fOmniSafeAddresses;

extern std::optional<std::reference_wrapper<node::NodeContext>> g_context;

int64_t GetTokenBalance(const std::string& address, uint32_t propertyId, TallyType ttype);
int64_t GetAvailableTokenBalance(const std::string& address, uint32_t propertyId);
int64_t GetReservedTokenBalance(const std::string& address, uint32_t propertyId);
int64_t GetFrozenTokenBalance(const std::string& address, uint32_t propertyId);

/** Global handler to initialize Omni Core. */
int mastercore_init();

/** Global handler to shut down Omni Core. */
int mastercore_shutdown();

/** Block and transaction handlers. */
void mastercore_handler_disc_begin(const int nHeight);
int mastercore_handler_block_begin(int nBlockNow, CBlockIndex const * pBlockIndex);
int mastercore_handler_block_end(int nBlockNow, CBlockIndex const * pBlockIndex, unsigned int);
bool mastercore_handler_tx(const CTransaction& tx, int nBlock, unsigned int idx, const CBlockIndex* pBlockIndex, const std::shared_ptr<std::map<COutPoint, Coin>> removedCoins);

/** Scans for marker and if one is found, add transaction to marker cache. */
void TryToAddToMarkerCache(const CTransactionRef& tx);
/** Removes transaction from marker cache. */
void RemoveFromMarkerCache(const uint256& txHash);
/** Checks, if transaction is in marker cache. */
bool IsInMarkerCache(const uint256& txHash);

/** Global handler to total wallet balances. */
void CheckWalletUpdate();

/** Used to notify that the number of tokens for a property has changed. */
void NotifyTotalTokensChanged(uint32_t propertyId, int block);

bool GetAddressIndex(uint256 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                     int start = 0, int end = 0);

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);

bool GetAddressUnspent(uint256 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs);

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes);

class Chainstate;
/** @returns the most-work valid chainstate. */
Chainstate& ChainstateActive();

class CChain;
/** @returns the most-work chain. */
CChain& ChainActive();

/** @returns the global block index map. */
node::BlockMap& BlockIndex();

namespace Consensus {
struct Params;
}

/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256& hash, CTransactionRef& tx, const Consensus::Params& params, uint256& hashBlock, const CBlockIndex* const blockIndex = nullptr);

struct CTimestampIndexIteratorKey {
    unsigned int timestamp;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 4;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata32be(s, timestamp);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        timestamp = ser_readdata32be(s);
    }

    CTimestampIndexIteratorKey(unsigned int time) {
        timestamp = time;
    }

    CTimestampIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
    }
};

struct CTimestampIndexKey {
    unsigned int timestamp;
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

    CTimestampIndexKey(unsigned int time, uint256 hash) {
        timestamp = time;
        blockHash = hash;
    }

    CTimestampIndexKey() {
        SetNull();
    }

    void SetNull() {
        timestamp = 0;
        blockHash.SetNull();
    }
};

struct CTimestampBlockIndexKey {
    uint256 blockHash;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 32;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        blockHash.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        blockHash.Unserialize(s);
    }

    CTimestampBlockIndexKey(uint256 hash) {
        blockHash = hash;
    }

    CTimestampBlockIndexKey() {
        SetNull();
    }

    void SetNull() {
        blockHash.SetNull();
    }
};

struct CTimestampBlockIndexValue {
    unsigned int ltimestamp;
    size_t GetSerializeSize(int nType, int nVersion) const {
        return 4;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata32be(s, ltimestamp);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        ltimestamp = ser_readdata32be(s);
    }

    CTimestampBlockIndexValue (unsigned int time) {
        ltimestamp = time;
    }

    CTimestampBlockIndexValue() {
        SetNull();
    }

    void SetNull() {
        ltimestamp = 0;
    }
};

struct CAddressUnspentKey {
    unsigned int type;
    uint256 hashBytes;
    uint256 txhash;
    size_t index;

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

    CAddressUnspentKey(unsigned int addressType, uint256 addressHash, uint256 txid, size_t indexValue) {
        type = addressType;
        hashBytes = addressHash;
        txhash = txid;
        index = indexValue;
    }

    CAddressUnspentKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        txhash.SetNull();
        index = 0;
    }
};

struct CAddressUnspentValue {
    CAmount satoshis;
    CScript script;
    int blockHeight;
    bool coinBase;

    SERIALIZE_METHODS(CAddressUnspentValue, obj) {
        READWRITE(obj.satoshis);
        READWRITE(*(CScriptBase*)(&obj.script));
        READWRITE(obj.blockHeight);
        READWRITE(obj.coinBase);
    }

    CAddressUnspentValue(CAmount sats, CScript scriptPubKey, int height, bool coinbase) {
        satoshis = sats;
        script = scriptPubKey;
        blockHeight = height;
        coinBase = coinbase;
    }

    CAddressUnspentValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        script.clear();
        blockHeight = 0;
        coinBase = false;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

struct CAddressIndexKey {
    unsigned int type;
    uint256 hashBytes;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    size_t index;
    bool spending;

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

    CAddressIndexKey(unsigned int addressType, uint256 addressHash, int height, int blockindex,
                     uint256 txid, size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        spending = false;
    }

};

struct CAddressIndexIteratorHeightKey {
    unsigned int type;
    uint256 hashBytes;
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 37;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
        ser_writedata32be(s, blockHeight);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
        blockHeight = ser_readdata32be(s);
    }

    CAddressIndexIteratorHeightKey(unsigned int addressType, uint256 addressHash, int height) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
    }
};

struct CAddressIndexIteratorKey {
    unsigned int type;
    uint256 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 33;
    }

    template<typename Stream>
    void Serialize(Stream& s) const {
        ser_writedata8(s, type);
        hashBytes.Serialize(s);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        type = ser_readdata8(s);
        hashBytes.Unserialize(s);
    }

    CAddressIndexIteratorKey(unsigned int addressType, uint256 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CAddressIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = 0;
        hashBytes.SetNull();
    }
};


namespace mastercore
{
//! In-memory collection of all amounts for all addresses for all properties
extern std::unordered_map<std::string, CMPTally> mp_tally_map;

// TODO: move, rename
extern CCoinsView viewDummy;
extern CCoinsViewCache view;
//! Guards coins view cache
extern RecursiveMutex cs_tx_cache;

/** Returns the encoding class, used to embed a payload. */
int GetEncodingClass(const CTransaction& tx, int nBlock);

/** Determines, whether it is valid to use a Class C transaction for a given payload size. */
bool UseEncodingClassC(size_t nDataSize);

bool isTestEcosystemProperty(uint32_t propertyId);
bool isMainEcosystemProperty(uint32_t propertyId);
uint32_t GetNextPropertyId(bool maineco); // maybe move into sp

CMPTally* getTally(const std::string& address);
bool update_tally_map(const std::string& who, uint32_t propertyId, int64_t amount, TallyType ttype);
int64_t getTotalTokens(uint32_t propertyId, int64_t* n_owners_total = nullptr);

std::string strMPProperty(uint32_t propertyId);
std::string strTransactionType(uint16_t txType);
std::string getTokenLabel(uint32_t propertyId);

/**
    NOTE: The following functions are only permitted for properties
          managed by a central issuer that have enabled freezing.
 **/
/** Adds an address and property to the frozenMap **/
void freezeAddress(const std::string& address, uint32_t propertyId);
/** Removes an address and property from the frozenMap **/
void unfreezeAddress(const std::string& address, uint32_t propertyId);
/** Checks whether an address and property are frozen **/
bool isAddressFrozen(const std::string& address, uint32_t propertyId);
/** Adds a property to the freezingEnabledMap **/
void enableFreezing(uint32_t propertyId, int liveBlock);
/** Removes a property from the freezingEnabledMap **/
void disableFreezing(uint32_t propertyId);
/** Checks whether a property has freezing enabled **/
bool isFreezingEnabled(uint32_t propertyId, int block);
/** Clears the freeze state in the event of a reorg **/
void ClearFreezeState();
/** Prints the freeze state **/
void PrintFreezeState();

}

#endif // BITCOIN_OMNICORE_OMNICORE_H
