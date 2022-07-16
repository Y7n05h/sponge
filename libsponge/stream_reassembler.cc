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
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    queue.push(index, data, eof);
    auto top = queue.topSeq();
    if (top == seq) {
        auto item = queue.pop();
        _output.write(item.buffer);
        seq += item.buffer.size();
        if (item.eof) {
            _output.input_ended();
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
