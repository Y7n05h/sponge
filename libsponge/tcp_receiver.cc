#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (flags & FIN) {
        return;
    }
    auto newOffset = offset;
    auto seqno = seg.header().seqno;
    if (seg.header().syn) {
        isn = WrappingInt32{seqno};
        if (flags & SYN) {
            return;
        }
        flags = SYN;
        newOffset = 1;
    }
    const auto str = seg.payload().copy();
    checkPoint = unwrap(seqno, isn, checkPoint);
    reassembler.push_substring(str, checkPoint - offset, seg.header().fin);
    if (reassembler.stream_out().input_ended()) {
        flags |= FIN;
        newOffset = 2;
    }
    offset = newOffset;
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (flags & SYN) {
        return wrap(reassembler.stream_out().bytes_written() + offset, isn);
    }
    return {};
}

size_t TCPReceiver::window_size() const {
    return reassembler.stream_out().bytes_read() + capacity - reassembler.stream_out().bytes_written();
}
