#pragma once

#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace proc::v2 {

enum class FieldRole : std::uint8_t {
    Input,
    State,
    Output,
};

enum class WriteLifetime : std::uint8_t {
    Persistent,
};

enum class ChangeKind : std::uint8_t {
    SetValue,
    ApplyDiff,
    Tombstone,
};

using FieldSlot = std::uint32_t;
using NodeSlot = std::uint32_t;
using OpId = std::uint32_t;
using AlgebraId = std::uint32_t;
using GuardPredicateId = std::uint32_t;
using MaskWord = std::uint64_t;

template <class Index>
class DenseIndexMask {
    static_assert(std::is_integral_v<Index>, "DenseIndexMask requires an integral index type");
    static_assert(std::is_unsigned_v<Index>, "DenseIndexMask requires an unsigned index type");

public:
    DenseIndexMask() = default;

    explicit DenseIndexMask(std::size_t bit_count)
        : bit_count_(bit_count), words_(word_count_for(bit_count), MaskWord{0}) {}

    std::size_t bit_count() const noexcept {
        return bit_count_;
    }

    bool in_bounds(Index index) const noexcept {
        return to_position(index) < bit_count_;
    }

    bool any() const noexcept {
        return !empty();
    }

    bool empty() const noexcept {
        for (MaskWord word : words_) {
            if (word != 0) return false;
        }
        return true;
    }

    void clear() noexcept {
        for (MaskWord& word : words_) {
            word = 0;
        }
    }

    std::size_t count() const noexcept {
        std::size_t total = 0;
        for (MaskWord word : words_) {
            total += static_cast<std::size_t>(std::popcount(word));
        }
        return total;
    }

    bool test(Index index) const noexcept {
        const auto pos = to_position(index);
        if (pos >= bit_count_) return false;
        return (words_[pos / bits_per_word] & bit_for(pos)) != 0;
    }

    void set(Index index) {
        const auto pos = to_position(index);
        assert(pos < bit_count_ && "DenseIndexMask::set requires an in-bounds slot");
        words_[pos / bits_per_word] |= bit_for(pos);
    }

    void reset(Index index) noexcept {
        const auto pos = to_position(index);
        if (pos >= bit_count_) return;
        words_[pos / bits_per_word] &= ~bit_for(pos);
    }

    void union_with(const DenseIndexMask& other) {
        assert_same_shape(other);
        for (std::size_t i = 0; i < words_.size(); ++i) {
            words_[i] |= other.words_[i];
        }
    }

    void intersect_with(const DenseIndexMask& other) {
        assert_same_shape(other);
        for (std::size_t i = 0; i < words_.size(); ++i) {
            words_[i] &= other.words_[i];
        }
    }

    template <class Fn>
    void for_each_set_bit(Fn&& fn) const {
        for (std::size_t word_index = 0; word_index < words_.size(); ++word_index) {
            MaskWord remaining = words_[word_index];
            while (remaining != 0) {
                const auto bit_index = static_cast<std::size_t>(std::countr_zero(remaining));
                const auto pos = (word_index * bits_per_word) + bit_index;
                if (pos < bit_count_) {
                    fn(static_cast<Index>(pos));
                }
                remaining &= (remaining - 1);
            }
        }
    }

private:
    static constexpr std::size_t bits_per_word = sizeof(MaskWord) * 8;

    static std::size_t word_count_for(std::size_t bit_count) noexcept {
        return (bit_count + bits_per_word - 1) / bits_per_word;
    }

    static std::size_t to_position(Index index) noexcept {
        return static_cast<std::size_t>(index);
    }

    static MaskWord bit_for(std::size_t pos) noexcept {
        return MaskWord{1} << (pos % bits_per_word);
    }

    void assert_same_shape(const DenseIndexMask& other) const {
        assert(bit_count_ == other.bit_count_ && "DenseIndexMask operations require masks compiled for the same slot domain");
    }

    std::size_t bit_count_ = 0;
    std::vector<MaskWord> words_;
};

using FieldMask = DenseIndexMask<FieldSlot>;
using NodeMask = DenseIndexMask<NodeSlot>;
using DirtyMask = FieldMask;
using OutputMask = FieldMask;

struct ExecutionPlan final {
    DirtyMask dirty_inputs;
    OutputMask requested_outputs;
    NodeMask active_nodes;
    std::vector<NodeSlot> topo;
};

} // namespace proc::v2
