#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
class XskTcpOutOfOrderQueue {
  public:
    struct OutOfOrderQueueElem {
        std::string buffer;
        bool eof;

      public:
        OutOfOrderQueueElem(OutOfOrderQueueElem &&elem, bool eof_ = false) noexcept
            : buffer(std::move(elem.buffer)), eof(eof_) {}
        explicit OutOfOrderQueueElem(std::string &&buf, bool eof_ = false) noexcept
            : buffer(std::move(buf)), eof(eof_) {}
    };
    std::map<uint32_t, OutOfOrderQueueElem> queue{};

    XskTcpOutOfOrderQueue() = default;
    void push(uint32_t seq, const std::string &packet, bool eof = false) {
        const auto idx = seq;
        // auto buffer = packet.clonePacketBuffer();
        auto buffer = packet;
        if (queue.empty()) {
            queue.emplace(idx, OutOfOrderQueueElem(std::move(buffer), eof));
        }
        auto lowerBound = queue.lower_bound(idx);
        while (lowerBound != queue.end()) {
            const auto bound = idx + buffer.size();
            const auto nextIdx = lowerBound->first;
            const auto &nextElem = lowerBound->second;
            if (bound < nextIdx) {
                break;
            }
            const auto nextBound = nextIdx + nextElem.buffer.size();
            if (bound < nextBound) {
                // Merge next buffer
                const auto size = buffer.size();
                const auto newSize = nextBound - idx;
                buffer.resize(newSize);
                memcpy(reinterpret_cast<uint8_t *>(&buffer[size]), &nextElem.buffer[bound - nextIdx], newSize - size);
            }
            lowerBound = queue.erase(lowerBound);
        }
        if (queue.empty() || queue.begin() == lowerBound) {
            queue.emplace(idx, OutOfOrderQueueElem(std::move(buffer), eof));
            return;
        }
        --lowerBound;
        {
            const auto prevIdx = lowerBound->first;
            auto &prevElem = lowerBound->second;
            const auto prevBound = prevIdx + prevElem.buffer.size();
            if (prevBound < idx) {
                queue.emplace(idx, OutOfOrderQueueElem(std::move(buffer), eof));
                return;
            }
            const auto bound = idx + buffer.size();
            auto newSize = bound - prevIdx;
            prevElem.buffer.resize(newSize);
            memcpy(&prevElem.buffer[idx - prevIdx], buffer.data(), buffer.size());
        }
    }
    [[nodiscard]] uint32_t topSeq() const { return queue.empty() ? -1 : queue.begin()->first; }
    OutOfOrderQueueElem pop() {
        auto beg = queue.begin();
        auto res = std::move(beg->second);
        queue.erase(beg);
        return res;
    }
};

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.
    XskTcpOutOfOrderQueue queue;
    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes
    size_t seq{0};

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
