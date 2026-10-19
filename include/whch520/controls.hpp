#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace whch520 {

inline constexpr std::string_view supported_firmware = "1.0.4";
inline constexpr std::array<std::uint8_t, 15> supported_table1{
    0x10, 0x12, 0x13, 0x14, 0x20, 0x23, 0x27, 0x32,
    0x50, 0x90, 0xa1, 0xc1, 0xe1, 0xe2, 0xd1};
inline constexpr std::array<std::uint8_t, 3> supported_table2{0x41, 0x30, 0x31};

struct EvidenceProfile {
  std::string_view model;
  std::string_view firmware;
  std::span<const std::uint8_t> table1;
  std::span<const std::uint8_t> table2;
  bool equalizer_available;
  std::uint32_t equalizer_bands;
  bool dsee_available;
  std::uint32_t boolean_general_settings;
  std::string_view general_setting_subject;
};

[[nodiscard]] bool matches_supported_profile(const EvidenceProfile &profile) noexcept;
[[nodiscard]] bool parse_on_off(std::string_view value);
[[nodiscard]] std::uint32_t parse_equalizer_preset(std::string_view value);
[[nodiscard]] std::string_view equalizer_preset_name(std::uint32_t preset) noexcept;

} // namespace whch520
