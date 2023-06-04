// Copyright (c) 2009-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPC_REGISTER_H
#define BITCOIN_RPC_REGISTER_H

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

/** These are in one header file to avoid creating tons of single-function
 * headers for everything under src/rpc/ */
class CRPCTable;

void RegisterBlockchainRPCCommands(CRPCTable &tableRPC);
void RegisterFeeRPCCommands(CRPCTable&);
void RegisterMempoolRPCCommands(CRPCTable&);
void RegisterMiningRPCCommands(CRPCTable &tableRPC);
void RegisterNodeRPCCommands(CRPCTable&);
void RegisterNetRPCCommands(CRPCTable&);
void RegisterOutputScriptRPCCommands(CRPCTable&);
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
void RegisterSignMessageRPCCommands(CRPCTable&);
void RegisterSignerRPCCommands(CRPCTable &tableRPC);
void RegisterTxoutProofRPCCommands(CRPCTable&);

void RegisterOmniDataRetrievalRPCCommands(CRPCTable &tableRPC);
#ifdef ENABLE_WALLET
void RegisterOmniTransactionCreationRPCCommands(CRPCTable &tableRPC);
#endif
void RegisterOmniPayloadCreationRPCCommands(CRPCTable &tableRPC);
void RegisterOmniRawTransactionRPCCommands(CRPCTable &tableRPC);
void RegisterOmniMiscRPCCommands(CRPCTable &tableRPC);

static inline void RegisterAllCoreRPCCommands(CRPCTable &t)
{
    RegisterBlockchainRPCCommands(t);
    RegisterFeeRPCCommands(t);
    RegisterMempoolRPCCommands(t);
    RegisterMiningRPCCommands(t);
    RegisterNodeRPCCommands(t);
    RegisterNetRPCCommands(t);
    RegisterOutputScriptRPCCommands(t);
    RegisterRawTransactionRPCCommands(t);
    RegisterSignMessageRPCCommands(t);
#ifdef ENABLE_EXTERNAL_SIGNER
    RegisterSignerRPCCommands(t);
#endif // ENABLE_EXTERNAL_SIGNER
    RegisterTxoutProofRPCCommands(t);

    /* Omni Core RPCs: */
    RegisterOmniMiscRPCCommands(t);
    RegisterOmniDataRetrievalRPCCommands(t);
#ifdef ENABLE_WALLET
    RegisterOmniTransactionCreationRPCCommands(t);
#endif
    RegisterOmniPayloadCreationRPCCommands(t);
    RegisterOmniRawTransactionRPCCommands(t);
}

#endif // BITCOIN_RPC_REGISTER_H
