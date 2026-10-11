#pragma once
#include <stdexcept>
#include <stop_token>

namespace yinamp::media::native {
class Cancelled final : public std::runtime_error {
  public:
    Cancelled() : std::runtime_error("Media job cancelled") {}
};
inline void CheckCancelled(std::stop_token stop) {
    if (stop.stop_requested())
        throw Cancelled{};
}
}
