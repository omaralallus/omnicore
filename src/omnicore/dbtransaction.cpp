
#include <cstdint>
#include <omnicore/dbtransaction.h>
#include <omnicore/errors.h>
#include <omnicore/log.h>

#include <fs.h>
#include <primitives/transaction.h>
#include <uint256.h>
#include <tinyformat.h>

#include <leveldb/status.h>

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

COmniTransactionDB::COmniTransactionDB(const fs::path& path, bool fWipe)
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading master transactions database: %s\n", status.ToString());
}

COmniTransactionDB::~COmniTransactionDB()
{
    if (msc_debug_persistence) PrintToLog("COmniTransactionDB closed\n");
}

struct CTxInfoKey {
    static constexpr uint8_t prefix = 't';
    const uint256& txid;

    SERIALIZE_METHODS(CTxInfoKey, obj) {
        READWRITE(obj.txid);
    }
};

struct COutPointCompression {
    FORMATTER_METHODS(COutPoint, obj) {
        READWRITE(obj.hash);
        READWRITE(Using<Varint>(obj.n));
    }
};

struct TxInCompression {
    // scriptWitness isn't serialized to save space
    FORMATTER_METHODS(CTxIn, obj) {
        READWRITE(Using<COutPointCompression>(obj.prevout));
        READWRITE(Using<ScriptCompression>(obj.scriptSig));
        READWRITE(Using<VarintInv>(obj.nSequence));
    }
};

struct CMutableTransactionCompression {
    FORMATTER_METHODS(CMutableTransaction, obj) {
        READWRITE(Using<VectorFormatter<TxInCompression>>(obj.vin));
        READWRITE(Using<VectorFormatter<TxOutCompression>>(obj.vout));
        READWRITE(Using<VarintSigned>(obj.nVersion));
        READWRITE(Using<Varint>(obj.nLockTime));
    }
};

struct CTxBlockValue {
    int blockHeight;
    uint32_t posInBlock;
    uint32_t processResult;

    SERIALIZE_METHODS(CTxBlockValue, obj) {
        READWRITE(Using<VarintSigned>(obj.blockHeight));
        READWRITE(Using<Varint>(obj.posInBlock));
        READWRITE(Using<Varint>(obj.processResult));
    }
};

struct CTxInfoValue : CTxBlockValue {
    CMutableTransaction tx;

    SERIALIZE_METHODS(CTxInfoValue, obj) {
        READWRITEAS(CTxBlockValue, obj);
        READWRITE(Using<CMutableTransactionCompression>(obj.tx));
    }
};

/**
 * Stores position in block and validation result for a transaction.
 */
void COmniTransactionDB::RecordTransaction(const CTransaction& tx, int block, uint32_t posInBlock, int processingResult)
{
    // invert negative values to compressed it
    uint32_t processResult = processingResult < 0 ? -processingResult : 0;
    CTxInfoValue value{block, posInBlock, processResult, CMutableTransaction{tx}};
    Write(CTxInfoKey{tx.GetHash()}, value);
    ++nWritten;
}

/**
 * Deletes transactions in case of rollback.
 */
void COmniTransactionDB::DeleteTransactions(const std::set<uint256>& txs)
{
    CDBWriteBatch batch;
    CDBaseIterator it{NewIterator()};
    for (auto& txid : txs) {
        batch.Delete(CTxInfoKey{txid});
    }
    WriteBatch(batch);
}

/**
 * Returns the position of a transaction in a block.
 */
uint32_t COmniTransactionDB::FetchTransactionPosition(const uint256& txid)
{
    constexpr uint32_t posInBlock = 999999; // setting an initial arbitrarily high value will ensure transaction is always "last" in event of bug/exploit
    CTxBlockValue value;
    return Read(CTxInfoKey{txid}, value) ? value.posInBlock : posInBlock;
}

/**
 * Returns the reason why a transaction is invalid.
 */
std::string COmniTransactionDB::FetchInvalidReason(const uint256& txid)
{
    int processingResult = -999999;
    CTxBlockValue value;
    if (Read(CTxInfoKey{txid}, value) && value.processResult) {
        processingResult = -int(value.processResult);
    }
    return error_str(processingResult);
}

/**
 *Returns transaction, block and blockTime.
 */
bool COmniTransactionDB::GetTransaction(const uint256& txid, CTransactionRef& tx, int& blockHeight)
{
    CTxInfoValue value;
    if (!Read(CTxInfoKey{txid}, value)) {
        return false;
    }
    blockHeight = value.blockHeight;
    tx = MakeTransactionRef(std::move(value.tx));
    assert(txid == tx->GetHash());
    return true;
}
