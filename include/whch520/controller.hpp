#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>

struct MDRConnectionLinux;
struct MDRHeadphones;

namespace whch520 {

struct Device;
class Controller {
public:
  explicit Controller(Device device);
  ~Controller();
  Controller(const Controller &) = delete;
  Controller &operator=(const Controller &) = delete;

  void connect(std::chrono::milliseconds timeout = std::chrono::seconds(15));
  void initialize(std::chrono::milliseconds timeout = std::chrono::seconds(15));
  void print_protocol_status() const;
  void set_dsee(bool enabled);
  void set_equalizer_preset(std::uint32_t preset);
  void set_equalizer_clear_bass(std::int32_t clear_bass);
  void set_equalizer_bands(const std::array<std::int32_t, 5> &bands);
  void disconnect() noexcept;

private:
  Device &device() noexcept;
  const Device &device() const noexcept;
  void require_supported_profile() const;
  void
  commit_and_wait(std::chrono::milliseconds timeout = std::chrono::seconds(15));
  void reinitialize();

  std::unique_ptr<Device> device_;
  MDRConnectionLinux *linux_connection_{};
  MDRHeadphones *headphones_{};
};

} // namespace whch520
