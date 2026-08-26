# RyuLink

RyuLink is the standalone Nintendo Switch Homebrew client for signing in,
choosing a room, then returning to HOME to launch a game in local wireless
mode. The persistent `ldn_mitm` sysmodule performs the later game-session
connection through the fixed RyuLink relay profile; RyuLink does not stay open
while the game runs.

The native 1280×720 app uses Logto Device Flow, then loads the authenticated
player profile and the single PUBLIC room from the control plane.
It also displays and saves the available-node preference. Player credentials
remain only in memory; closing or cancelling the app starts a fresh Device
Flow on the next launch.

Joining a room confirms the player's product-level room membership. For the
current MVP, every entry uses the same fixed `ldn_mitm` relay profile and is
therefore in one shared virtual LAN. Press X on an active room selection to
call `leave` and clear the App-local selection.

The MVP shows one PUBLIC room. Network transport is not selected by room. The
App performs no startup version check and has no in-app update UI.
User-triggered network actions show a loading overlay and each HTTP request has
a 5-second timeout; failures release the overlay and leave the App usable.

The relay profile is fixed at `sdmc:/config/ldn_mitm/relay.cfg`. It selects the
RyuLink relay and enables Internet Relay at boot; it is part of the SD package,
not a response from the control plane.

Before confirming a room selection, the App reads the public `ldn_mitm`
configuration service and requires that the sysmodule, Internet Relay, and a
Relay selection are ready. It does not write configuration or send room data to
the core.

This MVP does not implement multi-room relay isolation, payment, automatic
updates, network probes, or membership flows beyond the existing read-only
profile information.

## Build

Build from the repository root:

```bash
docker compose run --rm devkit
```

Copy the contents of `out/sd/` to the SD-card root. The relevant files are:

```text
switch/RyuLink/RyuLink.nro
atmosphere/contents/4200000000000010/exefs.nsp
config/ldn_mitm/relay.cfg
```
