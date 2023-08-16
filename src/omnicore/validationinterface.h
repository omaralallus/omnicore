#ifndef BITCOIN_OMNICORE_VALIDATIONINTERFACE_H
#define BITCOIN_OMNICORE_VALIDATIONINTERFACE_H

#include <omnicore/dbaddress.h>
#include <omnicore/omnicore.h>

#include <cstdint>
#include <primitives/block.h>
#include <sync.h>
#include <validationinterface.h>

#include <atomic>
#include <vector>

class CBlockIndex;

class CChainIndex
{
    mutable RecursiveMutex cs_chain;
    std::vector<const CBlockIndex*> vChain;

public:
    CChainIndex() = default;
    CChainIndex(const CChainIndex&) = delete;

    /** Returns the index entry for the genesis block of this chain, or nullptr if none. */
    const CBlockIndex* Genesis() const;

    /** Returns the index entry for the tip of this chain, or nullptr if none. */
    const CBlockIndex* Tip() const;

    /** Returns the index entry at a particular height in this chain, or nullptr if no such height exists. */
    const CBlockIndex* operator[](int nHeight) const;

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex* pindex) const;

    /** Find the successor of a block in this chain, or nullptr if the given index is not found or is the tip. */
    const CBlockIndex* Next(const CBlockIndex* pindex) const;

    /** Return the maximal height in the chain. Is equal to chain.Tip() ? chain.Tip()->nHeight : -1. */
    int Height() const;

    /** Set/initialize a chain with a given tip. */
    void SetTip(const CBlockIndex* pindex);
};

class COmniValidationInterface : public CValidationInterface {
    CChainIndex chain;
    std::set<uint256> txsToDelete;
    bool disconnectInitiated = false;
    std::atomic_bool initialBlockDownload = false, processingBlock = false;
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndexToDelete;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue>> spentIndexToUdpdate;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> addressUnspentIndexToUpdate;

    void SyncToTip(int loadedBlock);
    void RewindDBsState(int nHeight);
    void BeginProcessTx(const CBlockIndex* pindex);
    void EndProcessTx(const CBlockIndex* pindex, unsigned int countMP);

public:
    /**
     * Initialize tip and sync to it if needed
     */
    void Init(const CBlockIndex* pindex, int loadedBlock);
    /**
     * Notifies listeners when the block chain tip advances.
     *
     * When multiple blocks are connected at once, UpdatedBlockTip will be called on the final tip
     * but may not be called on every intermediate tip. If the latter behavior is desired,
     * subscribe to BlockConnected() instead.
     *
     * Called on a background thread.
     */
    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *, bool fInitialDownload) override;
    /**
     * Notifies listeners of a transaction having been added to mempool.
     *
     * Called on a background thread.
     */
    void TransactionAddedToMempool(const CTransactionRef& tx, uint64_t mempool_sequence) override;
    /**
     * Notifies listeners of a transaction leaving mempool.
     * - TransactionRemovedFromMempool(tx1 from block A)
     * - TransactionRemovedFromMempool(tx2 from block A)
     * - TransactionRemovedFromMempool(tx1 from block B)
     * - TransactionRemovedFromMempool(tx2 from block B)
     * - BlockConnected(A)
     * - BlockConnected(B)
     *
     * Called on a background thread.
     */
    void TransactionRemovedFromMempool(const CTransactionRef& tx, MemPoolRemovalReason reason, uint64_t mempool_sequence) override;
    /**
     * Notifies listeners of a block being connected.
     * Called on a background thread.
     */
    void BlockConnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex *pindex) override;
    /**
     * Notifies listeners of a block being disconnected
     *
     * Called on a background thread.
     */
    void BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex) override;
    /**
     * Notifies listeners of the new active block chain on-disk.
     *
     * Called on a background thread.
     */
    void ChainStateFlushed(const CBlockLocator&) override {}
    /**
     * Notifies listeners of a block validation result.
     * If the provided BlockValidationState IsValid, the provided block
     * is guaranteed to be the current best block at the time the
     * callback was generated (not necessarily now)
     */
    void BlockChecked(const CBlock&, const BlockValidationState&) override {}
    /**
     * Notifies listeners that a block which builds directly on our current tip
     * has been received and connected to the headers tree, though not validated yet */
    void NewPoWValidBlock(const CBlockIndex*, const std::shared_ptr<const CBlock>&) override {}

    /**
     * Returns tip height
     */
    int LastBlockHeight() const;

    /**
     * Returns tip time
     */
    uint32_t LastBlockTime() const;

    /**
     * Returns active chain
     */
    const CChainIndex& GetActiveChain() const;

    /**
     * Returns whether node is in initial block download
     */
    bool IsInitialBlockDownload() const;

    /**
     * Returns whether interface is processing a block
     */
    bool IsProcessingBlock() const;
};

extern std::shared_ptr<COmniValidationInterface> omniValidationInterface;

#endif // BITCOIN_OMNICORE_VALIDATIONINTERFACE_H
