#pragma once

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

// A fixed-size scratch buffer. Arrays start on typical 64-byte cache boundaries.
// Only trivial numeric types: no individual frees or destructor bookkeeping.
class Arena {
public:
    static constexpr std::size_t alignment = 64;

    explicit Arena(std::size_t bytes)
        : data_(static_cast<std::byte*>(::operator new(bytes, std::align_val_t{alignment}))),
          capacity_(bytes) {}
    ~Arena() { ::operator delete(data_, std::align_val_t{alignment}); }
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    template <typename T>
    T* allocate(std::size_t count) {
        static_assert(std::is_arithmetic_v<T>, "Arena supports numeric arrays only");
        static_assert(alignof(T) <= alignment);
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_alloc{};
        const auto padding = (alignment - used_ % alignment) % alignment;
        const auto bytes = count * sizeof(T);
        if (padding > capacity_ - used_ || bytes > capacity_ - used_ - padding)
            throw std::bad_alloc{};
        auto* result = reinterpret_cast<T*>(data_ + used_ + padding);
        // Begin object lifetimes without initializing values; the algorithm fills them.
        for (std::size_t i = 0; i < count; ++i)
            ::new (static_cast<void*>(result + i)) T;
        used_ += padding + bytes;
        return result;
    }

    // Call only after all users of previous allocations have finished.
    void reset() noexcept { used_ = 0; }
    std::size_t used_bytes() const noexcept { return used_; }

private:
    std::byte* data_;
    std::size_t capacity_;
    std::size_t used_ = 0;
};
