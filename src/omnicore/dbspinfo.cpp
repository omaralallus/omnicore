#include <omnicore/dbspinfo.h>

#include <omnicore/dbbase.h>
#include <omnicore/log.h>

#include <base58.h>
#include <clientversion.h>
#include <fs.h>
#include <key_io.h>
#include <serialize.h>
#include <streams.h>
#include <tinyformat.h>
#include <uint256.h>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

CMPSPInfo::Entry::Entry()
  : prop_type(0), prev_prop_id(0), num_tokens(0), property_desired(0),
    deadline(0), early_bird(0), percentage(0),
    close_early(false), max_tokens(false), missedTokens(0), timeclosed(0),
    fixed(false), manual(false), unique(false) {}

bool CMPSPInfo::Entry::isDivisible() const
{
    switch (prop_type) {
        case MSC_PROPERTY_TYPE_DIVISIBLE:
        case MSC_PROPERTY_TYPE_DIVISIBLE_REPLACING:
        case MSC_PROPERTY_TYPE_DIVISIBLE_APPENDING:
            return true;
    }
    return false;
}

void CMPSPInfo::Entry::print() const
{
    PrintToConsole("%s:%s(Fixed=%s,Divisible=%s):%d:%s/%s, %s %s\n",
            issuer,
            name,
            fixed ? "Yes" : "No",
            manual ? "Yes" : "No",
            unique ? "Yes" : "No",
            isDivisible() ? "Yes" : "No",
            num_tokens,
            category, subcategory, url, data);
}

/**
 * Stores a new issuer in the DB.
 *
 * @param block  The block of the update
 * @param idx    The position within the block of the update
 * @param newIssuer  The new issuer
 */
void CMPSPInfo::Entry::updateIssuer(int block, int idx, const std::string& newIssuer)
{
    historicalIssuers[std::make_pair(block, idx)] = newIssuer;
}

/**
 * Returns the issuer for the given block.
 *
 * @param block  The block to check
 * @return The issuer of that block
 */
std::string CMPSPInfo::Entry::getIssuer(int block) const
{
    auto it = historicalIssuers.upper_bound(std::make_pair(block, INT_MAX));
    return it != historicalIssuers.begin() ? (--it)->second : issuer;
}

/**
 * Stores a new delegate in the DB.
 *
 * @param block  The block of the update
 * @param idx    The position within the block of the update
 * @param newIssuer  The new delegate
 */
void CMPSPInfo::Entry::addDelegate(int block, int idx, const std::string& newIssuer)
{
    historicalDelegates[std::make_pair(block, idx)] = newIssuer;
}

/**
 * Clears the delegate in the DB.
 *
 * @param block  The block of the update
 * @param idx    The position within the block of the update
 */
void CMPSPInfo::Entry::removeDelegate(int block, int idx)
{
    historicalDelegates[std::make_pair(block, idx)] = "";
}

/**
 * Returns the delegate for the given block, if there is one.
 * If not, return an emptry string.
 *
 * @param block  The block to check
 * @return The issuer of that block
 */
std::string CMPSPInfo::Entry::getDelegate(int block) const
{
    auto it = historicalDelegates.upper_bound(std::make_pair(block, INT_MAX));
    return it != historicalDelegates.begin() ? (--it)->second : delegate;
}

static CMPSPInfo::Entry CreateOmniToken()
{
    CMPSPInfo::Entry omni;
    // special cases for constant SPs OMN and TOMN
    omni.issuer = EncodeDestination(ExodusAddress());
    omni.updateIssuer(0, 0, omni.issuer);
    omni.prop_type = MSC_PROPERTY_TYPE_DIVISIBLE;
    omni.num_tokens = 700000;
    omni.category = "N/A";
    omni.subcategory = "N/A";
    omni.name = "Omni tokens";
    omni.url = "http://www.omnilayer.org";
    omni.data = "Omni tokens serve as the binding between Bitcoin, smart properties and contracts created on the Omni Layer.";
    return omni;
}

static CMPSPInfo::Entry CreateTestOmniToken()
{
    CMPSPInfo::Entry tomni;
    tomni.issuer = EncodeDestination(ExodusAddress());
    tomni.updateIssuer(0, 0, tomni.issuer);
    tomni.prop_type = MSC_PROPERTY_TYPE_DIVISIBLE;
    tomni.num_tokens = 700000;
    tomni.category = "N/A";
    tomni.subcategory = "N/A";
    tomni.name = "Test Omni tokens";
    tomni.url = "http://www.omnilayer.org";
    tomni.data = "Test Omni tokens serve as the binding between Bitcoin, smart properties and contracts created on the Omni Layer.";
    return tomni;
}

CMPSPInfo::CMPSPInfo(const fs::path& path, bool fWipe) : implied_omni(CreateOmniToken()), implied_tomni(CreateTestOmniToken())
{
    leveldb::Status status = Open(path, fWipe);
    PrintToConsole("Loading smart property database: %s\n", status.ToString());
    init();
}

CMPSPInfo::~CMPSPInfo()
{
    if (msc_debug_persistence) PrintToLog("CMPSPInfo closed\n");
}

