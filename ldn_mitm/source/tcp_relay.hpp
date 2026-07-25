#pragma once
#include <switch.h>
#include <stratosphere.hpp>

namespace ams::mitm::ldn::tcprelay {

    /* TCP session relay (docs/tcp-relay-plan.md). Some titles run their LDN
       session over TCP, which the UDP relay cannot carry - and their connect()
       fails in the console's own stack before we ever see a packet. Instead of
       implementing TCP, proxy it: the game gets a real local TCP connection to
       us, and we tunnel the byte stream to the peer's ldn_mitm, which connects
       to its own game's listener. */

    /* Tunnel frames travel as ordinary relay IPv4/UDP frames addressed to this
       port, so the relay server routes them like any game traffic. */
    constexpr u16 TcpTunnelPort = 11453;

    /* Called from the bsd mitm for connect() (cmd 14) when the destination is
       a known relay peer. Redirects the connection to a local proxy listener
       and issues it on the game's forward session; the game ends up with a
       genuine TCP connection to us. Returns false if the connection should be
       forwarded unchanged instead (not a peer, relay off, out of streams). */
    bool RedirectConnect(::Service *fwd, s32 sockfd, u32 dst_ip, u16 dst_port,
                         s32 *out_ret, s32 *out_bsd_errno);

    /* Called from RelayTransport for a relay frame addressed to TcpTunnelPort.
       src_ip is the sending console (host order). */
    void OnTunnelFrame(u32 src_ip, const u8 *payload, size_t len);

    /* Start/stop the pump thread. Tied to the relay transport's lifetime. */
    void Start();
    void Stop();

}
