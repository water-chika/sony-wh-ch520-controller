#pragma once

#include <string>

namespace whch520 {

struct Device;

struct BluezStatus {
  bool connected{};
  unsigned int battery_percent{};
  bool has_battery{};
};

[[nodiscard]] BluezStatus read_bluez_status(const Device &device);
[[nodiscard]] std::string format_bluez_status(const Device &device,
                                              const BluezStatus &status);

} // namespace whch520
