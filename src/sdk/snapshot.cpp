#include "sdk/snapshot.hpp"

namespace shogod::sdk {

void publisher::publish() noexcept {
    std::lock_guard lock(mutex_);
    frame_snapshot& frame = buffers_[write_index_];
    frame.seq = published_ ? published_->seq + 2u : 2u;
    published_ = &frame;
    write_index_ = 1 - write_index_;
}

std::optional<frame_snapshot> publisher::acquire() const noexcept {
    std::lock_guard lock(mutex_);
    if (published_ == nullptr || published_->count < 0 ||
        published_->count > k_max_players || !published_->view_matrix.finite()) {
        return std::nullopt;
    }
    return *published_;
}

}  // namespace shogod::sdk
