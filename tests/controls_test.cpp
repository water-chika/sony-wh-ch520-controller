#include "whch520/controls.hpp"

#include <array>
#include <stdexcept>

namespace {
void require(bool condition) {
  if (!condition) {
    throw std::runtime_error("control helper test failed");
  }
}
} // namespace

int main() {
  const whch520::EvidenceProfile exact{
      .model = "WH-CH520",
      .firmware = "1.0.4",
      .table1 = whch520::supported_table1,
      .table2 = whch520::supported_table2,
      .equalizer_available = true,
      .equalizer_bands = 5,
      .dsee_available = true,
      .boolean_general_settings = 1,
      .general_setting_subject = "MULTIPOINT_SETTING",
  };
  require(whch520::matches_supported_profile(exact));
  auto wrong = exact;
  wrong.firmware = "1.0.5";
  require(!whch520::matches_supported_profile(wrong));
  wrong = exact;
  constexpr std::array<unsigned char, 3> reordered{0x30, 0x41, 0x31};
  wrong.table2 = reordered;
  require(!whch520::matches_supported_profile(wrong));
  require(whch520::parse_on_off("on"));
  require(!whch520::parse_on_off("off"));
  require(whch520::parse_equalizer_preset("custom") == 24);
  require(whch520::parse_equalizer_preset("29") == 29);
  try {
    (void)whch520::parse_equalizer_preset("30");
    require(false);
  } catch (const std::invalid_argument &) {
  }
}
