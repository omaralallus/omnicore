
#include <atomic>

#include <chain.h>
#include <key_io.h>
#include <node/interface_ui.h>
#include <shutdown.h>
#include <undo.h>
#include <validation.h>

#include <omnicore/activation.h>
#include <omnicore/consensushash.h>
#include <omnicore/convert.h>
#include <omnicore/dbaddress.h>
#include <omnicore/dbbase.h>
#include <omnicore/dbfees.h>
#include <omnicore/dbspinfo.h>
#include <omnicore/dbstolist.h>
#include <omnicore/dbtradelist.h>
#include <omnicore/dbtransaction.h>
#include <omnicore/dbtxlist.h>
#include <omnicore/dex.h>
#include <omnicore/log.h>
#include <omnicore/notifications.h>
#include <omnicore/mempool.h>
#include <omnicore/pending.h>
#include <omnicore/rules.h>
#include <omnicore/seedblocks.h>
#include <omnicore/sp.h>
#include <omnicore/tx.h>
#include <omnicore/utilsbitcoin.h>
#include <omnicore/validationinterface.h>

using namespace mastercore;

const CBlockIndex* CChainIndex::Genesis() const
{
    LOCK(cs_chain);
    return !vChain.empty() ? vChain.front() : nullptr;
}

const CBlockIndex* CChainIndex::Tip() const
{
    LOCK(cs_chain);
    return !vChain.empty() ? vChain.back() : nullptr;
}

const CBlockIndex* CChainIndex::operator[](int nHeight) const
{
    LOCK(cs_chain);
    return nHeight < 0 || nHeight > Height() ? nullptr : vChain[nHeight];
}

bool CChainIndex::Contains(const CBlockIndex* pindex) const
{
    LOCK(cs_chain);
    return (*this)[pindex->nHeight] == pindex;
}

const CBlockIndex* CChainIndex::Next(const CBlockIndex* pindex) const
{
    LOCK(cs_chain);
    return Contains(pindex) ? (*this)[pindex->nHeight + 1] : nullptr;
}

int CChainIndex::Height() const
{
    LOCK(cs_chain);
    return int(vChain.size()) - 1;
}

void CChainIndex::SetTip(const CBlockIndex* pindex)
{
    LOCK(cs_chain);
    vChain.resize(pindex->nHeight + 1);
    while (pindex && !Contains(pindex)) {
        vChain[pindex->nHeight] = pindex;
        pindex = pindex->pprev;
    }
}

/**
 * Reports the progress of the initial transaction scanning.
 *
 * The progress is printed to the console, written to the debug log file, and
 * the RPC status, as well as the splash screen progress label, are updated.
 *
 * @see msc_initial_scan()
 */
class ProgressReporter
{
private:
    const CBlockIndex* m_pblockFirst;
    const CBlockIndex* m_pblockLast;
    const int64_t m_timeStart;

    /** Returns the estimated remaining time in milliseconds. */
    int64_t estimateRemainingTime(double progress) const
    {
        int64_t timeSinceStart = GetTimeMillis() - m_timeStart;

        double timeRemaining = 3600000.0; // 1 hour
        if (progress > 0.0 && timeSinceStart > 0) {
            timeRemaining = (100.0 - progress) / progress * timeSinceStart;
        }

        return static_cast<int64_t>(timeRemaining);
    }

    /** Converts a time span to a human readable string. */
    std::string remainingTimeAsString(int64_t remainingTime) const
    {
        int64_t secondsTotal = 0.001 * remainingTime;
        int64_t hours = secondsTotal / 3600;
        int64_t minutes = (secondsTotal / 60) % 60;
        int64_t seconds = secondsTotal % 60;

        if (hours > 0) {
            return strprintf("%d:%02d:%02d hours", hours, minutes, seconds);
        } else if (minutes > 0) {
            return strprintf("%d:%02d minutes", minutes, seconds);
        } else {
            return strprintf("%d seconds", seconds);
        }
    }

public:
    ProgressReporter(const CBlockIndex* pblockFirst, const CBlockIndex* pblockLast)
        : m_pblockFirst(pblockFirst), m_pblockLast(pblockLast), m_timeStart(GetTimeMillis())
    {
    }

