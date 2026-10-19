#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace whch520 {

inline constexpr std::string_view supported_model = "WH-CH520";

struct Device {
  std::string name;
  std::string alias;
  std::string address;
  std::string object_path;
  bool connected{};
  std::optional<unsigned int> battery_percent;
};

[[nodiscard]] bool is_valid_mac(std::string_view address) noexcept;
[[nodiscard]] std::string normalize_mac(std::string_view address);
[[nodiscard]] bool is_supported_model(const Device &device) noexcept;
[[nodiscard]] std::vector<Device> list_bluez_devices();
[[nodiscard]] Device
require_supported_device(std::optional<std::string_view> address);

} // namespace whch520
