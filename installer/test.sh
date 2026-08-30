#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
BIN="$(mktemp -d)/ryulink-installer-core-test"
swiftc -framework CryptoKit "$ROOT/installer/InstallerCore.swift" "$ROOT/installer/InstallerCoreTests.swift" -o "$BIN"
"$BIN"