    /** Prints the current progress to the console and notifies the UI. */
    void update(const CBlockIndex* pblockNow) const
    {
        int nLastBlock = m_pblockLast->nHeight;
        int nCurrentBlock = pblockNow->nHeight;
        unsigned int nFirst = m_pblockFirst->nChainTx;
        unsigned int nCurrent = pblockNow->nChainTx;
        unsigned int nLast = m_pblockLast->nChainTx;

        double dProgress = 100.0 * (nCurrent - nFirst) / (nLast - nFirst);
        int64_t nRemainingTime = estimateRemainingTime(dProgress);

        std::string strProgress = strprintf(
                "Still scanning.. at block %d of %d. Progress: %.2f %%, about %s remaining..\n",
                nCurrentBlock, nLastBlock, dProgress, remainingTimeAsString(nRemainingTime));
        std::string strProgressUI = strprintf(
                "Still scanning.. at block %d of %d.\nProgress: %.2f %% (about %s remaining)",
                nCurrentBlock, nLastBlock, dProgress, remainingTimeAsString(nRemainingTime));

        PrintToConsole(strProgress);
        uiInterface.InitMessage(strProgressUI);
    }
};

void COmniValidationInterface::RewindDBsState(const CBlockIndex* pindex)
{
    // Check if any freeze related transactions would be rolled back - if so wipe the state and startclean
    auto nHeight = pindex->nHeight - 1;
    bool reorgContainsFreeze = pDbTransactionList->CheckForFreezeTxs(nHeight);
    if (reorgContainsFreeze) {
        PrintToConsole("Reorganization containing freeze related transactions detected, forcing a rescan...\n");
    }

    int best_state_block = 0;
    if (reorgContainsFreeze
    || (best_state_block = LoadMostRelevantInMemoryState()) < 0
    || best_state_block > nHeight) {
        extern void clear_all_state();
        clear_all_state();
        best_state_block = -1; // start from genesis block
    } else {
        auto block = best_state_block + 1; // revert to block inclusive
        auto pblock = std::make_shared<CBlock>();
        // sync txsToDelete to best_state_block
        while (pindex && pindex->nHeight >= block) {
            if (!node::ReadBlockFromDisk(*pblock, pindex, Params().GetConsensus())) {
                throw std::runtime_error("RewindDBsState: Cannot read block to rewind");
            }
            BlockDisconnected(pblock, pindex);
            disconnectInitiated = false;
            pindex = pindex->pprev;
        }
        pDbStoList->deleteAboveBlock(block);
        pDbSpInfo->deleteSPAboveBlock(block);
        pDbFeeCache->RollBackCache(block);
        pDbFeeHistory->RollBackHistory(block);
        pDbNFT->RollBackAboveBlock(block);
        pDbTradeList->deleteTransactions(txsToDelete, block);
        pDbTransactionList->deleteTransactions(txsToDelete, block);
        pDbTransaction->DeleteTransactions(txsToDelete, inputsToRestore, block);
        if (fAddressIndex) {
            pDbAddress->UpdateSpentIndex(spentIndexToUdpdate);
            pDbAddress->EraseAddressIndex(addressIndexToDelete);
            pDbAddress->UpdateAddressUnspentIndex(addressUnspentIndexToUpdate);
        }
    }

    txsToDelete.clear();
    inputsToRestore.clear();
    spentIndexToUdpdate.clear();
    addressIndexToDelete.clear();
    addressUnspentIndexToUpdate.clear();

    // clear the global wallet property list, perform a forced wallet update and tell the UI that state is no longer valid, and UI views need to be reinit
    global_wallet_property_list.clear();
    CheckWalletUpdate();
    uiInterface.OmniStateInvalidated();
    SyncToTip(best_state_block);
}

