#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
BIN="$(mktemp -d)/ryulink-installer-package-test"
PAYLOAD="$ROOT/dist/RyuLink Installer.app/Contents/Resources/payload"
swiftc -framework CryptoKit "$ROOT/installer/InstallerCore.swift" "$ROOT/installer/InstallerPackageTests.swift" -o "$BIN"
"$BIN" "$PAYLOAD"
