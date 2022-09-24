#ifndef SPONGE_LIBSPONGE_TCP_FACTORED_HH
#define SPONGE_LIBSPONGE_TCP_FACTORED_HH

#include "tcp_config.hh"
#include "tcp_receiver.hh"
#include "tcp_sender.hh"
#include "tcp_state.hh"

#ifdef DEBUG
#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <stdexcept>
#include <sys/time.h>
#include <unistd.h>

struct DebugFile {
    int fd = -1;
    static inline void TCPSgment2TCPHeader(const TCPSegment &seg, std::string &s) {
        const auto payload = seg.payload().str();
        s.resize(sizeof(tcphdr) + payload.size());
        auto *tcp = (tcphdr *)s.c_str();
        const auto &header = seg.header();
        tcp->ack = header.ack;
        tcp->fin = header.fin;
        tcp->syn = header.syn;
        tcp->rst = header.rst;
        tcp->doff = sizeof(tcphdr) / 4;
        tcp->ack_seq = htonl(header.ackno.raw_value());
        tcp->seq = htonl(header.seqno.raw_value());
        tcp->dest = htons(header.dport);
        tcp->source = htons(header.sport);
        tcp->window = htons(header.win);
        if (!payload.empty()) {
            memcpy((char *)(s.c_str() + sizeof(tcphdr)), (const char *)payload.data(), payload.size());
        }
    }

  public:
    DebugFile() {
        timeval tv;
        gettimeofday(&tv, nullptr);
        char buf[256];
        snprintf(buf, sizeof(buf), "/tmp/tcp/%ld.%ld", tv.tv_sec, tv.tv_usec);
        fd = open(buf, O_CREAT | O_WRONLY, 0777);
        if (fd < 0) {
            throw std::runtime_error("create failed");
        }
    }
    ~DebugFile() {
        if (fd >= 0) {
            close(fd);
        }
    }
    DebugFile(DebugFile &&) = default;
    DebugFile(const DebugFile &) = delete;
    DebugFile &operator=(DebugFile &&) = default;
    DebugFile &operator=(const DebugFile &) = delete;
    static void addIp(bool direct) {
        iphdr header;
        header.version = 4;
        header.ihl = sizeof(header) / 4;
        header.tos = 0;
        // header.tot_len=
    }
    static inline size_t __hexdump(const void *s, size_t size, char *output, const bool showChar) {
        char *outputOffset = output;
        // outputOffset += sprintf(outputOffset, "\n", size);

        uint32_t offset = 0;
        while (offset + 16 <= size) {
            const char *data = (const char *)s + offset;
            outputOffset += sprintf(outputOffset,
                                    "%08x %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx "
                                    "%02hhx %02hhx %02hhx %02hhx "
                                    "%02hhx %02hhx %02hhx ",
                                    offset,
                                    data[0],
                                    data[1],
                                    data[2],
                                    data[3],
                                    data[4],
                                    data[5],
                                    data[6],
                                    data[7],
                                    data[8],
                                    data[9],
                                    data[10],
                                    data[11],
                                    data[12],
                                    data[13],
                                    data[14],
                                    data[15]);
            if (showChar) {
                outputOffset += sprintf(outputOffset,
                                        "\t\t%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c ",
                                        data[0],
                                        data[1],
                                        data[2],
                                        data[3],
                                        data[4],
                                        data[5],
                                        data[6],
                                        data[7],
                                        data[8],
                                        data[9],
                                        data[10],
                                        data[11],
                                        data[12],
                                        data[13],
                                        data[14],
                                        data[15]);
            }

            *(outputOffset++) = '\n';
            offset += 16;
        }
        if (offset < size) {
            const auto *data = (const char *)s + offset;
            outputOffset += sprintf(outputOffset, "%08x ", offset);
            int i;
            for (i = 0; i < size - offset; i++) {
                outputOffset += sprintf(outputOffset, "%02hhx ", data[i]);
            }
            if (showChar) {
                for (; i < 16; i++) {
                    *(outputOffset++) = ' ';
                    *(outputOffset++) = ' ';
                    *(outputOffset++) = ' ';
                }
                *(outputOffset++) = '\t';
                *(outputOffset++) = '\t';
                memcpy(outputOffset, data, size - offset);
                outputOffset += size - offset;
            }
            *(outputOffset++) = '\n';
        }
        return outputOffset - output;
    }
    void hexdump(const void *s, size_t size, bool showChar = false) {
        char *buf = (char *)malloc(size / 16 * 200);
        const auto outputSize = __hexdump(s, size, buf, showChar);
        write(fd, buf, outputSize);
        fsync(fd);
        free(buf);
    }
    void hexdump(const std::string &s, bool showChar = false) { hexdump(s.c_str(), s.size(), showChar); }
    void hexdump(const TCPSegment &s, bool direct = false, bool showChar = false) {
        write(fd, direct ? "I\n" : "O\n", 2);
        std::string output;
        TCPSgment2TCPHeader(s, output);

        hexdump(output, showChar);
    }
};
#endif
//! \brief A complete endpoint of a TCP connection
class TCPConnection {
  private:
    TCPConfig _cfg;
    TCPReceiver _receiver{_cfg.recv_capacity};
    TCPSender _sender{_cfg.send_capacity, _cfg.rt_timeout, _cfg.fixed_isn};

#ifdef DEBUG
    DebugFile fd;
#endif

