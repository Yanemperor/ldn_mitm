#pragma once
#include "debug.hpp"
#include <functional>

enum class LANPacketType : u8 {
    Scan,
    ScanResp,
    Connect,
    SyncNetwork,
    /* Relay mode only: periodic station liveness signal (no TCP close exists
       there). Older builds ignore unknown types. */
    RelayHeartbeat,
    /* Relay mode only: "I am leaving this session, on purpose" - the relay has
       no TCP close to convey an orderly exit, so without it the other side
       waits out the 30s staleness timeout and then reports a link failure for
       what was a clean goodbye. Same payload as RelayHeartbeat. */
    RelayBye,
};

typedef std::function<int(LANPacketType, const void *, size_t)> ReplyFunc;
typedef std::function<int(LANPacketType, const void *, size_t, ReplyFunc)> MessageCallback;

/* recv() on a TCP socket returned 0: the peer closed its end cleanly. Reported
   as a distinct negative so it reaches Poll's "close this socket" path (0 alone
   would look like "nothing to do" and leave the dead fd readable forever), and
   so the caller can tell an orderly shutdown from a link failure. */
constexpr int LanSocketPeerClosed = -0xFD23;

class Pollable {
    public:
        virtual int getFd() = 0;
        virtual int onRead() = 0;
        virtual void onClose() = 0;
        static int Poll(Pollable *fds[], size_t nfds, int timeout = 100);
};

struct LANPacketHeader {
    u32 magic;
    LANPacketType type;
    u8 compressed;
    u16 length;
    u16 decompress_length;
    /* Per-socket send counter (was _reserved; stock builds send 0 and never
       read it). Lets a receiver on an unordered path (the relay) drop a
       stale/duplicate frame; 0 = sender doesn't stamp. */
    u16 seq;
};
class LanSocket {
    protected:
        static const int BufferSize = 2048;
        static const u32 LANMagic = 0x11451400;
        int fd;
        u8 buffer[BufferSize];
        u16 recvSize;
        u16 txSeq = 0;
        u16 rxSeq = 0;
        void resetRecvSize();
        void prepareHeader(LANPacketHeader &header, LANPacketType type);
        int compress(const void *input, size_t input_size, uint8_t *output, size_t *output_size);
        int decompress(const void *input, size_t input_size, uint8_t *output, size_t *output_size);
        int recvPartPacket(u8 *buffer, size_t bufLen, struct sockaddr_in *addr);
        virtual ssize_t recvfrom(void *buf, size_t len, struct sockaddr_in *addr) = 0;
        virtual int sendto(const void *buf, size_t len, struct sockaddr_in *addr) = 0;
    public:
        LanSocket(int fd) : fd(fd), recvSize(0) {};
        ~LanSocket();
        int sendPacket(LANPacketType type, const void *data, size_t size);
        int sendPacket(LANPacketType type, const void *data, size_t size, struct sockaddr_in *addr);
        int recvPacket(MessageCallback callback);
        /* seq of the packet recvPacket last delivered; valid inside/after its
           callback. */
        u16 lastRecvSeq() const { return this->rxSeq; };
        void close();
        bool isClosed() { return this->fd == -1; };
        int getFd() { return this->fd; };
};
class TcpLanSocketBase : public LanSocket {
    protected:
        virtual ssize_t recvfrom(void *buf, size_t len, struct sockaddr_in *addr);
        virtual int sendto(const void *buf, size_t len, struct sockaddr_in *addr);
    public:
        TcpLanSocketBase(int fd) : LanSocket(fd) {};
};
class UdpLanSocketBase : public LanSocket {
    protected:
        virtual u32 getBroadcast() = 0;
        virtual ssize_t recvfrom(void *buf, size_t len, struct sockaddr_in *addr);
        virtual int sendto(const void *buf, size_t len, struct sockaddr_in *addr);
        u16 listenPort;
    public:
        int sendBroadcast(LANPacketType type, const void *data, size_t size);
        int sendBroadcast(LANPacketType type);
        UdpLanSocketBase(int fd, u16 listenPort) : LanSocket(fd), listenPort(listenPort) {};
};
