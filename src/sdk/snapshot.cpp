#include "sdk/snapshot.hpp"

namespace shogod::sdk {

publisher::publisher() { published_.store(&buffers_[0], std::memory_order_release); }

void publisher::publish() noexcept {
    const int index = write_index_.load(std::memory_order_relaxed);
    frame_snapshot& out = buffers_[index];
    out.seq += 2;  // even = stable, odd never observed by the reader

    published_.store(&out, std::memory_order_release);
    write_index_.store(1 - index, std::memory_order_relaxed);
}

const frame_snapshot* publisher::acquire() const noexcept {
    const frame_snapshot* frame = published_.load(std::memory_order_acquire);
    if (frame == nullptr) return nullptr;
    if ((frame->seq & 1u) != 0u) return nullptr;
    if (frame->count < 0 || frame->count > k_max_players) return nullptr;
    if (!frame->view_matrix.finite()) return nullptr;
    return frame;
}

}  // namespace shogod::sdk