void COmniValidationInterface::SyncToTip(int loadedBlock)
{
    int nTimeBetweenProgressReports = gArgs.GetIntArg("-omniprogressfrequency", 30);  // seconds
    int64_t nNow = GetTime();
    unsigned int nTxsTotal = 0;
    unsigned int nTxsFoundTotal = 0;

    const CBlockIndex* pFirstBlock = chain[loadedBlock + 1];
    const CBlockIndex* pLastBlock = chain.Tip();

    if (!pFirstBlock || !pLastBlock) return;

    auto nFirstBlock = pFirstBlock->nHeight;
    auto nLastBlock = pLastBlock->nHeight;

    if (node::fPruneMode && nLastBlock - nFirstBlock >= MIN_BLOCKS_TO_KEEP) {
        throw std::runtime_error("Cannot recover in prune mode, needs restart with -reindex");
    }

    PrintToConsole("Scanning for transactions in block %d to block %d..\n", nFirstBlock, nLastBlock);

    ProgressReporter progressReporter(pFirstBlock, pLastBlock);

    int nBlock = 0;
    CCoinsViewCacheOnly view;
    // check if using seed block filter should be disabled
    bool seedBlockFilterEnabled = gArgs.GetBoolArg("-omniseedblockfilter", MainNet());

    for (nBlock = nFirstBlock; nBlock <= nLastBlock; ++nBlock)
    {
        if (ShutdownRequested()) {
            PrintToLog("Shutdown requested, stop scan at block %d of %d\n", nBlock, nLastBlock);
            break;
        }

        const CBlockIndex* pblockindex = chain[nBlock];
        if (!pblockindex) break;

        if (msc_debug_exo) {
            auto strBlockHash = pblockindex->GetBlockHash().GetHex();
            PrintToLog("%s(%d; max=%d):%s\n", __func__, nBlock, nLastBlock, strBlockHash);
        }

        if (GetTime() >= nNow + nTimeBetweenProgressReports) {
            progressReporter.update(pblockindex);
            nNow = GetTime();
        }

        unsigned int nTxNum = 0;
        unsigned int nTxsFoundInBlock = 0;
        BeginProcessTx(pblockindex);

        if (!seedBlockFilterEnabled || !SkipBlock(nBlock)) {
            CBlock block;
            if (!node::ReadBlockFromDisk(block, pblockindex, Params().GetConsensus())) break;

            for(const auto& tx : block.vtx) {
                if (ProcessTransaction(view, *tx, nTxNum, pblockindex)) ++nTxsFoundInBlock;
                ++nTxNum;
            }
        }

        nTxsFoundTotal += nTxsFoundInBlock;
        nTxsTotal += nTxNum;
        EndProcessTx(pblockindex, nTxsFoundInBlock);
    }

    if (nBlock < nLastBlock) {
        PrintToConsole("Scan stopped early at block %d of block %d\n", nBlock, nLastBlock);
    }

    PrintToConsole("%d new transactions processed, %d meta transactions found\n", nTxsTotal, nTxsFoundTotal);
}

/**
 * Scans the blockchain for meta transactions.
 *
 * It scans the blockchain, starting at the given block index, to the current
 * tip, much like as if new block were arriving and being processed on the fly.
 *
 * Every 30 seconds the progress of the scan is reported.
 *
 * In case the current block being processed is not part of the active chain, or
 * if a block could not be retrieved from the disk, then the scan stops early.
 * Likewise, global shutdown requests are honored, and stop the scan progress.
 *
 * @param loadedBlock[in]  The index of the first block to scan
 * @return An exit code, indicating success or failure
 */
void COmniValidationInterface::Init(const CBlockIndex* pindex, int loadedBlock)
{
    disconnectInitiated = false;
    UpdatedBlockTip(pindex, nullptr, true);
    SyncToTip(loadedBlock);
}

void COmniValidationInterface::BeginProcessTx(const CBlockIndex* pindex)
{
    if (disconnectInitiated) {
        chain.SetTip(pindex->pprev);
        disconnectInitiated = false;
        RewindDBsState(pindex);
    }

    // handle any features that go live with this block
    CheckLiveActivations(pindex->nHeight);
    eraseExpiredCrowdsale(pindex);
}

