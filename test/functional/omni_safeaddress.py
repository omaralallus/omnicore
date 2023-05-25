#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Omni safe addresses."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (assert_equal, assert_raises_rpc_error)

class OmniSafeAddressesTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-omnisafeaddresses=1']]

    def run_test(self):
        self.log.info("check omni address functionality")

        node = self.nodes[0]
        # Get address for mining and token issuance
        node.createwallet(wallet_name="w0", descriptors=True)
        token_address = node.getnewaddress()
        omni_address = node.omni_encodeaddress(token_address)
        assert_equal(omni_address[0:5], "ocrt1")

        self.generatetoaddress(node, 110, token_address)
        node.sendmany("", {"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP": 10, token_address: 4})
        self.generatetoaddress(node, 1, token_address)

        # query by both omni and btc address
        assert_equal(node.omni_getbalance(omni_address, 1),
                     node.omni_getbalance(token_address, 1))

        assert_equal(node.omni_getallbalancesforaddress(omni_address),
                     node.omni_getallbalancesforaddress(token_address))

        # token_address is rejected since it's a bitcoin address
        error_str = 'Omni address'
        assert_raises_rpc_error(-5, error_str, node.omni_send, omni_address, token_address, 1, "10")
        assert_raises_rpc_error(-5, error_str, node.omni_sendall, omni_address, token_address, 1)
        assert_raises_rpc_error(-5, error_str, node.omni_sendanydata, omni_address, "64", token_address)
        assert_raises_rpc_error(-5, error_str, node.omni_sendfreeze, omni_address, token_address, 1, "10")
        assert_raises_rpc_error(-5, error_str, node.omni_sendunfreeze, omni_address, token_address, 1, "10")
        assert_raises_rpc_error(-5, error_str, node.omni_sendgrant, omni_address, token_address, 1, "10")
        assert_raises_rpc_error(-5, error_str, node.omni_senddexpay, omni_address, token_address, 1, "10")
        assert_raises_rpc_error(-5, error_str, node.omni_sendchangeissuer, omni_address, token_address, 1)
        assert_raises_rpc_error(-5, error_str, node.omni_senddexaccept, omni_address, token_address, 1, "10")
        assert_raises_rpc_error(-5, error_str, node.omni_sendnonfungible, omni_address, token_address, 1, 0, 1)
        assert_raises_rpc_error(-5, error_str, node.omni_funded_send, omni_address, token_address, 1, "10", omni_address)
        assert_raises_rpc_error(-5, error_str, node.omni_funded_send, omni_address, omni_address, 1, "10", token_address)
        assert_raises_rpc_error(-5, error_str, node.omni_funded_sendall, omni_address, token_address, 1, omni_address)
        assert_raises_rpc_error(-5, error_str, node.omni_funded_sendall, omni_address, omni_address, 1, token_address)
        assert_raises_rpc_error(-5, error_str, node.omni_sendtomany, omni_address, 1, [{"address": token_address, "amount": "10"}])

        new_address = node.getnewaddress()
        new_omni_address = node.omni_encodeaddress(new_address)
        assert_equal(node.omni_getbalance(new_omni_address, 1)['balance'], '0.00000000')
        assert_equal(new_omni_address[0:5], "ocrt1")

        # new omni address is accepted
        node.omni_send(omni_address, new_omni_address, 1, "10")
        self.generatetoaddress(node, 1, token_address)
        assert_equal(node.omni_getbalance(new_omni_address, 1)['balance'], '10.00000000')
        assert_equal(node.omni_getbalance(new_address, 1),
                     node.omni_getbalance(new_omni_address, 1))

        # old omni address is accepted
        old_omni_address1 = node.getnewaddress(address_type='legacy')
        node.sendmany("", {old_omni_address1: 2}) # for inputs
        node.omni_send(omni_address, old_omni_address1, 1, "2")
        self.generatetoaddress(node, 1, token_address)
        assert_equal(node.omni_getbalance(old_omni_address1, 1)['balance'], '2.00000000')

        old_omni_address2 = node.getnewaddress(address_type='p2sh-segwit')
        node.omni_send(old_omni_address1, old_omni_address2, 1, "1")
        self.generatetoaddress(node, 1, token_address)
        assert_equal(node.omni_getbalance(old_omni_address1, 1),
                     node.omni_getbalance(old_omni_address2, 1))

if __name__ == '__main__':
    OmniSafeAddressesTest().main()

