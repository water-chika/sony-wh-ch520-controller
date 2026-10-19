#include "whch520/controls.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace whch520 {
namespace {

struct PresetName {
  std::string_view name;
  std::uint32_t value;
};

constexpr std::array presets{
    PresetName{"off", 0},       PresetName{"rock", 1},
    PresetName{"pop", 2},       PresetName{"jazz", 3},
    PresetName{"dance", 4},     PresetName{"edm", 5},
    PresetName{"r-and-b-hip-hop", 6}, PresetName{"acoustic", 7},
    PresetName{"bright", 8},    PresetName{"excited", 9},
    PresetName{"mellow", 10},   PresetName{"relaxed", 11},
    PresetName{"vocal", 12},    PresetName{"treble", 13},
    PresetName{"bass", 14},     PresetName{"speech", 15},
    PresetName{"heavy", 16},    PresetName{"clear", 17},
    PresetName{"hard", 18},     PresetName{"soft", 19},
    PresetName{"gaming", 20},   PresetName{"fps-1", 21},
    PresetName{"fps-2", 22},    PresetName{"fps-3", 23},
    PresetName{"custom", 24},   PresetName{"user-1", 25},
    PresetName{"user-2", 26},   PresetName{"user-3", 27},
    PresetName{"user-4", 28},   PresetName{"user-5", 29},
};

} // namespace

bool matches_supported_profile(const EvidenceProfile &profile) noexcept {
  return profile.model == "WH-CH520" &&
         profile.firmware == supported_firmware &&
         std::ranges::equal(profile.table1, supported_table1) &&
         std::ranges::equal(profile.table2, supported_table2) &&
         profile.equalizer_available && profile.equalizer_bands == 5 &&
         profile.dsee_available && profile.boolean_general_settings == 1 &&
         profile.general_setting_subject == "MULTIPOINT_SETTING";
}

bool parse_on_off(std::string_view value) {
  if (value == "on") {
    return true;
  }
  if (value == "off") {
    return false;
  }
  throw std::invalid_argument("expected 'on' or 'off'");
}

std::uint32_t parse_equalizer_preset(std::string_view value) {
  const auto found = std::ranges::find(presets, value, &PresetName::name);
  if (found != presets.end()) {
    return found->value;
  }
  std::uint32_t numeric{};
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), numeric);
  if (error == std::errc{} && end == value.data() + value.size() && numeric <= 29) {
    return numeric;
  }
  throw std::invalid_argument("unknown equalizer preset");
}

std::string_view equalizer_preset_name(std::uint32_t preset) noexcept {
  const auto found = std::ranges::find(presets, preset, &PresetName::value);
  return found == presets.end() ? "unknown" : found->name;
}

} // namespace whch520
