// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpserver.h>
#include <key_io.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/descriptor.h>
#include <validation.h>
#include <util/strencodings.h>

#include <omnicore/omnicore.h>

#include <consensus/consensus.h>
#include <txmempool.h>

#include <stdint.h>

#include <univalue.h>

bool getAddressesFromParams(const UniValue& params, std::vector<std::pair<uint256, int> > &addresses)
{
    if (params[0].isStr()) {
        uint256 hashBytes;
        int type = 0;
        if (!DecodeIndexKey(params[0].get_str(), hashBytes, type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
        }
        addresses.push_back(std::make_pair(hashBytes, type));
    } else if (params[0].isObject()) {

        UniValue addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Addresses is expected to be an array");
        }

        std::vector<UniValue> values = addressValues.getValues();

        for (std::vector<UniValue>::iterator it = values.begin(); it != values.end(); ++it) {

            uint256 hashBytes;
            int type = 0;
            if (!DecodeIndexKey(it->get_str(), hashBytes, type)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
            }
            addresses.push_back(std::make_pair(hashBytes, type));
        }
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    return true;
}

bool heightSort(std::pair<CAddressUnspentKey, CAddressUnspentValue> a,
                std::pair<CAddressUnspentKey, CAddressUnspentValue> b) {
    return a.second.blockHeight < b.second.blockHeight;
}

bool timestampSort(std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> a,
                   std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> b) {
    return a.second.time < b.second.time;
}

bool getAddressFromIndex(const int &type, const uint256 &hash, std::string &address)
{
    if (type == 2) {
        std::vector<unsigned char> addressBytes(hash.begin(), hash.begin() + 20);
        address = EncodeDestination(ScriptHash(uint160(addressBytes)));
    } else if (type == 1) {
        std::vector<unsigned char> addressBytes(hash.begin(), hash.begin() + 20);
        address = EncodeDestination(PKHash(uint160(addressBytes)));
    } else if (type == 3) {
        address = EncodeDestination(WitnessV0ScriptHash(hash));
    } else if (type == 4) {
        std::vector<unsigned char> addressBytes(hash.begin(), hash.begin() + 20);
        address = EncodeDestination(WitnessV0KeyHash(uint160(addressBytes)));
    } else {
        return false;
    }
    return true;
}

UniValue getaddressdeltas(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddressdeltas",
        "\nReturns all changes for an address (requires addressindex to be enabled).\n",
        {
            {"Input params", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Json object",
                {
                    {"addresses", RPCArg::Type::ARR, RPCArg::Optional::NO, "The addresses",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The address"},
                        }
                    },
                    {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "The start block height"},
                    {"end", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "The end block height"},
                    {"chainInfo", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED_NAMED_ARG, "Include chain info in results, only applies if start and end specified"},
                }
            }
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {
                    {RPCResult::Type::NUM, "satoshis", "The difference of satoshis"},
                    {RPCResult::Type::STR_HEX, "txid", "The related txid"},
                    {RPCResult::Type::NUM, "index", "The related input or output index"},
                    {RPCResult::Type::NUM, "height", "The block height"},
                    {RPCResult::Type::STR, "address", "The address"},
                }},
            }},
        },
        RPCExamples{
            HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}'")
            + HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"], \"start\": 5000, \"end\": 5500, \"chainInfo\": true}'")
            + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}")
            + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"], \"start\": 5000, \"end\": 5500, \"chainInfo\": true}")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    UniValue startValue = find_value(request.params[0].get_obj(), "start");
    UniValue endValue = find_value(request.params[0].get_obj(), "end");

    UniValue chainInfo = find_value(request.params[0].get_obj(), "chainInfo");
    bool includeChainInfo = false;
    if (chainInfo.isBool()) {
        includeChainInfo = chainInfo.get_bool();
    }

    int start = 0;
    int end = 0;

    if (startValue.isNum() && endValue.isNum()) {
        start = startValue.getInt<int>();
        end = endValue.getInt<int>();
        if (start <= 0 || end <= 0) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start and end is expected to be greater than zero");
        }
        if (end < start) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "End value is expected to be greater than start");
        }
    }

    std::vector<std::pair<uint256, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (std::vector<std::pair<uint256, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (start > 0 && end > 0) {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex, start, end)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        } else {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    UniValue deltas(UniValue::VARR);

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("satoshis", it->second);
        delta.pushKV("txid", it->first.txhash.GetHex());
        delta.pushKV("index", (int)it->first.index);
        delta.pushKV("blockindex", (int)it->first.txindex);
        delta.pushKV("height", it->first.blockHeight);
        delta.pushKV("address", address);
        deltas.push_back(delta);
    }

    UniValue result(UniValue::VOBJ);

    if (includeChainInfo && start > 0 && end > 0) {
        LOCK(cs_main);

        if (start > ::ChainActive().Height() || end > ::ChainActive().Height()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");
        }

        CBlockIndex* startIndex = ::ChainActive()[start];
        CBlockIndex* endIndex = ::ChainActive()[end];

        UniValue startInfo(UniValue::VOBJ);
        UniValue endInfo(UniValue::VOBJ);

        startInfo.pushKV("hash", startIndex->GetBlockHash().GetHex());
        startInfo.pushKV("height", start);

        endInfo.pushKV("hash", endIndex->GetBlockHash().GetHex());
        endInfo.pushKV("height", end);

        result.pushKV("deltas", deltas);
        result.pushKV("start", startInfo);
        result.pushKV("end", endInfo);

        return result;
    } else {
        return deltas;
    }
}

