#include "tcp_connection.hh"

#include <algorithm>
#include <cstddef>
#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return ms_time; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _receiver.segment_received(seg);
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
}

bool TCPConnection::active() const {
    return !(_sender.bytes_in_flight() == 0 && _sender.stream_in().eof() && _receiver.stream_out().input_ended());
}

size_t TCPConnection::write(const string &data) {
    if (data.empty()) {
        return 0;
    }
    const auto ret = _sender.stream_in().write(data);
    _sender.fill_window();
    send();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    ms_time += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    send();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.send_rst_segment();
            _receiver.stream_out().set_error();
            send();
            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
