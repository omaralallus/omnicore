#include <cstdint>
#include <omnicore/log.h>

#include <chainparamsbase.h>
#include <fs.h>
#include <logging.h>
#include <optional>
#include <streams.h>
#include <util/system.h>
#include <util/time.h>

#include <assert.h>
#include <stdio.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Default log files
const char* LOG_FILENAME    = "omnicore.log";

// Options
static const long LOG_BUFFERSIZE  =  8000000; //  8 MB
static const long LOG_SHRINKSIZE  = 50000000; // 50 MB

// Debug flags
bool msc_debug_parser_data        = 0;
bool msc_debug_parser_readonly    = 0;
//! Print information to potential DEx payments and outputs
bool msc_debug_parser_dex         = 1;
bool msc_debug_parser             = 0;
bool msc_debug_verbose            = 0;
bool msc_debug_verbose2           = 0;
bool msc_debug_verbose3           = 0;
bool msc_debug_vin                = 0;
bool msc_debug_script             = 0;
bool msc_debug_dex                = 1;
bool msc_debug_send               = 1;
bool msc_debug_tokens             = 0;
//! Print information about payloads with non-sequential sequence number
bool msc_debug_spec               = 0;
bool msc_debug_exo                = 0;
bool msc_debug_tally              = 1;
bool msc_debug_sp                 = 1;
bool msc_debug_sto                = 1;
bool msc_debug_txdb               = 0;
bool msc_debug_tradedb            = 1;
bool msc_debug_persistence        = 0;
bool msc_debug_ui                 = 0;
bool msc_debug_pending            = 1;
bool msc_debug_metadex1           = 0;
bool msc_debug_metadex2           = 0;
//! Print orderbook before and after each trade
bool msc_debug_metadex3           = 0;
//! Print transaction fields, when interpreting packets
bool msc_debug_packets            = 1;
//! Print transaction fields, when interpreting packets (in RPC mode)
bool msc_debug_packets_readonly   = 0;
//! Print each line added to consensus hash
bool msc_debug_consensus_hash     = 0;
//! Print consensus hashes for each block when parsing
bool msc_debug_consensus_hash_every_block = 0;
//! Print extra info on alert processing
bool msc_debug_alerts             = 1;
//! Print consensus hashes for each transaction when parsing
bool msc_debug_consensus_hash_every_transaction = 0;
//! Debug fees
bool msc_debug_fees               = 1;
//! Debug the non-fungible tokens database
bool msc_debug_nftdb              = 0;

/**
 * We use std::call_once() to make sure these are initialized
 * in a thread-safe manner the first time called:
 */
static FILE* fileout = nullptr;
std::recursive_mutex mutexDebugLog;

/** Flag to indicate, whether the Omni Core log file should be reopened. */
std::atomic<bool> fReopenOmniCoreLog{false};

/** override to print to omni log to console */
std::atomic<bool> fOmniCoreConsoleLog{false};

void CloseLogFile()
{
    std::lock_guard lock(mutexDebugLog);
    if (fileout) {
        fclose(fileout);
        fileout = nullptr;
    }
    fReopenOmniCoreLog = false;
}

/**
 * Returns path for debug log file.
 *
 * The log file can be specified via startup option "--omnilogfile=/path/to/omnicore.log",
 * and if none is provided, then the client's datadir is used as default location.
 */
static fs::path GetLogPath()
{
    fs::path pathLogFile;
    std::string strLogPath = gArgs.GetArg("-omnilogfile", "");

    if (!strLogPath.empty()) {
        pathLogFile = fs::path(strLogPath.c_str());
        TryCreateDirectories(pathLogFile.parent_path());
    } else {
        pathLogFile = gArgs.GetDataDirNet() / LOG_FILENAME;
    }

    return pathLogFile;
}

/**
 * Opens debug log file.
 */
static void DebugLogInit()
{
    std::lock_guard lock(mutexDebugLog);
    assert(fileout == nullptr);

    fs::path pathDebug = GetLogPath();
    fileout = fopen(pathDebug.string().c_str(), "a");

    if (fileout) {
        setbuf(fileout, nullptr); // Unbuffered
    } else {
        PrintToConsole("Failed to open debug log file: %s\n", pathDebug.string());
    }
}

/**
 * @return The current timestamp in the format: 2009-01-03 18:15:05
 */
static std::string GetTimestamp()
{
    return FormatISO8601DateTime(GetTime());
}

int LogWriteLine(const std::string& str, FILE* file)
{
    int ret = 0; // Number of characters written
    static std::atomic_bool fStartedNewLine = true;
    // Printing log timestamps can be useful for profiling
    fStartedNewLine && (ret += fprintf(file, "%s ", GetTimestamp().c_str()));
    fStartedNewLine = !str.empty() && str.back() == '\n';
    return ret + fwrite(str.data(), 1, str.size(), file);
}