// it performs cleanup and other functions
void COmniValidationInterface::EndProcessTx(const CBlockIndex* pindex, unsigned int countMP)
{
    // for every new received block must do:
    // 1) remove expired entries from the accept list (per spec accept entries are
    //    valid until their blocklimit expiration; because the customer can keep
    //    paying BTC for the offer in several installments)
    // 2) update the amount in the Exodus address
    int64_t devmsc = 0;
    auto nBlockNow = pindex->nHeight;
    unsigned int how_many_erased = eraseExpiredAccepts(nBlockNow);

    if (how_many_erased) {
        PrintToLog("%s(%d); erased %u accepts this block, line %d, file: %s\n",
                    __func__, how_many_erased, nBlockNow, __LINE__, __FILE__);
    }

    extern int64_t calculate_and_update_devmsc(unsigned int nTime, int block);
    // calculate devmsc as of this block and update the Exodus' balance
    devmsc = calculate_and_update_devmsc(pindex->GetBlockTime(), nBlockNow);

    if (msc_debug_exo) {
        auto exodus = ExodusAddress();
        int64_t balance = GetTokenBalance(EncodeDestination(exodus), OMNI_PROPERTY_MSC, BALANCE);
        PrintToLog("devmsc for block %d: %d, Exodus balance: %d\n", nBlockNow, devmsc, FormatDivisibleMP(balance));
    }

    // check the alert status, do we need to do anything else here?
    CheckExpiredAlerts(nBlockNow, pindex->GetBlockTime());

    // check that pending transactions are still in the mempool
    PendingCheck();

    // transactions were found in the block, signal the UI accordingly
    if (countMP > 0) CheckWalletUpdate();

    // calculate and print a consensus hash if required
    if (ShouldConsensusHashBlock(nBlockNow)) {
        uint256 consensusHash = GetConsensusHash();
        PrintToLog("Consensus hash for block %d: %s\n", nBlockNow, consensusHash.GetHex());
    }

    // request nftdb sanity check
    bool sanityCheck = true;
    pDbNFT->WriteBlockCache(nBlockNow, sanityCheck);

    // request checkpoint verification
    bool checkpointValid = VerifyCheckpoint(nBlockNow, pindex->GetBlockHash());
    if (!checkpointValid) {
        // failed checkpoint, can't be trusted to provide valid data - shutdown client
        const std::string& msg = strprintf(
            "Shutting down due to failed checkpoint for block %d (hash %s). "
            "Please restart with -startclean flag and if this doesn't work, please reach out to the support.\n",
            nBlockNow, pindex->GetBlockHash().GetHex());
        PrintToLog(msg);
        MayAbortNode(msg);
    }

    if (checkpointValid && nBlockNow >= ConsensusParams().GENESIS_BLOCK) {
        // save out the state after this block
        if (IsPersistenceEnabled(nBlockNow)) {
            PersistInMemoryState(pindex);
        }
    }
}

static uint160 Uint160(const uint256& uint_256)
{
    uint160 uint_160;
    std::copy(uint_256.begin(), uint_256.begin() + uint160::size(), uint_160.begin());
    return uint_160;
}

static CScript GetScriptFromIndex(size_t type, const uint256 &hash)
{
    switch (type) {
        case 1: return GetScriptForDestination(PKHash(Uint160(hash)));
        case 2: return GetScriptForDestination(ScriptHash(Uint160(hash)));
        case 3: return GetScriptForDestination(WitnessV0ScriptHash(hash));
        case 4: return GetScriptForDestination(WitnessV0KeyHash(Uint160(hash)));
        case 5: return GetScriptForDestination(WitnessV1Taproot(XOnlyPubKey{hash}));
    }
    return {};
}

void COmniValidationInterface::UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *, bool fInitialDownload)
{
    chain.SetTip(pindexNew);
    initialBlockDownload.store(fInitialDownload, std::memory_order_release);
}

void COmniValidationInterface::TransactionAddedToMempool(const CTransactionRef& tx, uint64_t mempool_sequence)
{
    AddTransactionToMempool(tx);
}

void COmniValidationInterface::TransactionRemovedFromMempool(const CTransactionRef& tx, MemPoolRemovalReason reason, uint64_t mempool_sequence)
{
    RemoveTransactionFromMempool(tx);
}

