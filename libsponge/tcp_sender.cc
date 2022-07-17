#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <algorithm>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const {
    size_t size = 0;
    for_each(backup.cbegin(), backup.cend(), [&size](const TCPSegment &segment) {
        size += segment.length_in_sequence_space();
    });
    return size;
}

void TCPSender::fill_window() {
    if (!(flags & SYN)) {
        TCPSegment frame;
        auto &header = frame.header();
        header.syn = true;
        header.seqno = wrap(_next_seqno++, _isn);
        _segments_out.push(frame);
        backup.push_back(std::move(frame));
        ms_last_time = 0;
        flags |= SYN;
        return;
    }
    if (!(flags & SYN_RECV)) {
        return;
    }
    if (_stream.buffer_empty()) {
        if (!(flags & FIN) && _stream.eof()) {
            TCPSegment frame;
            auto &header = frame.header();
            header.fin = true;
            header.seqno = wrap(_next_seqno++, _isn);
            _segments_out.push(frame);
            backup.push_back(std::move(frame));
            ms_last_time = 0;
            flags |= FIN;
        }
        return;
    }
    TCPSegment seg;
    seg.header().seqno = wrap(_next_seqno, _isn);
    const auto size = std::max(1UL, std::min(windows, _stream.buffer_size()));
    _next_seqno = size;
    auto payload = _stream.read(size);
    if (_stream.eof()) {
        seg.header().fin = true;
        flags |= FIN;
    }
    seg.payload() = Buffer(std::move(payload));
    _segments_out.push(seg);
    backup.push_back(std::move(seg));
    ms_last_time = 0;
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno_, const uint16_t window_size) {
    auto absoluteAck = unwrap(ackno_, _isn, ackno);
    if (absoluteAck <= ackno) {
        return;
    }
    windows = window_size;
    ackno = absoluteAck;
    restrans = 0;
    ms_last_time = 0;
    while (!backup.empty()) {
        const auto packet = backup.front();
        auto packetBound = unwrap(packet.header().seqno, _isn, ackno);
        if (packetBound > absoluteAck) {
            return;
        }
        backup.pop_front();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    ms_last_time += ms_since_last_tick;
    if (ms_last_time < _initial_retransmission_timeout || backup.empty()) {
        return;
    }

    ms_last_time = 0;
    _initial_retransmission_timeout <<= 1;
    restrans += 1;

    _segments_out.push(backup.front());
}

unsigned int TCPSender::consecutive_retransmissions() const { return restrans; }

void TCPSender::send_empty_segment() {}
void TCPSender::sendSYN() noexcept {
    TCPSegment frame;
    auto &header = frame.header();
    header.syn = true;
    header.seqno = wrap(_next_seqno++, _isn);
    _segments_out.push(frame);
    backup.push_back(std::move(frame));
    ms_last_time = 0;
}
