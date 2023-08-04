#ifndef BITCOIN_OMNICORE_DBTRANSACTION_H
#define BITCOIN_OMNICORE_DBTRANSACTION_H

#include <omnicore/dbbase.h>

#include <fs.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <stdint.h>

#include <string>
#include <vector>

/** LevelDB based storage for storing Omni transaction validation and position in block data.
 */
class COmniTransactionDB : public CDBBase
{
public:
    COmniTransactionDB(const fs::path& path, bool fWipe);
    virtual ~COmniTransactionDB();

    /** Stores block height, position in block, block time and validation result for a transaction. */
    void RecordTransaction(const CTransaction& tx, int block, uint32_t posInBlock, int processingResult);

    /** Deletes transactions in case of rollback. */
    void DeleteTransactions(const std::set<uint256>& txs);

    /** Stores transaction outputs. */
    void RecordTransactionOuts(const CTransaction& tx);

    /** Returns the position of a transaction in a block. */
    uint32_t FetchTransactionPosition(const uint256& txid);

    /** Returns the reason why a transaction is invalid. */
    std::string FetchInvalidReason(const uint256& txid);

    /** Returns transaction, block and blockTime. */
    bool GetTransaction(const uint256& txid, CTransactionRef& tx, int& block);

    /** Returns transaction out. */
    bool GetTransactionOut(const COutPoint& outpoint, CTxOut& out);
};

namespace mastercore
{
    //! LevelDB based storage for storing Omni transaction validation and position in block data
    extern COmniTransactionDB* pDbTransaction;
}

#endif // BITCOIN_OMNICORE_DBTRANSACTION_H

