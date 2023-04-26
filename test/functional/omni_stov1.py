#!/usr/bin/env python3
# Copyright (c) 2017-2018 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Send To Ownders V1."""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal

class OmniSendToOwnersV1(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [['-omniactivationallowsender=any']]

    def run_test(self):
        self.log.info("test Send To Owners V1")

        # Preparing some mature Bitcoins
        self.nodes[0].createwallet(wallet_name="w0", descriptors=True)
        coinbase_address = self.nodes[0].getnewaddress()
        self.generatetoaddress(self.nodes[0], 101, coinbase_address)

        # Obtaining a master address to work with
        address = self.nodes[0].getnewaddress()

        # Funding the address with some testnet BTC for fees
        self.nodes[0].sendtoaddress(address, 20)
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Participating in the Exodus crowdsale to obtain some OMNI
        txid = self.nodes[0].sendmany("", {"moneyqMan7uh8FqdCA2BV5yZ8qVrc9ikLP": 10, address: 4})
        self.generatetoaddress(self.nodes[0], 10, coinbase_address)

        # Checking the transaction was valid.
        result = self.nodes[0].gettransaction(txid)
        assert_equal(result['confirmations'], 10)

        # Creating an indivisible test property
        self.nodes[0].omni_sendissuancefixed(address, 1, 1, 0, "Z_TestCat", "Z_TestSubCat", "Z_IndivisTestProperty", "Z_TestURL", "Z_TestData", "100")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Creating a divisible test property
        self.nodes[0].omni_sendissuancefixed(address, 1, 2, 0, "Z_TestCat", "Z_TestSubCat", "Z_DivisTestProperty", "Z_TestURL", "Z_TestData", "10000")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Generating addresses to use as STO recipients
        addresses = []
        for x in range(0, 10):
            addresses.append(self.nodes[0].getnewaddress())

        # Seeding a total of 100 SP#3

        # Seeding address 1 with 5% = 5 SP#3
        self.nodes[0].omni_send(address, addresses[1], 3, "5")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Seeding address 2 with 10% = 10 SP#3
        self.nodes[0].omni_send(address, addresses[2], 3, "10")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Seeding address 3 with 15% = 15 SP#3
        self.nodes[0].omni_send(address, addresses[3], 3, "15")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Seeding address 4 with 20% = 20 SP#3
        self.nodes[0].omni_send(address, addresses[4], 3, "20")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Seeding address 5 with 25% = 25 SP#3
        self.nodes[0].omni_send(address, addresses[5], 3, "25")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Seeding address 6 with 25% = 25 SP#3
        self.nodes[0].omni_send(address, addresses[6], 3, "25")
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Testing a cross property (v1) STO, distributing 1000.00 SPT #4 to holders of SPT #3
        txid = self.nodes[0].omni_sendsto(address, 4, "1000", "", 3)
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Checking the STO transaction was invalid (feature not yet activated)...
        result = self.nodes[0].omni_gettransaction(txid)
        assert_equal(result['valid'], False)

        # Activating cross property (v1) Send To Owners...
        activation_block = self.nodes[0].getblockcount() + 8
        txid = self.nodes[0].omni_sendactivation(address, 10, activation_block, 999)
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Checking the transaction was valid...
        result = self.nodes[0].omni_gettransaction(txid)
        assert_equal(result['valid'], True)

        # Mining 10 blocks to forward past the activation block
        self.generatetoaddress(self.nodes[0], 10, coinbase_address)

        # Checking the activation went live as expected...
        featureid = self.nodes[0].omni_getactivations()['completedactivations']
        allpairs = False
        for ids in featureid:
            if ids['featureid'] == 10:
                allpairs = True
        assert_equal(allpairs, True)

        # Testing a cross property (v1) STO, distributing 1000.00 SPT #4 to holders of SPT #3
        txid = self.nodes[0].omni_sendsto(address, 4, "1000", "", 3)
        self.generatetoaddress(self.nodes[0], 1, coinbase_address)

        # Checking the STO transaction was valid...
        result = self.nodes[0].omni_gettransaction(txid)
        assert_equal(result['valid'], True)

        # Checking address 1 received 5% of the distribution (50.00 SPT #4)...
        result = self.nodes[0].omni_getbalance(addresses[1], 4)
        assert_equal(result['balance'], "50.00000000")

        # Checking address 2 received 10% of the distribution (100.00 SPT #4)...
        result = self.nodes[0].omni_getbalance(addresses[2], 4)
        assert_equal(result['balance'], "100.00000000")

        # Checking address 3 received 15% of the distribution (150.00 SPT #4)...
        result = self.nodes[0].omni_getbalance(addresses[3], 4)
        assert_equal(result['balance'], "150.00000000")

        # Checking address 4 received 20% of the distribution (200.00 SPT #4)...
        result = self.nodes[0].omni_getbalance(addresses[4], 4)
        assert_equal(result['balance'], "200.00000000")

        # Checking address 5 received 20% of the distribution (250.00 SPT #4)...
        result = self.nodes[0].omni_getbalance(addresses[5], 4)
        assert_equal(result['balance'], "250.00000000")

        # Checking address 6 received 20% of the distribution (250.00 SPT #4)...
        result = self.nodes[0].omni_getbalance(addresses[6], 4)
        assert_equal(result['balance'], "250.00000000")

if __name__ == '__main__':
    OmniSendToOwnersV1().main()
