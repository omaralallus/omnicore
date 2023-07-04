#ifndef BITCOIN_OMNICORE_DBSTOLIST_H
#define BITCOIN_OMNICORE_DBSTOLIST_H

#include <omnicore/dbbase.h>

#include <fs.h>
#include <uint256.h>

#include <univalue.h>

#include <stdint.h>

#include <string>
#include <unordered_map>

namespace interfaces {
class Wallet;
} // namespace interfaces

/** LevelDB based storage for STO recipients.
 */
class CMPSTOList : public CDBBase
{
public:
    CMPSTOList(const fs::path& path, bool fWipe);
    virtual ~CMPSTOList();

    void getRecipients(const uint256& txid, const std::string& filterAddress, UniValue* recipientArray, uint64_t* total, uint64_t* numRecipients, interfaces::Wallet* iWallet = nullptr);
    std::unordered_map<int, uint256> getMySTOReceipts(const std::string& filterAddress, int startBlock, int endBlock, interfaces::Wallet& iWallet);

    /**
     * This function deletes records of STO receivers above/equal to a specific block from the STO database.
     *
     * Returns the number of records changed.
     */
    int deleteAboveBlock(int blockNum);
    void printStats();
    void printAll();
    void recordSTOReceive(const std::string&, const uint256&, int, uint32_t, uint64_t);
};

namespace mastercore
{
    //! LevelDB based storage for STO recipients
    extern CMPSTOList* pDbStoList;
}

#endif // BITCOIN_OMNICORE_DBSTOLIST_H
