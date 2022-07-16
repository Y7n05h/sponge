#include "stream_reassembler.hh"

#include <algorithm>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : queue(), _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, size_t index, const bool eof) {
    auto packetBound = index + data.size();
    if (packetBound < seq) {
        return;
    }
    if (packetBound == seq) {
        if (eof) {
            _output.end_input();
        }
        return;
    }
    const auto maxSeq = _output.bytes_read() + _capacity;
    if (maxSeq <= index) {
        return;
    }
    auto beg = data.cbegin();
    auto end = data.cend();
    if (packetBound > maxSeq) {
        end -= packetBound - maxSeq;
    }
    size_t pos = 0;
    if (index < seq) {
        pos = seq - index;
        beg += pos;
        index = seq;
    }
    string message(beg, end);
    queue.push(index, message, eof);
    auto top = queue.topSeq();
    if (top == seq) {
        auto item = queue.pop();
        seq += _output.write(item.buffer);
        if (item.eof) {
            _output.end_input();
        }
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t size = 0;
    for (auto &i : queue.queue) {
        size += i.second.buffer.size();
    }
    return size;
}

bool StreamReassembler::empty() const { return queue.queue.empty(); }
