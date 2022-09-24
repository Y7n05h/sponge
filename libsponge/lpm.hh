#ifndef LPM_HH
#define LPM_HH

#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <linux/byteorder/little_endian.h>
#include <memory>
#include <stdexcept>
#include <string.h>
#include <string>

#define BUG_ON(cond) static_assert(!(cond), "BUILD BUG ON: " #cond)

template <class V>
class LpmTrie;
class LpmTrieInfo {
    uint32_t prefixLen;
    uint8_t data[4];

    template <class V>
    friend class LpmTrie;

    static __always_inline int fls(unsigned int x) noexcept {
        int r;

        asm("bsrl %1,%0\n\t"
            "jnz 1f\n\t"
            "movl $-1,%0\n"
            "1:"
            : "=r"(r)
            : "rm"(x));
        return r + 1;
    }
    constexpr static uint32_t max_prerfixlen = 32;

  public:
    LpmTrieInfo() { BUG_ON(offsetof(LpmTrieInfo, data) % sizeof(uint32_t)); }
    size_t longestPrefixMatch(const LpmTrieInfo &s) const noexcept {
        uint32_t limit = std::min(prefixLen, s.prefixLen);
        uint32_t diff = __be32_to_cpu(*(const __be32 *)data ^ *(const __be32 *)s.data);
        uint32_t prefixlen = 32 - fls(diff);
        if (prefixlen >= limit)
            return limit;
        return prefixlen;
    }
    inline int extract_bit(size_t index) const noexcept { return !!(data[index / 8] & (1 << (7 - (index % 8)))); }
    LpmTrieInfo(uint32_t prefix, uint32_t prefixLen_ = max_prerfixlen) noexcept : prefixLen(prefixLen_) {
        *(__be32 *)data = __cpu_to_be32(prefix);
    }
#ifdef DEBUG
    using string = std::string;
    static std::pair<string, string> splitField(const string &inp, char sepa) {
        std::pair<string, string> ret;
        string::size_type cpos = inp.find(sepa);
        if (cpos == string::npos)
            ret.first = inp;
        else {
            ret.first = inp.substr(0, cpos);
            ret.second = inp.substr(cpos + 1);
        }
        return ret;
    }
    LpmTrieInfo(const string &s) {
        auto [ip, second] = splitField(s, '/');
        prefixLen = second.empty() ? 32 : std::stoi(second);
        if (inet_pton(AF_INET, ip.c_str(), data) < 0) {
            throw std::runtime_error("parse error");
        }
    }
#endif
};

using LpmTrieKey = LpmTrieInfo;

template <class V>
class LpmTrie {
    struct LpmNode {
        LpmNode *child[2] = {};
        std::shared_ptr<V> value;
        LpmTrieInfo info;
        uint8_t flags = 0;
        constexpr static uint8_t LPM_TREE_NODE_FLAG_IM = 1;
        LpmNode() noexcept = default;
        LpmNode(const LpmTrieInfo &_info, std::shared_ptr<V> &&_value) : value(std::move(_value)), info(_info) {}
    };
    LpmNode *root = nullptr;

  public:
    std::shared_ptr<V> find(const LpmTrieKey &key) const noexcept {
        LpmNode *node = root;
        LpmNode *found = nullptr;
        while (node) {
            auto matchLen = node->info.longestPrefixMatch(key);
            if (matchLen == LpmTrieInfo::max_prerfixlen) {
                found = node;
                break;
            }
            if (matchLen < node->info.prefixLen) {
                break;
            }
            if (!(node->flags & LpmNode::LPM_TREE_NODE_FLAG_IM))
                found = node;
            node = node->child[key.extract_bit(node->info.prefixLen)];
        }
        return found ? found->value : nullptr;
    }
    void insertOrUpdate(const LpmTrieKey &key, std::shared_ptr<V> value) {
        LpmNode **slot = &root;
        LpmNode *node;
        uint32_t matchLen;
        while ((node = *slot)) {
            LpmTrieInfo &info = node->info;
            matchLen = info.longestPrefixMatch(key);
            if (info.prefixLen != matchLen || info.prefixLen == key.prefixLen ||
                info.prefixLen == LpmTrieInfo::max_prerfixlen) {
                break;
            }
            slot = &node->child[key.extract_bit(info.prefixLen)];
        }
        if (!node) {
            *slot = new LpmNode(key, std::move(value));
            return;
        }
        if (node->info.prefixLen == matchLen) {
            node->value = std::move(value);
            return;
        }
        if (matchLen == key.prefixLen) {
            auto *new_node = new LpmNode(key, std::move(value));
            new_node->child[node->info.extract_bit(matchLen)] = node;
            *slot = new_node;
            return;
        }
        {
            LpmNode *im_node = new LpmNode(node->info, nullptr);
            im_node->info.prefixLen = matchLen;
            im_node->flags = LpmNode::LPM_TREE_NODE_FLAG_IM;
            if (key.extract_bit(matchLen)) {
                im_node->child[0] = node;
                im_node->child[1] = new LpmNode(key, std::move(value));
            } else {
                im_node->child[0] = new LpmNode(key, std::move(value));
                im_node->child[1] = node;
            }
            *slot = im_node;
        }
    }
};

#endif