void CMPSPInfo::Clear()
{
    // wipe database via parent class
    CDBBase::Clear();
    // reset "next property identifiers"
    init();
}

void CMPSPInfo::init(uint32_t nextSPID, uint32_t nextTestSPID)
{
    next_spid = nextSPID;
    next_test_spid = nextTestSPID;
}

uint32_t CMPSPInfo::peekNextSPID(uint8_t ecosystem) const
{
    uint32_t nextId = 0;

    switch (ecosystem) {
        case OMNI_PROPERTY_MSC: // Main ecosystem, MSC: 1, TMSC: 2, First available SP = 3
            nextId = next_spid;
            break;
        case OMNI_PROPERTY_TMSC: // Test ecosystem, same as above with high bit set
            nextId = next_test_spid;
            break;
        default: // Non-standard ecosystem, ID's start at 0
            nextId = 0;
    }

    return nextId;
}

struct CUpdatePropertyKey {
    static constexpr uint8_t prefix = 's';
    uint32_t propertyId;

    SERIALIZE_METHODS(CUpdatePropertyKey, obj) {
        READWRITE(obj.propertyId);
    }
};

struct CDelegatePropertyKey {
    static constexpr uint8_t prefix = 'd';
    uint32_t propertyId;

    SERIALIZE_METHODS(CDelegatePropertyKey, obj) {
        READWRITE(obj.propertyId);
    }
};

struct CUniquePropertyKey {
    static constexpr uint8_t prefix = 'u';
    uint32_t propertyId;

    SERIALIZE_METHODS(CUniquePropertyKey, obj) {
        READWRITE(obj.propertyId);
    }
};

struct CHistoricalPropertyKey {
    static constexpr uint8_t prefix = 'b';
    const uint256& blockHash;
    const uint32_t& propertyId;

    SERIALIZE_METHODS(CHistoricalPropertyKey, obj) {
        READWRITE(obj.blockHash);
        READWRITE(obj.propertyId);
    }
};

struct CLookupTxKey {
    static constexpr uint8_t prefix = 't';
    const uint256& txid;

    SERIALIZE_METHODS(CLookupTxKey, obj) {
        READWRITE(obj.txid);
    }
};

struct CDelegateValue {
    CRef<std::string> delegate;
    CRef<std::map<std::pair<int, int>, std::string>> historicalDelegates;

    SERIALIZE_METHODS(CDelegateValue, obj) {
        READWRITE(obj.delegate);
        READWRITE(obj.historicalDelegates);
    }
};

bool CMPSPInfo::updateSP(uint32_t propertyId, const Entry& info)
{
    // cannot update implied SP
    if (OMNI_PROPERTY_MSC == propertyId || OMNI_PROPERTY_TMSC == propertyId) {
        return false;
    }

    // DB key for property entry
    auto sKey = KeyToString(CUpdatePropertyKey{propertyId});

    // DB value for property entry
    auto sValue = ValueToString(info);

    leveldb::WriteBatch batch;
    std::string strSpPrevValue;

    // if a value exists move it to the old key
    if (Read(sKey, strSpPrevValue)) {
        // DB key for historical property entry
        auto slPrevKey = KeyToString(CHistoricalPropertyKey{info.update_block, propertyId});
        batch.Put(slPrevKey, strSpPrevValue);
    }
    batch.Put(sKey, sValue);

    // Update delegate info if set
    if (!info.historicalDelegates.empty()) {
        batch.Put(KeyToString(CDelegatePropertyKey{propertyId}), ValueToString(CDelegateValue{info.delegate, info.historicalDelegates}));
    }

    if (!WriteBatch(batch)) {
        PrintToLog("%s(): ERROR for SP %d: NOK\n", __func__, propertyId);
        return false;
    }

    PrintToLog("%s(): updated entry for SP %d successfully\n", __func__, propertyId);
    return true;
}


uint32_t CMPSPInfo::putSP(uint8_t ecosystem, const Entry& info)
{
    uint32_t propertyId = 0;
    switch (ecosystem) {
        case OMNI_PROPERTY_MSC: // Main ecosystem, MSC: 1, TMSC: 2, First available SP = 3
            propertyId = next_spid++;
            break;
        case OMNI_PROPERTY_TMSC: // Test ecosystem, same as above with high bit set
            propertyId = next_test_spid++;
            break;
        default: // Non-standard ecosystem, ID's start at 0
            propertyId = 0;
    }

    // DB key for property entry
    auto sKey = KeyToString(CUpdatePropertyKey{propertyId});

    // DB value for property entry
    auto sValue = ValueToString(info);

    // DB key for identifier lookup entry
    auto lKey = KeyToString(CLookupTxKey{info.txid});

    // DB value for identifier
    auto lValue = ValueToString(propertyId);

    // sanity checking
    std::string existingEntry;
    if (Read(sKey, existingEntry) && sValue.compare(existingEntry) != 0) {
        std::string strError = strprintf("writing SP %d to DB, when a different SP already exists for that identifier", propertyId);
        PrintToLog("%s() ERROR: %s\n", __func__, strError);
    } else if (Read(lKey, existingEntry) && lValue.compare(existingEntry) != 0) {
        std::string strError = strprintf("writing index txid %s : SP %d is overwriting a different value", info.txid.ToString(), propertyId);
        PrintToLog("%s() ERROR: %s\n", __func__, strError);
    }

    // Create and check unique field
    auto uKey = KeyToString(CUniquePropertyKey{propertyId});
    if (info.unique) {
        // sanity checking
        if (Read(uKey, existingEntry) && existingEntry != strprintf("%d", info.unique)) {
            std::string strError = strprintf("writing SP %d unique field to DB, when a different SP already exists for that identifier", propertyId);
            PrintToLog("%s() ERROR: %s\n", __func__, strError);
        }
    }

    // atomically write both the the SP and the index to the database
    leveldb::WriteBatch batch;
    batch.Put(sKey, sValue);
    batch.Put(lKey, lValue);

    if (info.unique) {
        batch.Put(uKey, ValueToString(info.unique));
    }

    if (!WriteBatch(batch)) {
        PrintToLog("%s(): ERROR for SP %d: NOK\n", __func__, propertyId);
    }

    return propertyId;
}

