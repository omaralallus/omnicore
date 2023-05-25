#ifndef BITCOIN_OMNICORE_SCRIPT_H
#define BITCOIN_OMNICORE_SCRIPT_H

#include <string>
#include <vector>

class CScript;

#include <script/standard.h>

/** Determines the minimum output amount to be spent by an output. */
int64_t OmniGetDustThreshold(const CScript& scriptPubKey);

/** Identifies standard output types based on a scriptPubKey. */
bool GetOutputType(const CScript& scriptPubKey, TxoutType& whichTypeRet);

/** Extracts the pushed data as hex-encoded string from a script. */
bool GetScriptPushes(const CScript& script, std::vector<std::string>& vstrRet, bool fSkipFirst = false);

/** Returns public keys or hashes from scriptPubKey, for standard transaction types. */
bool SafeSolver(const CScript& scriptPubKey, TxoutType& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet);

/** Returns valid destination from Omni address. */
CTxDestination DecodeOmniDestination(const std::string& address);

/** Returns Omni address from valid destination. */
std::string EncodeOmniDestination(const CTxDestination& dest);

/** Returns Omni address if it's applicable. */
std::string TryEncodeOmniAddress(const std::string& address);

/** Returns original Bitcoin address. */
std::string TryDecodeOmniAddress(const std::string& address);

#endif // BITCOIN_OMNICORE_SCRIPT_H
