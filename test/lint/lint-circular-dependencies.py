#!/usr/bin/env python3
#
# Copyright (c) 2020-2022 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Check for circular dependencies

import os
import re
import subprocess
import sys

EXPECTED_CIRCULAR_DEPENDENCIES = (
    "chainparamsbase -> util/system -> chainparamsbase",
    "node/blockstorage -> validation -> node/blockstorage",
    "policy/fees -> txmempool -> policy/fees",
    "qt/addresstablemodel -> qt/walletmodel -> qt/addresstablemodel",
    "qt/recentrequeststablemodel -> qt/walletmodel -> qt/recentrequeststablemodel",
    "qt/sendcoinsdialog -> qt/walletmodel -> qt/sendcoinsdialog",
    "qt/transactiontablemodel -> qt/walletmodel -> qt/transactiontablemodel",
    "wallet/fees -> wallet/wallet -> wallet/fees",
    "wallet/wallet -> wallet/walletdb -> wallet/wallet",
    "kernel/coinstats -> validation -> kernel/coinstats",
    "kernel/mempool_persist -> validation -> kernel/mempool_persist",
    "index/base -> node/context -> net_processing -> index/blockfilterindex -> index/base",
    "txdb -> validation -> txdb",

    # omnicore
    "omnicore/activation -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/activation",
    "omnicore/consensushash -> omnicore/dbspinfo -> omnicore/omnicore -> omnicore/consensushash",
    "omnicore/consensushash -> omnicore/dex -> omnicore/rules -> omnicore/consensushash",
    "omnicore/consensushash -> omnicore/mdex -> omnicore/dbfees -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/consensushash",
    "omnicore/dbaddress -> omnicore/errors -> omnicore/omnicore -> omnicore/dbaddress",
    "omnicore/dbaddress -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbaddress",
    "omnicore/dbfees -> omnicore/sto -> omnicore/omnicore -> omnicore/dbfees",
    "omnicore/dbfees -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbfees",
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/dbspinfo",
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbspinfo",
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/sp -> omnicore/dbspinfo",
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/tx -> omnicore/dbspinfo",
    "omnicore/dbstolist -> omnicore/walletutils -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbstolist",
    "omnicore/dbtradelist -> omnicore/dbtransaction -> omnicore/errors -> omnicore/omnicore -> omnicore/dbtradelist",
    "omnicore/dbtradelist -> omnicore/mdex -> omnicore/dbtradelist",
    "omnicore/dbtradelist -> omnicore/mdex -> omnicore/tx -> omnicore/dbtradelist",
    "omnicore/dbtradelist -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbtradelist",
    "omnicore/dbtransaction -> omnicore/errors -> omnicore/omnicore -> omnicore/dbtransaction",
    "omnicore/dbtransaction -> omnicore/errors -> omnicore/omnicore -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbtransaction",
    "omnicore/dbtxlist -> omnicore/dex -> omnicore/rules -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/omnicore -> omnicore/mdex -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/dex -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/omnicore -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/tx -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dbtxlist",
    "omnicore/dex -> omnicore/omnicore -> omnicore/dex",
    "omnicore/dex -> omnicore/omnicore -> omnicore/persistence -> omnicore/dex",
    "omnicore/dex -> omnicore/omnicore -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/dex",
    "omnicore/dex -> omnicore/tx -> omnicore/dex",
    "omnicore/errors -> omnicore/omnicore -> omnicore/errors",
    "omnicore/mdex -> omnicore/tx -> omnicore/mdex",
    "omnicore/mempool -> omnicore/omnicore -> omnicore/mempool",
    "omnicore/mempool -> omnicore/omnicore -> omnicore/pending -> omnicore/mempool",
    "omnicore/mempool -> omnicore/omnicore -> omnicore/walletutils -> omnicore/mempool",
    "omnicore/mempool -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/mempool",
    "omnicore/nftdb -> omnicore/omnicore -> omnicore/tx -> omnicore/nftdb",
    "omnicore/notifications -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/notifications",
    "omnicore/omnicore -> omnicore/rules -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/script -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/sp -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/tally -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/tx -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/tx -> omnicore/sto -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/utilsbitcoin -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/walletutils -> omnicore/omnicore",
    "omnicore/rules -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/rules",
    "omnicore/tx -> omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/tx",
    "omnicore/utilsbitcoin -> omnicore/validationinterface -> omnicore/utilsbitcoin",
)

CODE_DIR = "src"


def main():
    circular_dependencies = []
    exit_code = 0

    os.chdir(CODE_DIR)
    files = subprocess.check_output(
        ['git', 'ls-files', '--', '*.h', '*.cpp'],
        universal_newlines=True,
    ).splitlines()

    command = [sys.executable, "../contrib/devtools/circular-dependencies.py", *files]
    dependencies_output = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        universal_newlines=True,
    )

    for dependency_str in dependencies_output.stdout.rstrip().split("\n"):
        circular_dependencies.append(
            re.sub("^Circular dependency: ", "", dependency_str)
        )

    # Check for an unexpected dependencies
    for dependency in circular_dependencies:
        if dependency not in EXPECTED_CIRCULAR_DEPENDENCIES:
            exit_code = 1
            print(
                f'A new circular dependency in the form of "{dependency}",\n',
                file=sys.stderr,
            )

    # Check for missing expected dependencies
    for expected_dependency in EXPECTED_CIRCULAR_DEPENDENCIES:
        if expected_dependency not in circular_dependencies:
            exit_code = 1
            print(
                f'Good job! The circular dependency "{expected_dependency}" is no longer present.',
            )
            print(
                f"Please remove it from EXPECTED_CIRCULAR_DEPENDENCIES in {__file__}",
            )
            print(
                "to make sure this circular dependency is not accidentally reintroduced.\n",
            )

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
