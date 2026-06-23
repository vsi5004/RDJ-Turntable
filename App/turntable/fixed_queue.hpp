#pragma once

#include <cstddef>

namespace turntable {

template <typename T, std::size_t Capacity>
class FixedQueue {
public:
    static_assert(Capacity > 0, "FixedQueue capacity must be non-zero");

    bool push(const T& value)
    {
        if (size_ == Capacity) return false;
        storage_[tail_] = value;
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        return true;
    }

    bool pop(T& value)
    {
        if (size_ == 0) return false;
        value = storage_[head_];
        head_ = (head_ + 1) % Capacity;
        --size_;
        return true;
    }

    void clear() { head_ = tail_ = size_ = 0; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    T storage_[Capacity]{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t size_ = 0;
};

}  // namespace turntable
