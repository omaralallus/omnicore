#include <omnicore/script.h>

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <script/script.h>
#include <script/standard.h>
#include <serialize.h>
#include <util/strencodings.h>

#include <string>
#include <utility>
#include <vector>

/** The minimum transaction relay fee. */
extern CFeeRate minRelayTxFee;

/**
 * Determines the minimum output amount to be spent by an output, based on the
 * scriptPubKey size in relation to the minimum relay fee.
 *
 * @param scriptPubKey[in]  The scriptPubKey
 * @return The dust threshold value
 */
int64_t OmniGetDustThreshold(const CScript& scriptPubKey)
{
    CTxOut txOut(0, scriptPubKey);

    return GetDustThreshold(txOut, minRelayTxFee) * 3;
}

/**
 * Identifies standard output types based on a scriptPubKey.
 *
 * Note: whichTypeRet is set to TxoutType::NONSTANDARD, if no standard script was found.
 *
 * @param scriptPubKey[in]   The script
 * @param whichTypeRet[out]  The output type
 * @return True if a standard script was found
 */
bool GetOutputType(const CScript& scriptPubKey, TxoutType& whichTypeRet)
{
    std::vector<std::vector<unsigned char> > vSolutions;

    if (SafeSolver(scriptPubKey, whichTypeRet, vSolutions)) {
        return true;
    }
    whichTypeRet = TxoutType::NONSTANDARD;

    return false;
}

/**
 * Extracts the pushed data as hex-encoded string from a script.
 *
 * @param script[in]      The script
 * @param vstrRet[out]    The extracted pushed data as hex-encoded string
 * @param fSkipFirst[in]  Whether the first push operation should be skipped (default: false)
 * @return True if the extraction was successful (result can be empty)
 */
bool GetScriptPushes(const CScript& script, std::vector<std::string>& vstrRet, bool fSkipFirst)
{
    int version = 0;
    std::vector<unsigned char> program;
    if (script.IsWitnessProgram(version, program)) {
        if (fSkipFirst) {
            return true;
        }
        if ((version == 0 && program.size() == WITNESS_V0_KEYHASH_SIZE)
        || ((version == 0 && program.size() == WITNESS_V0_SCRIPTHASH_SIZE)
        || ((version == 1 && program.size() == WITNESS_V1_TAPROOT_SIZE)))) {
            vstrRet.push_back(HexStr(program));
        }
        return true;
    }

    int count = 0;
    CScript::const_iterator pc = script.begin();

    while (pc < script.end()) {
        opcodetype opcode;
        std::vector<unsigned char> data;
        if (!script.GetOp(pc, opcode, data))
            return false;
        if (0x00 <= opcode && opcode <= OP_PUSHDATA4)
            if (count++ || !fSkipFirst) vstrRet.push_back(HexStr(data));
    }

    return true;
}

/**
 * Returns public keys or hashes from scriptPubKey, for standard transaction types.
 *
 * Note: in contrast to the script/standard/Solver, this Solver is not affected by
 * user settings, and in particular any OP_RETURN size is considered as standard.
 *
 * @param scriptPubKey[in]    The script
 * @param typeRet[out]        The output type
 * @param vSolutionsRet[out]  The extracted public keys or hashes
 * @return True if a standard script was found
 */
bool SafeSolver(const CScript& scriptPubKey, TxoutType& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet)
{
    // Templates
    static const std::multimap<TxoutType, CScript> mTemplates{
        // Standard tx, sender provides pubkey, receiver adds signature
        { TxoutType::PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG },

        // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        { TxoutType::PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG },

        // Sender provides N pubkeys, receivers provides M signatures
        { TxoutType::MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG },

        // Empty, provably prunable, data-carrying output
        { TxoutType::NULL_DATA, CScript() << OP_RETURN },
    };

    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        typeRet = TxoutType::SCRIPTHASH;
        vSolutionsRet.emplace_back(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        return true;
    }

    int witnessversion;
    std::vector<unsigned char> witnessprogram;
    if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_KEYHASH_SIZE) {
            typeRet = TxoutType::WITNESS_V0_KEYHASH;
            vSolutionsRet.push_back(std::move(witnessprogram));
            return true;
        }
        if (witnessversion == 0 && witnessprogram.size() == WITNESS_V0_SCRIPTHASH_SIZE) {
            typeRet = TxoutType::WITNESS_V0_SCRIPTHASH;
            vSolutionsRet.push_back(std::move(witnessprogram));
            return true;
        }
        if (witnessversion == 1 && witnessprogram.size() == WITNESS_V1_TAPROOT_SIZE) {
            typeRet = TxoutType::WITNESS_V1_TAPROOT;
            vSolutionsRet.push_back(std::move(witnessprogram));
            return true;
        }
        return false;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 2 && scriptPubKey[0] == OP_RETURN)
    {
        CScript script(scriptPubKey.begin()+1, scriptPubKey.end());
        if (script.IsPushOnly()) {
            typeRet = TxoutType::NULL_DATA;
            return true;
        }
    }

    // Scan templates
    const CScript& script1 = scriptPubKey;
    for(const auto& tplate : mTemplates)
    {
        const CScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        std::vector<unsigned char> vch1, vch2;

        // Compare
        CScript::const_iterator pc1 = script1.begin();
        CScript::const_iterator pc2 = script2.begin();
        while (true)
        {
            if (pc1 == script1.end() && pc2 == script2.end())
            {
                // Found a match
                typeRet = tplate.first;
                if (typeRet == TxoutType::MULTISIG)
                {
                    // Additional checks for TxoutType::MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }
                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1))
                break;
            if (!script2.GetOp(pc2, opcode2, vch2))
                break;

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS)
            {
                while (vch1.size() >= 33 && vch1.size() <= 65)
                {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1))
                        break;
                }
                if (!script2.GetOp(pc2, opcode2, vch2))
                    break;
                // Normal situation is to fall through
                // to other if/else statements
            }

            if (opcode2 == OP_PUBKEY)
            {
                if (vch1.size() < 33 || vch1.size() > 65)
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_PUBKEYHASH)
            {
                if (vch1.size() != sizeof(uint160))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_SMALLINTEGER)
            {   // Single-byte small integer pushed onto vSolutions
                if (opcode1 == OP_0 ||
                    (opcode1 >= OP_1 && opcode1 <= OP_16))
                {
                    char n = (char)CScript::DecodeOP_N(opcode1);
                    vSolutionsRet.push_back(std::vector<unsigned char>(1, n));
                }
                else
                    break;
            }
            else if (opcode1 != opcode2 || vch1 != vch2)
            {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TxoutType::NONSTANDARD;
    return false;
}
