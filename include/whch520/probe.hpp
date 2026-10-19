#pragma once

#include <span>
#include <string_view>

namespace whch520 {

class StrictProbe {
public:
  virtual ~StrictProbe() = default;
  virtual void on_connected(std::string_view address) = 0;
  virtual void on_receive(std::span<const unsigned char> frame) = 0;
  virtual void on_send(std::span<const unsigned char> frame) = 0;
  virtual void on_disconnected() noexcept = 0;
};

// Temporary no-op adapter. Replace make_strict_probe() when the sibling probe
// API lands; controller call sites deliberately enforce strict frame direction.
[[nodiscard]] StrictProbe &strict_probe();

} // namespace whch520
