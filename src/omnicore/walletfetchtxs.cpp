/**
 * @file walletfetchtxs.cpp
 *
 * The fetch functions provide a sorted list of transaction hashes ordered by block,
 * position in block and position in wallet including STO receipts.
 */

#include <omnicore/walletfetchtxs.h>

#include <omnicore/convert.h>
#include <omnicore/dbstolist.h>
#include <omnicore/dbtransaction.h>
#include <omnicore/dbtxlist.h>
#include <omnicore/log.h>
#include <omnicore/omnicore.h>
#include <omnicore/pending.h>
#include <omnicore/utilsbitcoin.h>

#include <init.h>
#include <interfaces/wallet.h>
#include <validation.h>
#include <sync.h>
#include <tinyformat.h>
#include <index/txindex.h>
#ifdef ENABLE_WALLET
#include <omnicore/walletutils.h>
#include <wallet/wallet.h>
#endif

#include <boost/algorithm/string.hpp>

#include <stdint.h>
#include <list>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mastercore
{

/**
 * Returns an ordered list of Omni transactions including STO receipts that are relevant to the wallet.
 *
 * Ignores order in the wallet (which can be skewed by watch addresses) and utilizes block height and position within block.
 */
std::map<std::string, uint256> FetchWalletOmniTransactions(interfaces::Wallet& iWallet, unsigned int count, int startBlock, int endBlock)
{
    std::map<std::string, uint256> mapResponse;
#ifdef ENABLE_WALLET
    std::set<uint256> seenHashes;
    std::multimap<int64_t, const interfaces::WalletTx*> txOrdered;
    const auto& transactions = iWallet.getWalletTxs();
    for (const auto& transaction : transactions)
        txOrdered.emplace(transaction.order_position, &transaction);

    // Iterate backwards through wallet transactions until we have count items to return:
    for (std::multimap<int64_t, const interfaces::WalletTx*>::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
        const interfaces::WalletTx* pwtx = it->second;
        const uint256& txHash = pwtx->tx->GetHash();
        int blockHeight = 0;
        if (!pDbTransactionList->getValidMPTX(txHash, &blockHeight)) continue;
        if (blockHeight < startBlock || blockHeight > endBlock) continue;
        int blockPosition = pDbTransaction->FetchTransactionPosition(txHash);
        std::string sortKey = strprintf("%06d%010d", blockHeight, blockPosition);
        mapResponse.emplace(sortKey, txHash);
        seenHashes.insert(txHash);
        if (mapResponse.size() >= count) break;
    }

    // Insert STO receipts - receiving an STO has no inbound transaction to the wallet, so we will insert these manually into the response
    auto mySTOReceipts = pDbStoList->getMySTOReceipts({}, startBlock, endBlock, iWallet);
    for (const auto& [blockHeight, txHash] : mySTOReceipts) {
        if (seenHashes.find(txHash) != seenHashes.end()) continue; // an STO may already be in the wallet if we sent it
        int blockPosition = pDbTransaction->FetchTransactionPosition(txHash);
        std::string sortKey = strprintf("%06d%010d", blockHeight, blockPosition);
        mapResponse.emplace(sortKey, txHash);
    }
#endif
    return mapResponse;
}


} // namespace mastercore