void COmniValidationInterface::BlockConnected(const std::shared_ptr<const CBlock> &block, const CBlockIndex *pindex)
{
    LOCK(cs_tally);

    processingBlock.store(true, std::memory_order_release);

    BeginProcessTx(pindex);

    chain.SetTip(pindex);

    //! transaction position within the block
    unsigned int nTxIdx = 0;

    //! number of meta transactions found
    unsigned int nNumMetaTxs = 0;

    CCoinsViewCacheOnly view;
    for (auto& tx : block->vtx) {
        //! Omni Core: new confirmed transaction notification
        if (ProcessTransaction(view, *tx, nTxIdx, pindex)) {
            PrintToLog("%s: new confirmed transaction [height: %d, idx: %u]\n", __func__, pindex->nHeight, nTxIdx);
            ++nNumMetaTxs;
        }
        ++nTxIdx;
    }

    //! Omni Core: end of block connect notification
    if (nNumMetaTxs)
        PrintToLog("%s: block connect end [new height: %d, found: %u txs]\n", __func__, pindex->nHeight, nNumMetaTxs);

    EndProcessTx(pindex, nNumMetaTxs);

    if (fAddressIndex) {
        std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
        std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue>> addressUnspentIndex;
        std::vector<std::pair<CSpentIndexKey, CSpentIndexValue>> spentIndex;

        for (unsigned int i = 0; i < block->vtx.size(); i++) {
            const CTransaction &tx = *(block->vtx[i]);
            for (unsigned int j = 0; j < tx.vin.size(); j++) {
                const CTxIn& input = tx.vin[j];
                const CTxOut& prevout = view.GetOutputFor(input);

                if (auto result = ScriptToUint(prevout.scriptPubKey)) {
                    auto& [index, address] = *result;
                    addressIndex.emplace_back(CAddressIndexKey{index, address, pindex->nHeight, i, tx.GetHash(), j, true}, prevout.nValue * -1);

                    // remove address from unspent index
                    addressUnspentIndex.emplace_back(CAddressUnspentKey{index, address, input.prevout.hash, input.prevout.n}, CAddressUnspentValue{});
                    spentIndex.emplace_back(CSpentIndexKey{input.prevout.hash, input.prevout.n}, CSpentIndexValue{tx.GetHash(), j, pindex->nHeight, prevout.nValue, index, address});
                }
            }

            for (unsigned int k = 0; k < tx.vout.size(); k++) {
                const CTxOut &out = tx.vout[k];
                const bool isTxCoinBase = tx.IsCoinBase();

                if (auto result = ScriptToUint(out.scriptPubKey)) {
                    auto& [index, address] = *result;
                    // record receiving activity
                    addressIndex.emplace_back(CAddressIndexKey{index, address, pindex->nHeight, i, tx.GetHash(), k}, out.nValue);
                    // record unspent output
                    addressUnspentIndex.emplace_back(CAddressUnspentKey{index, address, tx.GetHash(), k}, CAddressUnspentValue{out.nValue, out.scriptPubKey, pindex->nHeight, isTxCoinBase});
                }
            }
        }

        if (!pDbAddress->WriteAddressIndex(addressIndex))
            PrintToLog("%s: Failed to write address index\n", __func__);

        if (!pDbAddress->UpdateAddressUnspentIndex(addressUnspentIndex))
            PrintToLog("%s: Failed to write address unspent index\n", __func__);

        if (!pDbAddress->UpdateSpentIndex(spentIndex))
            PrintToLog("%s: Failed to write transaction index\n", __func__);

        unsigned int logicalTS = pindex->nTime;
        unsigned int prevLogicalTS = 0;

        // retrieve logical timestamp of the previous block
        if (pindex->pprev && !pDbAddress->ReadTimestampBlockIndex(pindex->pprev->GetBlockHash(), prevLogicalTS))
            PrintToLog("%s: Failed to read previous block's logical timestamp\n", __func__);

        if (logicalTS <= prevLogicalTS) {
            logicalTS = prevLogicalTS + 1;
            PrintToLog("%s: Previous logical timestamp is newer Actual[%d] prevLogical[%d] Logical[%d]\n", __func__, pindex->nTime, prevLogicalTS, logicalTS);
        }

        if (!pDbAddress->WriteTimestampIndex(CTimestampIndexKey{logicalTS, pindex->GetBlockHash()}))
            PrintToLog("%s: Failed to write timestamp index\n", __func__);

        if (!pDbAddress->WriteTimestampBlockIndex(pindex->GetBlockHash(), logicalTS))
            PrintToLog("%s: Failed to write blockhash index\n", __func__);
    }

    processingBlock.store(false, std::memory_order_release);

    for (auto& tx : block->vtx)
        RemoveTransactionFromMempool(tx);
}

