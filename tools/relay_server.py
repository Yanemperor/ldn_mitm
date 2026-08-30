#!/usr/bin/env python3
"""
A tiny self-hosted relay server for ldn_mitm's internet play (and for
switch-lan-play in general). Run it on a machine both players can reach - a
low-latency VPS between the players is ideal - then list its address in each
console's sdmc:/config/ldn_mitm/relay.cfg and pick it in the Tesla overlay.

It speaks lan-play's client<->server protocol: UDP, each datagram is
[u8 type][payload].
  type 0x00 KEEPALIVE  - empty; refreshes the idle timer for every IP already
                         learned from this endpoint. It carries no address, so
                         it CANNOT learn a new mapping - a client must send at
                         least one IPv4 frame (ldn_mitm re-broadcasts its
                         advertisement periodically) before it is routable.
  type 0x01 IPV4       - payload is a bare IPv4 packet (IP header + L4 + data)
  type 0x02 PING       - liveness probe; the first 4 bytes are echoed back to
                         the sender (ldn_mitm uses this to detect a dead relay
                         path and reconnect).
  type 0x03 IPV4_FRAG  - fragmented IPv4 for small-MTU paths; forwarded by the
                         src/dst in its 16-byte header, clients reassemble.
  type 0x20 SCOPE      - ldn_mitm extension: [u32 BE session token]. Declares
                         which LDN session this endpoint is in (0 = none).
                         Game traffic (see below) is then forwarded only
                         between endpoints sharing the token, so one shared
                         server no longer sprays every session's packets at
                         every client. Sent every 5s by ldn_mitm; stock
                         lan-play clients never send it and stay unscoped.
  (other types are ignored)

Routing, lan-play semantics plus scoping:
  - source-learn: map (sender's scope token, IPv4 SRC address) to the UDP
    endpoint the frame came from.
  - forward by DST address:
      broadcast (x.x.x.255 / 255.255.255.255):
        discovery frames (UDP dport 11452)   -> every OTHER learned client
                                                (scanning must cross sessions)
        game frames, sender scoped           -> only clients with the same token
        game frames, sender unscoped         -> every other client (stock
                                                lan-play behavior)
      unicast -> the client that owns that IP under the sender's token,
                 falling back to the unscoped table.
  - clients idle for >60s are expired.

Requires only Python 3 (no dependencies).

Usage:
    python relay_server.py                 # bind 0.0.0.0:11451
    python relay_server.py --port 11455
    python relay_server.py -v              # log every relayed packet
    python relay_server.py --membership-url https://api.example/relay/validate

When --membership-url is set, every client needs an App-issued relay
credential. The URL is checked at most once per endpoint per day; a denied
client receives a control packet that makes the console persist Relay OFF.

For play across the internet, forward the chosen UDP port to this machine and
give the players this machine's PUBLIC IP (or a hostname) and port.
"""
import argparse
import socket
import time
import json
import urllib.error
import urllib.request

TYPE_KEEPALIVE = 0x00
TYPE_IPV4 = 0x01
TYPE_PING = 0x02
TYPE_IPV4_FRAG = 0x03
TYPE_SCOPE = 0x20
TYPE_RELAY_CREDENTIAL = 0x21
TYPE_RELAY_DENIED = 0x22
IDLE_TIMEOUT = 60.0   # seconds before a silent client is forgotten
SCOPE_TIMEOUT = 300.0  # seconds before a stale scope declaration is forgotten
DISCOVERY_PORT = 11452  # LANDiscovery control traffic - always crosses scopes
MEMBERSHIP_RECHECK_SECONDS = 24 * 60 * 60


def ip_str(b):
    return ".".join(str(x) for x in b)


def is_broadcast(dst4):
    # 255.255.255.255 or a directed broadcast (last octet 255).
    return dst4 == b"\xff\xff\xff\xff" or dst4[3] == 0xFF


def udp_dport(payload):
    """UDP destination port of a bare IPv4 packet, or None."""
    if len(payload) < 20:
        return None
    ihl = (payload[0] & 0x0F) * 4
    if ihl < 20 or payload[9] != 17 or len(payload) < ihl + 4:
        return None
    return (payload[ihl + 2] << 8) | payload[ihl + 3]