UniValue getaddressbalance(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddressbalance",
        "\nReturns the balance for an address(es) (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, RPCArg::Optional::NO, "The addresses",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The address"},
                }},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "balance", "The current balance in satoshis"},
                {RPCResult::Type::STR, "received", "The total number of satoshis received (including change)"},
                {RPCResult::Type::STR, "immature", "The total number of satoshis received (including change)"},
            }
        },
        RPCExamples{
            HelpExampleCli("getaddressbalance", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}'")
            + HelpExampleRpc("getaddressbalance", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    std::vector<std::pair<uint256, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (std::vector<std::pair<uint256, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    CAmount balance = 0;
    CAmount received = 0;
    CAmount immature = 0;

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
        if (it->second > 0) {
            received += it->second;
        }
        balance += it->second;
        if (it->first.txindex == 0 && ((::ChainActive().Height() - it->first.blockHeight) < COINBASE_MATURITY))
            immature += it->second;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("balance", balance);
    result.pushKV("received", received);
    result.pushKV("immature", immature);

    return result;
}

UniValue getaddressutxos(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddressutxos",
        "\nReturns all unspent outputs for an address (requires addressindex to be enabled).\n",
        {
            {"Input params", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Json object",
                {
                    {"addresses", RPCArg::Type::ARR, RPCArg::Optional::NO, "The addresses",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The address"},
                        }
                    },
                    {"chainInfo", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED_NAMED_ARG, "Include chain info with results"},
                }
            }
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                     {
                     {RPCResult::Type::STR, "address", "The address base58check encoded"},
                     {RPCResult::Type::STR_HEX, "txid", "The output txid"},
                     {RPCResult::Type::NUM, "height", "The block height"},
                     {RPCResult::Type::NUM, "outputIndex", "The output index"},
                     {RPCResult::Type::STR, "outputIndex", "The script hex encoded"},
                     {RPCResult::Type::NUM, "satoshis", "The number of satoshis of the output"},
                }},
            }},
        },
        RPCExamples{
            HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}'")
            + HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"], \"chainInfo\": true}'")
            + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}")
            + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"], \"chainInfo\": true}")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    bool includeChainInfo = false;
    if (request.params[0].isObject()) {
        UniValue chainInfo = find_value(request.params[0].get_obj(), "chainInfo");
        if (chainInfo.isBool()) {
            includeChainInfo = chainInfo.get_bool();
        }
    }

    std::vector<std::pair<uint256, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;

    for (std::vector<std::pair<uint256, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (!GetAddressUnspent((*it).first, (*it).second, unspentOutputs)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }

    std::sort(unspentOutputs.begin(), unspentOutputs.end(), heightSort);

    UniValue utxos(UniValue::VARR);

    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++) {
        UniValue output(UniValue::VOBJ);
        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        output.pushKV("address", address);
        output.pushKV("txid", it->first.txhash.GetHex());
        output.pushKV("outputIndex", (int)it->first.index);
        output.pushKV("script", HexStr(it->second.script.begin(), it->second.script.end()));
        output.pushKV("satoshis", it->second.satoshis);
        output.pushKV("height", it->second.blockHeight);
        output.pushKV("coinbase", it->second.coinBase);
        utxos.push_back(output);
    }

    if (includeChainInfo) {
        UniValue result(UniValue::VOBJ);
        result.pushKV("utxos", utxos);

        LOCK(cs_main);
        result.pushKV("hash", ::ChainActive().Tip()->GetBlockHash().GetHex());
        result.pushKV("height", (int)::ChainActive().Height());
        return result;
    } else {
        return utxos;
    }
}