    //! outbound queue of segments that the TCPConnection wants sent
    std::queue<TCPSegment> _segments_out{};

    //! Should the TCPConnection stay active (and keep ACKing)
    //! for 10 * _cfg.rt_timeout milliseconds after both streams have ended,
    //! in case the remote TCPConnection doesn't know we've received its whole stream?
    bool _linger_after_streams_finish{true};

    size_t _time_since_last_segment_received_ms{0};
    bool _is_active{true};

    void _set_rst_state(bool send_rst);
    void _trans_segments_to_out_with_ack_and_win();

  public:
    //! \name "Input" interface for the writer
    //!@{

    //! \brief Initiate a connection by sending a SYN segment
    void connect();

    //! \brief Write data to the outbound byte stream, and send it over TCP if possible
    //! \returns the number of bytes from `data` that were actually written.
    size_t write(const std::string &data);

    //! \returns the number of `bytes` that can be written right now.
    size_t remaining_outbound_capacity() const;

    //! \brief Shut down the outbound byte stream (still allows reading incoming data)
    void end_input_stream();
    //!@}

    //! \name "Output" interface for the reader
    //!@{

    //! \brief The inbound byte stream received from the peer
    ByteStream &inbound_stream() { return _receiver.stream_out(); }
    //!@}

    //! \name Accessors used for testing

    //!@{
    //! \brief number of bytes sent and not yet acknowledged, counting SYN/FIN each as one byte
    size_t bytes_in_flight() const;
    //! \brief number of bytes not yet reassembled
    size_t unassembled_bytes() const;
    //! \brief Number of milliseconds since the last segment was received
    size_t time_since_last_segment_received() const;
    //!< \brief summarize the state of the sender, receiver, and the connection
    TCPState state() const { return {_sender, _receiver, active(), _linger_after_streams_finish}; };
    //!@}

    //! \name Methods for the owner or operating system to call
    //!@{

    //! Called when a new segment has been received from the network
    void segment_received(const TCPSegment &seg);

    //! Called periodically when time elapses
    void tick(const size_t ms_since_last_tick);

    //! \brief TCPSegments that the TCPConnection has enqueued for transmission.
    //! \note The owner or operating system will dequeue these and
    //! put each one into the payload of a lower-layer datagram (usually Internet datagrams (IP),
    //! but could also be user datagrams (UDP) or any other kind).
    std::queue<TCPSegment> &segments_out() { return _segments_out; }

    //! \brief Is the connection still alive in any way?
    //! \returns `true` if either stream is still running or if the TCPConnection is lingering
    //! after both streams have finished (e.g. to ACK retransmissions from the peer)
    bool active() const;
    //!@}

    //! Construct a new connection from a configuration
    explicit TCPConnection(const TCPConfig &cfg) : _cfg{cfg} {}

    //! \name construction and destruction
    //! moving is allowed; copying is disallowed; default construction not possible

    //!@{
    ~TCPConnection();  //!< destructor sends a RST if the connection is still open
    TCPConnection() = delete;
    TCPConnection(TCPConnection &&other) = default;
    TCPConnection &operator=(TCPConnection &&other) = default;
    TCPConnection(const TCPConnection &other) = delete;
    TCPConnection &operator=(const TCPConnection &other) = delete;
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_FACTORED_HH
