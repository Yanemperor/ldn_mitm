#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
PAYLOAD="$ROOT/out/sd"
APP="$ROOT/dist/RyuLink Installer.app"

for RELATIVE in switch/RyuLink/RyuLink.nro atmosphere/contents/4200000000000010/exefs.nsp atmosphere/contents/4200000000000010/flags/boot2.flag config/ldn_mitm/relay.cfg; do
  [[ -f "$PAYLOAD/$RELATIVE" ]] || { print -u2 "Missing payload file: $RELATIVE"; exit 1; }
done

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$ROOT/installer/Info.plist" "$APP/Contents/Info.plist"
for RELATIVE in switch/RyuLink/RyuLink.nro atmosphere/contents/4200000000000010/exefs.nsp atmosphere/contents/4200000000000010/flags/boot2.flag config/ldn_mitm/relay.cfg; do
  mkdir -p "$APP/Contents/Resources/payload/${RELATIVE:h}"
  cp "$PAYLOAD/$RELATIVE" "$APP/Contents/Resources/payload/$RELATIVE"
done
swiftc -O -framework AppKit -framework SwiftUI -framework CryptoKit \
  "$ROOT/installer/InstallerCore.swift" "$ROOT/installer/RyuLinkInstaller.swift" \
  -o "$APP/Contents/MacOS/RyuLinkInstaller"
codesign --force --sign - "$APP"
print "Built: $APP"