void COmniValidationInterface::BlockDisconnected(const std::shared_ptr<const CBlock>& block, const CBlockIndex* pindex)
{
    //! Omni Core: begin block disconnect notification
    PrintToLog("%s Omni Core handler: height: %d\n", __func__, pindex->nHeight);

    LOCK(cs_tally);

    for (auto& tx : block->vtx) {
        txsToDelete.insert(tx->GetHash());
    }

    CBlockUndo blockUndo;
    if (node::UndoReadFromDisk(blockUndo, pindex)) {
        for (auto i = 1u; i < block->vtx.size(); i++) {
            const CTransaction &tx = *(block->vtx[i]);
            CTxUndo &txundo = blockUndo.vtxundo.at(i - 1);
            for (auto j = tx.vin.size(); j-- > 0;) {
                inputsToRestore.emplace(tx.vin[j].prevout, txundo.vprevout.at(j).out);
            }
        }
    }

    disconnectInitiated = true;

    if (!fAddressIndex) {
        return;
    }

    for (int i = block->vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = *(block->vtx[i]);
        for (int k = tx.vout.size() - 1; k >= 0; k--) {
            const CTxOut &out = tx.vout[k];
            if (auto result = ScriptToUint(out.scriptPubKey)) {
                auto& [index, address] = *result;
                // undo receiving activity
                addressIndexToDelete.emplace_back(CAddressIndexKey{index, address, pindex->nHeight, unsigned(i), tx.GetHash(), unsigned(k), false}, 0);
                // undo unspent index
                addressUnspentIndexToUpdate.emplace_back(CAddressUnspentKey{index, address, tx.GetHash(), unsigned(k)}, CAddressUnspentValue{});
            }
        }
        const bool isTxCoinBase = tx.IsCoinBase();
        for (int j = tx.vin.size() - 1; j >= 0; j--) {
            const CTxIn& input = tx.vin[j];
            CSpentIndexValue spend;
            if (pDbAddress->ReadSpentIndex({input.prevout.hash, input.prevout.n}, spend)) {
                // undo spending activity
                addressIndexToDelete.emplace_back(CAddressIndexKey{spend.addressType, spend.addressHash, pindex->nHeight, unsigned(i), tx.GetHash(), unsigned(j), true}, 0);
                // restore unspent index
                addressUnspentIndexToUpdate.emplace_back(CAddressUnspentKey{spend.addressType, spend.addressHash, input.prevout.hash, input.prevout.n},
                                                        CAddressUnspentValue{spend.satoshis, GetScriptFromIndex(spend.addressType, spend.addressHash), spend.blockHeight, isTxCoinBase});
            }
            spentIndexToUdpdate.emplace_back(CSpentIndexKey{input.prevout.hash, input.prevout.n}, CSpentIndexValue{});
        }
    }
}

int COmniValidationInterface::LastBlockHeight() const
{
    auto tip = chain.Tip();
    return tip ? tip->nHeight : 0;
}

uint32_t COmniValidationInterface::LastBlockTime() const
{
    auto tip = chain.Tip();
    return tip ? tip->nTime : 0;
}

const CChainIndex& COmniValidationInterface::GetActiveChain() const
{
    return chain;
}

bool COmniValidationInterface::IsInitialBlockDownload() const
{
    return initialBlockDownload.load(std::memory_order_relaxed);
}

bool COmniValidationInterface::IsProcessingBlock() const
{
    return processingBlock.load(std::memory_order_relaxed);
}

std::shared_ptr<COmniValidationInterface> omniValidationInterface;
