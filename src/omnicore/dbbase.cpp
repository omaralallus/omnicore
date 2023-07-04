#include <omnicore/dbbase.h>
#include <omnicore/log.h>

#include <fs.h>
#include <util/system.h>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

#include <stdint.h>

/**
 * Opens or creates a LevelDB based database.
 */
leveldb::Status CDBBase::Open(const fs::path& path, bool fWipe)
{
    if (fWipe) {
        if (msc_debug_persistence) PrintToLog("Wiping LevelDB in %s\n", path.string());
        leveldb::DestroyDB(path.string(), options);
    }
    TryCreateDirectories(path);
    if (msc_debug_persistence) PrintToLog("Opening LevelDB in %s\n", path.string());

    leveldb::DB* db;
    auto status = leveldb::DB::Open(options, path.string(), &db);
    if (status.ok()) {
        pdb.reset(db);
    }
    return status;
}

/**
 * Deletes all entries of the database, and resets the counters.
 */
void CDBBase::Clear()
{
    int64_t nTimeStart = GetTimeMicros();
    unsigned int n = 0;
    leveldb::WriteBatch batch;
    CDBaseIterator it{NewIterator()};

    for (; it; ++it) {
        batch.Delete(it.Key());
        ++n;
    }

    nRead = 0;
    nWritten = 0;
    bool status = WriteBatch(batch);

    int64_t nTime = GetTimeMicros() - nTimeStart;
    if (msc_debug_persistence)
        PrintToLog("Removed %d entries: %s [%.3f ms/entry, %.3f ms total]\n",
            n, status ? "OK" : "NOK", (n > 0 ? (0.001 * nTime / n) : 0), 0.001 * nTime);
}

/**
 * Deinitializes and closes the database.
 */
void CDBBase::Close()
{
    pdb.reset();
}


/**
@todo  Move initialization and deinitialization of databases into this file (?)
@todo  Move file based storage into this file
@todo  Replace file based storage with LevelDB based storage
@todo  Wrap global state objects and prevent direct access:

static CMPTradeList* ptradedb = new CMPTradeList();

CMPTradeList& TradeDB() {
    assert(ptradedb);
    return *ptradedb;
}

// before:
t_tradelistdb->recordTrade();

// after:
TradeDB().recordTrade();

*/