bool CMPSPInfo::getSP(uint32_t propertyId, Entry& info) const
{
    // special cases for constant SPs MSC and TMSC
    if (OMNI_PROPERTY_MSC == propertyId) {
        info = implied_omni;
        return true;
    } else if (OMNI_PROPERTY_TMSC == propertyId) {
        info = implied_tomni;
        return true;
    }

    // DB value for property entry
    if (!Read(CUpdatePropertyKey{propertyId}, info)) {
        PrintToLog("%s(): ERROR for SP %d: not found\n", __func__, propertyId);
        return false;
    }

    // Check for unique entry
    info.unique = Read(CUniquePropertyKey{propertyId}, info.unique) && info.unique;

    // Check for delegate entry
    if (!Read(CDelegatePropertyKey{propertyId}, CDelegateValue{info.delegate, info.historicalDelegates})) {
        info.delegate.clear();
        info.historicalDelegates.clear();
    }

    return true;
}

bool CMPSPInfo::hasSP(uint32_t propertyId) const
{
    // Special cases for constant SPs MSC and TMSC
    if (OMNI_PROPERTY_MSC == propertyId || OMNI_PROPERTY_TMSC == propertyId) {
        return true;
    }

    // DB key for property entry
    auto sKey = KeyToString(CUpdatePropertyKey{propertyId});

    // DB value for property entry
    std::string strSpValue;
    return Read(sKey, strSpValue);
}

uint32_t CMPSPInfo::findSPByTX(const uint256& txid) const
{
    uint32_t propertyId = 0;
    // DB key for identifier lookup entry
    return Read(CLookupTxKey{txid}, propertyId) ? propertyId : 0;
}

int64_t CMPSPInfo::popBlock(const uint256& block_hash)
{
    int64_t remainingSPs = 0;
    leveldb::WriteBatch commitBatch;
    for (CDBaseIterator it{NewIterator(), CUniquePropertyKey{0}}; it; ++it) {
        // deserialize the persisted value
        Entry info;
        if (!it.Value(info)) {
            continue;
        }
        // pop the block
        if (info.update_block == block_hash) {
            // need to roll this SP back
            if (info.update_block == info.creation_block) {
                // this is the block that created this SP, so delete the SP and the tx index ent
                commitBatch.Delete(it.Key());
                commitBatch.Delete(KeyToString(CLookupTxKey{info.txid}));
            } else {
                auto propertyId = it.Key<CUniquePropertyKey>().propertyId;
                auto hKey = KeyToString(CHistoricalPropertyKey{info.update_block, propertyId});
                std::string strSpPrevValue;
                if (Read(hKey, strSpPrevValue)) {
                    // copy the prev state to the current state and delete the old state
                    commitBatch.Put(it.Key(), strSpPrevValue);
                    commitBatch.Delete(hKey);
                    ++remainingSPs;
                } else {
                    continue;
                }
            }
        } else {
            ++remainingSPs;
        }
    }

    WriteBatch(commitBatch);
    return remainingSPs;
}

static constexpr std::string_view wprefix = "B";

void CMPSPInfo::setWatermark(const uint256& watermark)
{
    Write(wprefix, watermark);
}

bool CMPSPInfo::getWatermark(uint256& watermark) const
{
    return Read(wprefix, watermark);
}

void CMPSPInfo::printAll() const
{
    // print off the hard coded MSC and TMSC entries
    for (uint32_t idx = OMNI_PROPERTY_MSC; idx <= OMNI_PROPERTY_TMSC; idx++) {
        Entry info;
        PrintToConsole("%10d => ", idx);
        if (getSP(idx, info)) {
            info.print();
        } else {
            PrintToConsole("<Internal Error on implicit SP>\n");
        }
    }

    for (CDBaseIterator it{NewIterator(), CUniquePropertyKey{0}}; it; ++it) {
        auto propertyId = it.Key<CUniquePropertyKey>().propertyId;
        PrintToConsole("%10s => ", propertyId);
        // deserialize the persisted data
        Entry info;
        if (it.Value(info)) {
            info.print();
        }
    }
}
