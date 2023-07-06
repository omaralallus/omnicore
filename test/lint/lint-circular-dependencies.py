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

    # omnicore
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/dbspinfo",
    "omnicore/dbtradelist -> omnicore/mdex -> omnicore/dbtradelist",
    "omnicore/dbtxlist -> omnicore/dex -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/omnicore -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/tx -> omnicore/dbtxlist",
    "omnicore/dex -> omnicore/omnicore -> omnicore/dex",
    "omnicore/dex -> omnicore/tx -> omnicore/dex",
    "omnicore/mdex -> omnicore/tx -> omnicore/mdex",
    "omnicore/omnicore -> omnicore/rules -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/script -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/sp -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/tally -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/tx -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/utilsbitcoin -> omnicore/omnicore",
    "omnicore/omnicore -> omnicore/walletutils -> omnicore/omnicore",
    "omnicore/omnicore -> validation -> omnicore/omnicore",
    "txdb -> validation -> txdb",
    "init -> txdb -> omnicore/omnicore -> init",
    "node/blockstorage -> txdb -> omnicore/omnicore -> node/blockstorage",
    "omnicore/consensushash -> omnicore/dbspinfo -> omnicore/omnicore -> omnicore/consensushash",
    "omnicore/consensushash -> omnicore/dex -> omnicore/rules -> omnicore/consensushash",
    "omnicore/dbfees -> omnicore/sto -> omnicore/omnicore -> omnicore/dbfees",
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/sp -> omnicore/dbspinfo",
    "omnicore/dbspinfo -> omnicore/omnicore -> omnicore/tx -> omnicore/dbspinfo",
    "omnicore/dbtradelist -> omnicore/mdex -> omnicore/tx -> omnicore/dbtradelist",
    "omnicore/dbtransaction -> omnicore/errors -> omnicore/omnicore -> omnicore/dbtransaction",
    "omnicore/dbtxlist -> omnicore/omnicore -> omnicore/mdex -> omnicore/dbtxlist",
    "omnicore/dbtxlist -> omnicore/dex -> omnicore/rules -> omnicore/dbtxlist",
    "omnicore/dex -> omnicore/omnicore -> omnicore/persistence -> omnicore/dex",
    "omnicore/nftdb -> omnicore/omnicore -> omnicore/tx -> omnicore/nftdb",
    "omnicore/omnicore -> omnicore/tx -> omnicore/sto -> omnicore/omnicore",
    "index/base -> node/context -> net_processing -> index/blockfilterindex -> index/base",
    "index/txindex -> node/blockstorage -> txdb -> omnicore/omnicore -> index/txindex",
    "init -> txdb -> omnicore/omnicore -> omnicore/walletutils -> init",
    "net_processing -> node/blockstorage -> txdb -> omnicore/omnicore -> node/context -> net_processing",
    "node/blockstorage -> txdb -> omnicore/omnicore -> wallet/wallet -> psbt -> node/transaction -> node/blockstorage",
    "index/txindex -> node/blockstorage -> txdb -> omnicore/omnicore -> wallet/wallet -> psbt -> node/transaction -> index/txindex",
    "net_processing -> node/blockstorage -> txdb -> omnicore/omnicore -> wallet/wallet -> psbt -> node/transaction -> net_processing",

    # Temporary, removed in followup https://github.com/bitcoin/bitcoin/pull/24230
    "index/base -> node/context -> net_processing -> index/blockfilterindex -> index/base",
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
                f'A new circular dependency in the form of "{dependency}" appears to have been introduced.\n',
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
