/*
 * Copyright (c) 2018 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <switch.h>
#include <stratosphere.hpp>

namespace ams::mitm::ldn {

    /* On-console internet relay client (docs/internet-relay-plan.md): two
       consoles on different networks play an LDN game with no PC, by relaying
       LDN discovery/join and game traffic through a lan-play relay server
       (UDP, [u8 type][payload]; keepalive 0x00, IPv4 0x01). Off unless
       configured AND enabled in the overlay. */
    namespace relay {

        /* Server list config; format documented at LoadConfig (relay_client.cpp). */
        constexpr const char RelayConfigPath[] = "sdmc:/config/ldn_mitm/relay.cfg";
        /* App-issued, revocable credential.  It deliberately lives outside
           relay.cfg so the public relay profile never carries a secret. */
        constexpr const char RelayCredentialPath[] = "sdmc:/config/ldn_mitm/relay.credential";
        /* The control plane keeps the lease stable; retaining the last lease
           locally lets a game start without first opening the App again. */
        constexpr const char RelayVirtualIpPath[] = "sdmc:/config/ldn_mitm/relay.virtual_ip";
        constexpr size_t RelayCredentialMaxLen = 127;
        constexpr int MaxServers    = 8;
        constexpr int ServerNameLen = 32;

        /* Read RelayConfigPath into the server list. Call once at startup
           (from Main). Safe if the file is missing. */
        void LoadConfig();

        /* Effective relay gate: user enabled it AND a server is configured.
           When true, LANDiscovery keeps the console on the internet and
           mirrors discovery/join over the relay, and the bsd:u mitm relays +
           serves game session traffic. When false, shipped local-only LDN. */
        bool IsEnabled();

        /* Runtime on/off toggle (independent of whether servers exist), and
           the server picker - all driven by the Tesla overlay via the config
           IPC service. */
        void SetRelayEnabled(bool on);
        /* Rewrite relay.cfg's directive lines (enabled/broadcast/selected)
           from current runtime state; called by the config service when the
           broadcast-relay toggle changes (the other setters persist
           themselves). */
        void PersistSettings();
        bool GetRelayEnabled();
        int  ServerCount();
        const char *ServerName(int index);   /* "" if out of range */
        int  SelectedServer();
        void SelectServer(int index);

        /* Relay virtual IPv4 state, in host byte order. SetVirtualIp is the
           only active writer; zero means the caller has not configured it. */
        constexpr Result ResultVirtualIpNotConfigured = MAKERESULT(0xFD, 103);
        constexpr Result ResultRelayCredentialMissing = MAKERESULT(0xFD, 104);
        Result SetVirtualIp(u32 ip);
        u32 GetVirtualIp();
        Result RequireVirtualIp(u32 *out_ip = nullptr);
        Result SetRelayCredential(const void *credential, size_t size);
        Result RequireRelayCredential();

        /* Selected relay server. ServerIp is the literal IPv4 (host order) or
           0 for a hostname; ServerHost is the raw address token (RelayTransport
           resolves a hostname at connect time). Valid when IsEnabled(). */
        u32 ServerIp();
        const char *ServerHost();
        u16 ServerPort();

        /* Thread-safe entry for bsd:u IPC threads: wrap the payload and send to
           the relay. Returns -1 when the transport is closed. dport host order. */
        int BridgeSendGameBroadcast(const void *payload, size_t len, u16 dport);

        /* Like BridgeSendGameBroadcast but for a unicast to a specific peer:
           wraps the payload with dst = dst_ip (host order) so the relay routes
           it to the client that owns that address. Games that switch from a
           broadcast handshake to unicast session traffic need this - the peer
           IP is unroutable across the internet otherwise. */
        int BridgeSendGameUnicast(const void *payload, size_t len, u16 dport, u32 dst_ip);

        /* Reusable relay socket: the proven observer setup (internet nifm
           request + registered UDP socket connected to the relay) plus
           helpers to send/recv a LANPacket as an IPv4/UDP broadcast frame.
           Not thread-safe; owned and driven by one LANDiscovery worker. */
        class RelayTransport {
            public:
                RelayTransport() = default;
                ~RelayTransport() { this->Close(); }

                /* Acquire internet, open+register+connect the socket. The
                   virtual src is the configured 10.13.x.x address. */
                Result Open();
                void Close();
                bool IsOpen() const { return m_fd >= 0; }
                int GetFd() const { return m_fd; }

                /* Wrap a LANPacket as an IPv4/UDP broadcast (virtual src ->
                   10.13.255.255:11452) and send it to the relay. */
                int SendBroadcast(const void *lan_packet, size_t size);

                /* Keep our registration alive while otherwise silent (an idle
                   host). Call periodically from the worker. */
                int SendKeepalive();

                /* Path-liveness probe: keepalives are send-only, so send a
                   PING (0x02) the server echoes back. Counts a miss until any
                   frame arrives. Call once per beacon tick. */
                int SendPing();

                /* True when the server path is gone: socket closed, or - only
                   against a server that has echoed at least one ping (some
                   relays don't implement 0x02) - several pings unanswered.
                   The owner should Close()+Open() to recover. */
                bool PathDead() const;

                /* Receive one relay frame. A discovery frame (UDP dst 11452) is
                   copied to out (returns length, sets *out_src_ip to its virtual
                   IP); a peer game frame is handed to GameRx (returns 0). <0 on
                   error. */
                int RecvBroadcast(void *out, size_t max_size, u32 *out_src_ip);

                /* Wrap a game datagram (src = our real IP, dst = 255.255.255.255,
                   sport = dport) and send to the relay. Called from bsd:u mitm
                   threads via BridgeSendGameBroadcast. */
                int SendGameBroadcast(const void *payload, size_t len, u16 dport);

                /* As SendGameBroadcast but dst = dst_ip (host order): the relay
                   routes it to the owning peer. When the peer's virtual relay
                   address is known (LearnPeer) the frame is addressed vsrc ->
                   vsrc with the real IPs carried in a shim, so the server
                   never has to route by a (commonly colliding) private LAN
                   address; otherwise falls back to the legacy real-IP frame. */
                int SendGameUnicast(const void *payload, size_t len, u16 dport, u32 dst_ip);

                /* Record a peer's real-IP <-> virtual-relay-address pair,
                   learned by the discovery layer from the outer source of its
                   control packets (ScanResp/Connect/SyncNetwork/heartbeat).
                   Thread-safe; last write per real IP wins. */
                void LearnPeer(u32 real_ip, u32 vsrc);

                /* Tell a scope-aware relay server (tools/relay_server.py)
                   which session we are in (0 = none) so it stops forwarding
                   other sessions' game traffic to us. Stock lan-play servers
                   ignore the unknown message type. */
                int SendScope(u32 token);

            private:
                int SendRelayCredential();
                /* Build [0x01][IPv4+UDP+payload] and send to the relay. */
                int SendWrapped(u32 src, u32 dst, u16 sport, u16 dport, u16 ip_id, const void *payload, size_t len);

                /* Split a wrapped packet too large for one relay frame across
                   IPV4_FRAG frames (the receive side already reassembles). */
                int SendFragmented(u32 src, u32 dst, const u8 *packet, size_t total);

                /* Shared tail of RecvBroadcast: route one bare IPv4 packet
                   (whole or reassembled) to the discovery caller or GameRx. */
                int ProcessIpv4(const u8 *ip, size_t iplen, void *out, size_t max_size, u32 *out_src_ip);

                /* One lan-play IPV4_FRAG (0x03) frame: stash the part; returns
                   the completed packet (sets *out_len) or nullptr. */
                const u8 *ReassembleFrag(const u8 *p, size_t n, size_t *out_len);

                /* Resolve a hostname to IPv4 (host order) via a hand-built DNS
                   query (libnx getaddrinfo aborts in this sysmodule). 0 on
                   failure. */
                u32 ResolveHostname(const char *host);

                /* Queue a relay-received peer frame for the bsd:u RecvFrom to
                   serve with the peer's real source address. */
                void InjectGameFrame(const u8 *ip, size_t iplen);

                int m_fd = -1;
                NifmRequest m_req{};
                bool m_have_req = false;
                bool m_have_nifm_session = false;
                u16 m_frag_send_id = 0;
                u32 m_rsrc = 0;   /* our REAL IP, host order */

                /* real IP -> virtual relay address of session peers, filled by
                   LearnPeer (worker thread) and read by the game send path
                   (bsd:u IPC threads), hence the mutex. Small: one entry per
                   console we have heard from. */
                static constexpr int PeerMapMax = 16;
                struct PeerAddr { u32 real; u32 vsrc; };
                mutable os::SdkMutex m_peer_mutex;
                PeerAddr m_peers[PeerMapMax] = {};
                int m_peer_next = 0;   /* round-robin evict when full */
                u32 LookupVsrc(u32 real_ip) const;

                /* Ping liveness (see SendPing/PathDead). Reset by Open(). */
                static constexpr u32 PingMaxMisses = 4;
                u32 m_ping_misses = 0;
                bool m_ping_supported = false;

                /* Reassembly slots for peers that fragment (pmtu-configured
                   lan-play clients). Any accepted packet is <= 1500, so 1600
                   bounds every in-slot write. */
                static constexpr int FragSlots = 4;
                struct FragSlot {
                    bool used;
                    u16 id;
                    u32 src;
                    u8 mask;
                    u16 total_len;
                    u8 buffer[1600];
                };
                FragSlot m_frags[FragSlots] = {};
                u8 m_frag_evict = 0;
        };

    }

}