UniValue getaddressmempool(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddressmempool",
        "\nReturns all mempool deltas for an address (requires addressindex to be enabled).\n",
        {
            {"addresses", RPCArg::Type::ARR, RPCArg::Optional::NO, "The addresses",
                {
                    {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The address"},
                }},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {
                        {RPCResult::Type::STR, "address", "The address"},
                        {RPCResult::Type::STR_HEX, "txid", "The related txid"},
                        {RPCResult::Type::NUM, "index", "The related input or output index"},
                        {RPCResult::Type::NUM, "satoshis", "The difference of satoshis"},
                        {RPCResult::Type::NUM, "timestamp", "The time the transaction entered the mempool (seconds)"},
                        {RPCResult::Type::STR, "prevtxid", "The previous txid (if spending)"},
                        {RPCResult::Type::STR, "prevout", "The previous transaction output index (if spending)"},
                }},
            }},
        },
        RPCExamples{
            HelpExampleCli("getaddressmempool", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}'")
            + HelpExampleRpc("getaddressmempool", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    std::vector<std::pair<uint256, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    auto mempool = ::ChainstateActive().GetMempool();
    if (!mempool) {
        throw JSONRPCError(RPC_MISC_ERROR, "No mempool available");
    }

    std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> > indexes;
    if (!mempool->getAddressIndex(addresses, indexes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
    }

    std::sort(indexes.begin(), indexes.end(), timestampSort);

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta> >::iterator it = indexes.begin();
         it != indexes.end(); it++) {

        std::string address;
        if (!getAddressFromIndex(it->first.type, it->first.addressBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", address);
        delta.pushKV("txid", it->first.txhash.GetHex());
        delta.pushKV("index", (int)it->first.index);
        delta.pushKV("satoshis", it->second.amount);
        delta.pushKV("timestamp", it->second.time);
        if (it->second.amount < 0) {
            delta.pushKV("prevtxid", it->second.prevhash.GetHex());
            delta.pushKV("prevout", (int)it->second.prevout);
        }
        result.push_back(delta);
    }

    return result;
}

UniValue getblockhashes(const JSONRPCRequest& request)
{
    RPCHelpMan{"getblockhashes",
        "\nReturns array of hashes of blocks within the timestamp range provided.\n",
        {
            {"high", RPCArg::Type::NUM, RPCArg::Optional::NO, "The newer block timestamp"},
            {"low", RPCArg::Type::NUM, RPCArg::Optional::NO, "The older block timestamp"},
            {"options", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "An object with options",
                {
                    {"noOrphans", RPCArg::Type::BOOL, RPCArg::DefaultHint{"false"}, "Will only include blocks on the main chain"},
                    {"logicalTimes", RPCArg::Type::BOOL, RPCArg::DefaultHint{"false"}, "Will include logical timestamps with hashes"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {
                {RPCResult::Type::STR_HEX, "hash", "The block hash"},
            }},
        },
        RPCExamples{
            HelpExampleCli("getblockhashes", "1231614698 1231024505")
            + HelpExampleCli("getblockhashes", "1231614698 1231024505 '{\"noOrphans\":false, \"logicalTimes\":true}'")
            + HelpExampleRpc("getblockhashes", "1231614698, 1231024505")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    unsigned int high = request.params[0].getInt<int>();
    unsigned int low = request.params[1].getInt<int>();
    bool fActiveOnly = false;
    bool fLogicalTS = false;

    if (request.params.size() > 2) {
        if (request.params[2].isObject()) {
            UniValue noOrphans = find_value(request.params[2].get_obj(), "noOrphans");
            UniValue returnLogical = find_value(request.params[2].get_obj(), "logicalTimes");

            if (noOrphans.isBool())
                fActiveOnly = noOrphans.get_bool();

            if (returnLogical.isBool())
                fLogicalTS = returnLogical.get_bool();
        }
    }

    std::vector<std::pair<uint256, unsigned int> > blockHashes;

    if (!GetTimestampIndex(high, low, fActiveOnly, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<uint256, unsigned int> >::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        if (fLogicalTS) {
            UniValue item(UniValue::VOBJ);
            item.pushKV("blockhash", it->first.GetHex());
            item.pushKV("logicalts", (int)it->second);
            result.push_back(item);
        } else {
            result.push_back(it->first.GetHex());
        }
    }

    return result;
}

UniValue getspentinfo(const JSONRPCRequest& request)
{
    RPCHelpMan{"getspentinfo",
        "\nReturns the txid and index where an output is spent.\n",
        {
            {"data", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Transaction data",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hex string of the txid"},
                    {"index", RPCArg::Type::NUM, RPCArg::Optional::NO, "The start block height"},
                },
            },
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
                {RPCResult::Type::NUM, "index", "The spending input index"},
                {RPCResult::Type::ELISION, "", ""},
            }
        },
        RPCExamples{
            HelpExampleCli("getspentinfo", "'{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}'")
            + HelpExampleRpc("getspentinfo", "{\"txid\": \"0437cd7f8525ceed2324359c2d0ba26006d92d856a9c20fa0241106ee5a597c9\", \"index\": 0}")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    UniValue txidValue = find_value(request.params[0].get_obj(), "txid");
    UniValue indexValue = find_value(request.params[0].get_obj(), "index");

    if (!txidValue.isStr() || !indexValue.isNum()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid txid or index");
    }

    uint256 txid = ParseHashV(txidValue, "txid");
    int outputIndex = indexValue.getInt<int>();

    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    if (!GetSpentIndex(key, value)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
    }

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", value.txid.GetHex());
    obj.pushKV("index", (int)value.inputIndex);
    obj.pushKV("height", value.blockHeight);

    return obj;
}

UniValue getaddresstxids(const JSONRPCRequest& request)
{
    RPCHelpMan{"getaddresstxids",
        "\nReturns the txids for an address(es) (requires addressindex to be enabled).\n",
        {
            {"Input params", RPCArg::Type::OBJ, RPCArg::Optional::NO, "Json object",
                {
                    {"addresses", RPCArg::Type::ARR, RPCArg::Optional::NO, "The addresses",
                        {
                            {"address", RPCArg::Type::STR, RPCArg::Optional::OMITTED, "The address"},
                        }
                    },
                    {"start", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "The start block height"},
                    {"end", RPCArg::Type::NUM, RPCArg::Optional::OMITTED_NAMED_ARG, "The end block height"},
                }
            }
        },
        RPCResult{
            RPCResult::Type::ARR, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction id"},
                {RPCResult::Type::ELISION, "", ""},
            },
        },
        RPCExamples{
            HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}'")
            + HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"], \"start\": 5000, \"end\": 5500}'")
            + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"]}")
            + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"1AMHv5kQ2gG5mLUbhhpLErjuuhk1r53tJ2\"], \"start\": 5000, \"end\": 5500}")
        },
    }.Check(request);

    if (!fAddressIndex) {
        throw JSONRPCError(RPC_MISC_ERROR, "Address index not enabled");
    }

    std::vector<std::pair<uint256, int> > addresses;

    if (!getAddressesFromParams(request.params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    int start = 0;
    int end = 0;
    if (request.params[0].isObject()) {
        UniValue startValue = find_value(request.params[0].get_obj(), "start");
        UniValue endValue = find_value(request.params[0].get_obj(), "end");
        if (startValue.isNum() && endValue.isNum()) {
            start = startValue.getInt<int>();
            end = endValue.getInt<int>();
        }
    }

    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;

    for (std::vector<std::pair<uint256, int> >::iterator it = addresses.begin(); it != addresses.end(); it++) {
        if (start > 0 && end > 0) {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex, start, end)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        } else {
            if (!GetAddressIndex((*it).first, (*it).second, addressIndex)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
            }
        }
    }

    std::set<std::pair<int, std::string> > txids;
    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=addressIndex.begin(); it!=addressIndex.end(); it++) {
        int height = it->first.blockHeight;
        std::string txid = it->first.txhash.GetHex();

        if (addresses.size() > 1) {
            txids.insert(std::make_pair(height, txid));
        } else {
            if (txids.insert(std::make_pair(height, txid)).second) {
                result.push_back(txid);
            }
        }
    }

    if (addresses.size() > 1) {
        for (std::set<std::pair<int, std::string> >::const_iterator it=txids.begin(); it!=txids.end(); it++) {
            result.push_back(it->second);
        }
    }

    return result;
}

static UniValue clearmempool(const JSONRPCRequest& request)
{
    RPCHelpMan("clearmempool",
        "\nClears the memory pool and returns a list of the removed transactions.\n",
        {},
        {
            RPCResult{
                RPCResult::Type::ARR, "", "", {
                    {RPCResult::Type::STR_HEX, "hash", "The transaction hash"},
                }
            }
        },
        RPCExamples{
            HelpExampleCli("clearmempool", "")
            + HelpExampleRpc("clearmempool", "")
        }
    ).Check(request);

    auto mempool = ::ChainstateActive().GetMempool();
    if (!mempool)
        throw JSONRPCError(RPC_MISC_ERROR, "No mempool available");

    std::vector<uint256> vtxid;
    mempool->queryHashes(vtxid);

    UniValue removed(UniValue::VARR);
    for (const uint256& hash : vtxid)
        removed.push_back(hash.ToString());

    mempool->clear();

    return removed;
}

void RegisterOmniMiscRPCCommands(CRPCTable &t)
{
// clang-format off
    static const CRPCCommand commands[] =
    { //  category              name                      actor (function)         argNames
        { "blockchain",         "clearmempool",           &clearmempool,           {} },
        { "util",               "getaddresstxids",        &getaddresstxids,        {"addresses"} },
        { "util",               "getaddressdeltas",       &getaddressdeltas,       {"addresses"} },
        { "util",               "getaddressbalance",      &getaddressbalance,      {"addresses"} },
        { "util",               "getaddressutxos",        &getaddressutxos,        {"addresses"} },
        { "util",               "getaddressmempool",      &getaddressmempool,      {"addresses"} },
        { "util",               "getblockhashes",         &getblockhashes,         {"high","low","options"} },
        { "util",               "getspentinfo",           &getspentinfo,           {"argument"} },
    };
// clang-format on

    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