/**
 * Prints to log file.
 *
 * The configuration options "-logtimestamps" can be used to indicate, whether
 * the message to log should be prepended with a timestamp.
 *
 * If "-printtoconsole" is enabled, then the message is written to the standard
 * output, usually the console, instead of a log file.
 *
 * @param str[in]  The message to log
 * @return The total number of characters written
 */
int LogFilePrint(const std::string& str)
{
    if (auto ret = ConsolePrint(str)) {
        return ret;
    }

    std::lock_guard lock(mutexDebugLog);
    // Reopen the log file, if requested
    if (fReopenOmniCoreLog) {
        CloseLogFile();
        DebugLogInit();
    }
    return fileout ? LogWriteLine(str, fileout) : 0;
}

/**
 * Prints to the standard output, usually the console.
 *
 * The configuration option "-logtimestamps" can be used to indicate, whether
 * the message should be prepended with a timestamp.
 *
 * @param str[in]  The message to print
 * @return The total number of characters written
 */
int ConsolePrint(const std::string& str)
{
    int ret = 0;
    if (fOmniCoreConsoleLog) {
        ret = LogWriteLine(str, stdout);
        fflush(stdout);
    }
    return ret;
}

/**
 * Determine whether to override compiled debug levels via enumerating startup option --omnidebug.
 *
 * Example usage (granular categories)    : --omnidebug=parser --omnidebug=metadex1 --omnidebug=ui
 * Example usage (enable all categories)  : --omnidebug=all
 * Example usage (disable all debugging)  : --omnidebug=none
 * Example usage (disable all except XYZ) : --omnidebug=none --omnidebug=parser --omnidebug=sto
 */

#ifndef WIN32
#include <signal.h>
static void handleSIGUP(int)
{
    fReopenOmniCoreLog = true;
}
#endif

void InitDebugLogLevels()
{
    fOmniCoreConsoleLog = gArgs.GetBoolArg("-printtoconsole", false);
    if (!fOmniCoreConsoleLog) {
        DebugLogInit();
    }

#ifndef WIN32
    // Reopen omnicore.log on SIGHUP
    struct sigaction sa{};
    sa.sa_handler = handleSIGUP;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);
#endif

    if (!gArgs.IsArgSet("-omnidebug")) {
        return;
    }

    const std::vector<std::string>& debugLevels = gArgs.GetArgs("-omnidebug");

    for (const auto& d : debugLevels) {
        auto none = d == "none";
        auto all = d == "all";
#define ENABLE_LOG(x) msc_debug_##x = none ? false : (all || d == #x)
        ENABLE_LOG(parser_data);
        ENABLE_LOG(parser_readonly);
        ENABLE_LOG(parser_dex);
        ENABLE_LOG(parser);
        ENABLE_LOG(verbose);
        ENABLE_LOG(verbose2);
        ENABLE_LOG(verbose3);
        ENABLE_LOG(vin);
        ENABLE_LOG(script);
        ENABLE_LOG(dex);
        ENABLE_LOG(send);
        ENABLE_LOG(tokens);
        ENABLE_LOG(spec);
        ENABLE_LOG(exo);
        ENABLE_LOG(tally);
        ENABLE_LOG(sp);
        ENABLE_LOG(sto);
        ENABLE_LOG(txdb);
        ENABLE_LOG(tradedb);
        ENABLE_LOG(persistence);
        ENABLE_LOG(ui);
        ENABLE_LOG(pending);
        ENABLE_LOG(metadex1);
        ENABLE_LOG(metadex2);
        ENABLE_LOG(metadex3);
        ENABLE_LOG(packets);
        ENABLE_LOG(packets_readonly);
        ENABLE_LOG(consensus_hash);
        ENABLE_LOG(consensus_hash_every_block);
        ENABLE_LOG(alerts);
        ENABLE_LOG(consensus_hash_every_transaction);
        ENABLE_LOG(fees);
        ENABLE_LOG(nftdb);
#undef ENABLE_LOG
    }
}

/**
 * Scrolls debug log, if it's getting too big.
 */
void ShrinkDebugLog()
{
    const fs::path pathLog = GetLogPath();
    if (!fs::exists(pathLog) || fs::file_size(pathLog) <= LOG_SHRINKSIZE) {
        return;
    }
    const auto fileStr = pathLog.string();
    AutoFile autoFile(fopen(fileStr.c_str(), "r"));
    if (auto file = autoFile.Get()) {
        // Restart the file with some of the end
        std::string pch;
        pch.resize(LOG_BUFFERSIZE);
        fseek(file, -LOG_BUFFERSIZE, SEEK_END);
        int nBytes = fread(pch.data(), 1, LOG_BUFFERSIZE, file);
        autoFile.fclose();

        AutoFile reOpen(fopen(fileStr.c_str(), "w"));
        if (auto file = reOpen.Get()) {
            fwrite(pch.data(), 1, nBytes, file);
        }
    }
}
