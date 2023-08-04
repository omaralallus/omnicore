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
    PrintToLog("%s:%s(Fixed=%s;Manual=%s;Unique=%s;Divisible=%s):%d:%s/%s, %s %s\n",
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

struct CBasePropertyKey {
    uint32_t propertyId = 0;

    SERIALIZE_METHODS(CBasePropertyKey, obj) {
        READWRITE(Using<Varint>(obj.propertyId));
    }
};

struct CUpdatePropertyKey : CBasePropertyKey {
    static constexpr uint8_t prefix = 's';
    uint32_t block = ~0u;

    SERIALIZE_METHODS(CUpdatePropertyKey, obj) {
        READWRITEAS(CBasePropertyKey, obj);
        READWRITE(Using<BigEndian32Inv>(obj.block));
    }
};

struct CLookupTxKey {
    static constexpr uint8_t prefix = 't';
    const uint256& txid;

    SERIALIZE_METHODS(CLookupTxKey, obj) {
        READWRITE(obj.txid);
    }
};

bool CMPSPInfo::updateSP(uint32_t propertyId, const Entry& info, int block)
{
    // cannot update implied SP
    if (OMNI_PROPERTY_MSC == propertyId || OMNI_PROPERTY_TMSC == propertyId) {
        return false;
    }

    // DB key for property entry
    auto sKey = KeyToString(CUpdatePropertyKey{propertyId, uint32_t(block)});

    // DB value for property entry
    auto sValue = ValueToString(info);

    // sanity checking
    std::string existingEntry;
    if (Read(sKey, existingEntry) && sValue.compare(existingEntry) != 0) {
        std::string strError = strprintf("writing SP %d to DB, when a different SP already exists for that identifier", propertyId);
        PrintToLog("%s() ERROR: %s\n", __func__, strError);
    }

    return Write(sKey, sValue);
}


uint32_t CMPSPInfo::putSP(uint8_t ecosystem, const Entry& info, int block)
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

    updateSP(propertyId, info, block);

    if (!Write(CLookupTxKey{info.txid}, propertyId)) {
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
    CDBaseIterator it{NewIterator(), PartialKey<CUpdatePropertyKey>(CBasePropertyKey{propertyId})};
    auto status = it.Valid() && it.Value(info);
    if (!status) {
        PrintToLog("%s(): ERROR for SP %d: not found\n", __func__, propertyId);
    }
    return status;
}

bool CMPSPInfo::hasSP(uint32_t propertyId) const
{
    // Special cases for constant SPs MSC and TMSC
    if (OMNI_PROPERTY_MSC == propertyId || OMNI_PROPERTY_TMSC == propertyId) {
        return true;
    }
    Entry info;
    return getSP(propertyId, info);
}

uint32_t CMPSPInfo::findSPByTX(const uint256& txid) const
{
    // DB key for identifier lookup entry
    uint32_t propertyId;
    return Read(CLookupTxKey{txid}, propertyId) ? propertyId : 0;
}

void CMPSPInfo::deleteSPAboveBlock(int block)
{
    CDBWriteBatch batch;
    uint32_t startBlock = block;
    CDBaseIterator it{NewIterator()};
    for (uint8_t ecosystem = 1; ecosystem <= 2; ecosystem++) {
        uint32_t startPropertyId = (ecosystem == 1) ? 1 : TEST_ECO_PROPERTY_1;
        auto lastPropertyId = peekNextSPID(ecosystem);
        for (uint32_t propertyId = startPropertyId; propertyId < lastPropertyId; propertyId++) {
            for (it.Seek(PartialKey<CUpdatePropertyKey>(CBasePropertyKey{propertyId})); it; ++it) {
                auto key = it.Key<CUpdatePropertyKey>();
                if (key.block < startBlock) break;
                auto info = it.Value<Entry>();
                if (info.creation_block == info.update_block) {
                    batch.Delete(CLookupTxKey{info.txid});
                }
                batch.Delete(it.Key());
            }
        }
    }
    WriteBatch(batch);
}

static constexpr std::string_view wprefix = "B";

struct CWattermarkValue {
    CRef<uint256> blockHash;
    CRef<int> blockHeight;

    SERIALIZE_METHODS(CWattermarkValue, obj) {
        READWRITE(obj.blockHash);
        READWRITE(obj.blockHeight);
    }
};

void CMPSPInfo::setWatermark(const uint256& watermark, int block)
{
    Write(wprefix, CWattermarkValue{watermark, block});
}

bool CMPSPInfo::getWatermark(uint256& watermark, int& block) const
{
    return Read(wprefix, CWattermarkValue{watermark, block});
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

    for (CDBaseIterator it{NewIterator(), CLookupTxKey{{}}}; it; ++it) {
        auto propertyId = it.Value<uint32_t>();
        PrintToConsole("%10s => ", propertyId);
        // deserialize the persisted data
        Entry info;
        if (getSP(propertyId, info)) {
            info.print();
        }
    }
}
