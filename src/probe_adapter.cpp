#include "whch520/probe.hpp"

namespace whch520 {
namespace {

class NoopStrictProbe final : public StrictProbe {
public:
  void on_connected(std::string_view) override {}
  void on_receive(std::span<const unsigned char>) override {}
  void on_send(std::span<const unsigned char>) override {}
  void on_disconnected() noexcept override {}
};

} // namespace

StrictProbe &strict_probe() {
  static NoopStrictProbe probe;
  return probe;
}

} // namespace whch520