class MembershipAuthorizer:
    """Control-plane membership gate, cached per UDP endpoint for one day."""

    def __init__(self, url):
        self.url = url
        self.allowed_until = {}

    def validate(self, token):
        body = json.dumps({"credential": token}).encode()
        request = urllib.request.Request(self.url, data=body,
            headers={"Content-Type": "application/json"}, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                result = json.load(response)
            return bool(result.get("data", {}).get("allowed"))
        except (OSError, ValueError, urllib.error.HTTPError):
            return False

    def authenticate(self, endpoint, token, now):
        if not token:
            return False
        cached = self.allowed_until.get(endpoint)
        if cached and cached[0] == token and cached[1] > now:
            return True
        if not self.validate(token):
            self.allowed_until.pop(endpoint, None)
            return False
        self.allowed_until[endpoint] = (token, now + MEMBERSHIP_RECHECK_SECONDS)
        return True

    def allows(self, endpoint, now):
        cached = self.allowed_until.get(endpoint)
        return cached is not None and cached[1] > now


def main():
    ap = argparse.ArgumentParser(description="Self-hosted lan-play/ldn_mitm relay server.")
    ap.add_argument("--port", "-p", type=int, default=11451, help="UDP port to bind (default 11451)")
    ap.add_argument("--bind", default="0.0.0.0", help="address to bind (default 0.0.0.0)")
    ap.add_argument("--verbose", "-v", action="store_true", help="log every relayed packet")
    ap.add_argument("--membership-url", help="control-plane relay credential validator; enables VIP-only access")
    args = ap.parse_args()

    membership = MembershipAuthorizer(args.membership_url) if args.membership_url else None

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    print(f"[relay] listening on {args.bind}:{args.port} (Ctrl-C to stop)")

    # (scope token, virtual-src 4 bytes) -> (udp_endpoint, last_seen)
    clients = {}
    # udp_endpoint -> (scope token, last_declared); absent/expired = unscoped (0)
    scopes = {}
    # endpoints seen, so a brand-new one is announced once
    endpoints = set()

    def scope_of(ep, now):
        entry = scopes.get(ep)
        if entry is None:
            return 0
        token, seen = entry
        if now - seen > SCOPE_TIMEOUT:
            del scopes[ep]
            return 0
        return token

    def active_endpoints(now, exclude=None, token=None):
        """Live client endpoints (deduped, idle ones expired). token=None means
        every endpoint; otherwise endpoints in that scope, plus any that have
        not declared one.

        Matching unscoped endpoints too is deliberate. Peers declare their
        scope on a timer, so for a moment after a session forms one side is
        scoped and the other is not; requiring an exact match there silently
        drops the sender's traffic - 70 frames went missing that way in
        testing, right where a session handshake happens. Stock lan-play
        clients never declare a scope at all and would otherwise be cut off
        entirely. Scoping is a bandwidth and privacy optimisation, so failing
        open costs a little leakage; failing closed costs the session, and the
        receiving console drops non-session frames anyway."""
        eps = set()
        for key, (ep, seen) in list(clients.items()):
            if now - seen > IDLE_TIMEOUT:
                del clients[key]
                continue
            if ep == exclude:
                continue
            if token is not None:
                ep_scope = scope_of(ep, now)
                if ep_scope != token and ep_scope != 0:
                    continue
            eps.add(ep)
        return eps

    while True:
        try:
            data, addr = sock.recvfrom(4096)
        except ConnectionResetError:
            # Windows: a prior send to a since-closed port bounced.
            continue
        if not data:
            continue

        if addr not in endpoints:
            endpoints.add(addr)
            print(f"[relay] new client {addr[0]}:{addr[1]}  ({len(active_endpoints(time.time())) + 1} online)")

        now = time.time()
        msg_type, payload = data[0], data[1:]

        if membership:
            if msg_type == TYPE_RELAY_CREDENTIAL:
                try:
                    token = payload.decode("ascii") if 0 < len(payload) <= 127 else ""
                except UnicodeDecodeError:
                    token = ""
                if not membership.authenticate(addr, token, now):
                    sock.sendto(bytes((TYPE_RELAY_DENIED,)), addr)
                    if args.verbose:
                        print(f"[relay] membership denied {addr[0]}:{addr[1]}")
                continue
            if not membership.allows(addr, now):
                sock.sendto(bytes((TYPE_RELAY_DENIED,)), addr)
                continue

        if msg_type == TYPE_KEEPALIVE:
            # Refresh (never learn) mappings for this endpoint - keepalives
            # carry no address.
            for key, (ep, _) in list(clients.items()):
                if ep == addr:
                    clients[key] = (ep, now)
            continue

        if msg_type == TYPE_PING:
            # Echo the first 4 bytes back (lan-play server semantics).
            try:
                sock.sendto(data[:4], addr)
            except OSError:
                pass
            continue

        if msg_type == TYPE_SCOPE:
            if len(payload) < 4:
                continue
            token = int.from_bytes(payload[0:4], "big")
            prev = scope_of(addr, now)
            scopes[addr] = (token, now)
            if token != prev:
                # Re-key this endpoint's learned addresses so unicast keeps
                # working across the transition (join/leave) without waiting
                # for its next data frame.
                for (old_token, vsrc), (ep, seen) in list(clients.items()):
                    if ep == addr and old_token == prev:
                        del clients[(old_token, vsrc)]
                        clients[(token, vsrc)] = (ep, seen)
                if args.verbose:
                    print(f"[relay] scope {addr[0]}:{addr[1]} {prev:#x} -> {token:#x}")
            # Opportunistic cleanup of stale declarations.
            if len(scopes) > 256:
                for ep in [e for e, (_, seen) in scopes.items() if now - seen > SCOPE_TIMEOUT]:
                    del scopes[ep]
            continue

        # Route IPv4 frames by the packet's addresses, fragments by their
        # frag-header addresses (src[4] dst[4] at offsets 0/4); fragments are
        # forwarded as-is, the receiving client reassembles.
        if msg_type == TYPE_IPV4 and len(payload) >= 20:
            src4 = payload[12:16]
            dst4 = payload[16:20]
            dport = udp_dport(payload)
        elif msg_type == TYPE_IPV4_FRAG and len(payload) >= 16:
            src4 = payload[0:4]
            dst4 = payload[4:8]
            # Only part 0 carries the headers; treat all fragments as game
            # traffic (discovery packets never fragment - they fit one frame).
            dport = None
        else:
            continue

        token = scope_of(addr, now)
        prev = clients.get((token, src4))
        clients[(token, src4)] = (addr, now)
        if prev is None or prev[0] != addr:
            print(f"[relay] learn {ip_str(src4)} (scope {token:#x}) -> {addr[0]}:{addr[1]}")

        if is_broadcast(dst4):
            # Discovery must cross scopes (a scanner is not in a session yet);
            # game traffic stays inside the sender's session. An unscoped
            # sender (stock lan-play client) keeps the classic
            # broadcast-to-everyone behavior.
            if dport == DISCOVERY_PORT or token == 0:
                targets = active_endpoints(now, exclude=addr)
            else:
                targets = active_endpoints(now, exclude=addr, token=token)
            sent = 0
            for ep in targets:
                try:
                    sock.sendto(data, ep)
                    sent += 1
                except OSError:
                    pass
            if args.verbose:
                kind = "disc" if dport == DISCOVERY_PORT else "game"
                print(f"[relay] bcast {ip_str(src4)} -> {ip_str(dst4)} "
                      f"({len(payload)}B {kind} scope {token:#x}) x{sent}")
        else:
            # Own-scope owner first; unscoped as the transition/compat
            # fallback (a peer whose scope declaration hasn't arrived yet).
            owner = clients.get((token, dst4)) or clients.get((0, dst4))
            if owner and owner[0] != addr:
                try:
                    sock.sendto(data, owner[0])
                    if args.verbose:
                        print(f"[relay] ucast {ip_str(src4)} -> {ip_str(dst4)} "
                              f"({len(payload)}B scope {token:#x})")
                except OSError:
                    pass


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[relay] stopped")
