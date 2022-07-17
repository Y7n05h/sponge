#include "byte_stream.hh"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

using namespace std;

ByteStream::ByteStream(const size_t cap) : buf(cap), capacity(cap) { buf.resize(cap); }

size_t ByteStream::write(const string &data) {
    const auto avaliable = remaining_capacity();
    if (avaliable == 0) {
        return 0;
    }
    const auto res = std::min(avaliable, data.size());
    writeBytes(reinterpret_cast<const uint8_t *>(data.data()), res);
    writerSeq += res;
    prod = writerSeq % capacity;
    return res;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    const auto readAvaliable = std::min(buffer_size(), len);
    string res;
    if (readAvaliable == 0) {
        return res;
    }
    res.resize(readAvaliable);
    readBytes(reinterpret_cast<uint8_t *>(res.data()), readAvaliable);
    return res;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    if (len <= buffer_size()) {
        readerSeq += len;
        conm = readerSeq % capacity;
    } else {
        throw std::runtime_error("Remove error");
    }
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    auto res = peek_output(len);
    readerSeq += res.size();
    conm = readerSeq % capacity;
    return res;
}

void ByteStream::end_input() { flag |= WRITEEND; }

bool ByteStream::input_ended() const { return flag & WRITEEND; }

size_t ByteStream::buffer_size() const { return writerSeq - readerSeq; }

bool ByteStream::buffer_empty() const { return writerSeq == readerSeq; }

bool ByteStream::eof() const { return input_ended() && writerSeq == readerSeq; }

size_t ByteStream::bytes_written() const { return writerSeq; }

size_t ByteStream::bytes_read() const { return readerSeq; }

size_t ByteStream::remaining_capacity() const { return capacity - buffer_size(); }
void ByteStream::writeBytes(const uint8_t *data, size_t size) noexcept {
    const auto part1 = capacity - prod;
    if (part1 >= size) {
        memcpy(&buf[prod], data, size);
    } else {
        const auto part2 = size - part1;
        memcpy(&buf[prod], data, part1);
        memcpy(buf.data(), data + part1, part2);
    }
}
void ByteStream::readBytes(uint8_t *data, size_t size) const noexcept {
    const auto part1 = capacity - conm;
    if (part1 >= size) {
        memcpy(data, &buf[conm], size);
    } else {
        const auto part2 = size - part1;
        memcpy(data, &buf[conm], part1);
        memcpy(data + part1, buf.data(), part2);
    }
}
