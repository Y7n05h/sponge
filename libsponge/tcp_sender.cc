#include "tcp_sender.hh"

#include "tcp_config.hh"
#include "tcp_segment.hh"

#include <algorithm>
#include <cstdint>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()})), retxTimer(retx_timeout), _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - ackno; }

void TCPSender::fill_window() {
    if (!(flags & SYN)) {
        TCPSegment frame;
        auto &header = frame.header();
        header.syn = true;
        header.seqno = wrap(_next_seqno++, _isn);
        _segments_out.push(frame);
        windows--;
        backup.push_back(std::move(frame));
        flags |= SYN;
        return;
    }
    if (ackno == 0) {
        return;
    }
    if (_stream.buffer_empty()) {
        if (!(flags & FIN) && _stream.eof() && windows > 0) {
            TCPSegment frame;
            auto &header = frame.header();
            header.fin = true;
            header.seqno = wrap(_next_seqno++, _isn);
            --windows;
            _segments_out.push(frame);
            backup.push_back(std::move(frame));
            flags |= FIN;
        }
        return;
    }
    while (windows > 0) {
        TCPSegment seg;
        seg.header().seqno = wrap(_next_seqno, _isn);
        auto size = std::min(windows, _stream.buffer_size());
        size = std::min(size, TCPConfig::MAX_PAYLOAD_SIZE);
        windows -= size;
        _next_seqno += size;
        auto payload = _stream.read(size);
        if (_stream.eof() && windows > 0) {
            seg.header().fin = true;
            flags |= FIN;
            --windows;
            ++_next_seqno;
        }
        seg.payload() = Buffer(std::move(payload));
        _segments_out.push(seg);
        backup.push_back(std::move(seg));
        if (_stream.buffer_empty()) {
            if (_stream.eof() && !(flags & FIN)) {
                continue;
            }
            return;
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno_, const uint16_t window_size) {
    auto absoluteAck = unwrap(ackno_, _isn, ackno);
    if (absoluteAck < ackno || absoluteAck > _next_seqno) {
        return;
    }
    if (window_size) {
        auto maxSeq = absoluteAck + window_size;
        if (maxSeq > _next_seqno + windows) {
            windows = maxSeq - _next_seqno;
        }
        flags &= ~WINDOWS_DETECT;
    } else {
        windows = 1;
        flags |= WINDOWS_DETECT;
    }

    if (absoluteAck == ackno) {
        return;
    }
    ackno = absoluteAck;
    restrans = 0;
    retxTimer.reset();
    while (!backup.empty()) {
        const auto packet = backup.front();
        auto packetBound = unwrap(packet.header().seqno, _isn, ackno) + packet.length_in_sequence_space();
        if (packetBound > absoluteAck) {
            return;
        }
        backup.pop_front();
    }
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    retxTimer.ticket(ms_since_last_tick);
    if (!retxTimer.timeout()) {
        return;
    }
    if (!backup.empty()) {
        restrans += 1;
        _segments_out.push(backup.front());
        if (!(flags & WINDOWS_DETECT)) {
            retxTimer.doubleTimeout();
        }
        retxTimer.restart();
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return restrans; }

void TCPSender::send_empty_segment() {}
void TCPSender::send_rst_segment() {
    TCPSegment seg;
    seg.header().rst = true;
    _segments_out.push(seg);
    _stream.set_error();
}
