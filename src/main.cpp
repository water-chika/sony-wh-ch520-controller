#include "whch520/bluez.hpp"
#include "whch520/controller.hpp"
#include "whch520/controls.hpp"
#include "whch520/device.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

void usage(std::ostream &output) {
  output << "usage:\n"
         << "  whch520ctl list\n"
         << "  whch520ctl status|read [MAC]\n"
         << "  whch520ctl dsee on|off [MAC]\n"
         << "  whch520ctl eq-preset NAME|0..29 [MAC]\n"
         << "  whch520ctl eq-clear-bass -10..10 [MAC]\n"
         << "  whch520ctl eq-bands B1 B2 B3 B4 B5 [MAC]\n";
}

std::int32_t parse_level(std::string_view text) {
  std::int32_t value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value < -10 ||
      value > 10) {
    throw std::invalid_argument("EQ value must be an integer from -10 to 10");
  }
  return value;
}

std::optional<std::string_view> optional_mac(int argc, char **argv) {
  if (argc > 3) {
    throw std::invalid_argument("too many arguments");
  }
  return argc == 3 ? std::optional<std::string_view>(argv[2]) : std::nullopt;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      usage(std::cerr);
      return 2;
    }
    const std::string_view command(argv[1]);
    if (command == "connect") {
      throw std::invalid_argument(
          "'connect' was replaced by the read-only 'read' command");
    }
    if (command != "list" && command != "status" && command != "read" &&
        command != "dsee" && command != "eq-preset" &&
        command != "eq-clear-bass" && command != "eq-bands") {
      usage(std::cerr);
      return 2;
    }
    if (command == "list") {
      if (argc != 2) {
        throw std::invalid_argument("list does not accept a MAC address");
      }
      for (const auto &device : whch520::list_bluez_devices()) {
        std::cout << device.address << '\t'
                  << (device.name.empty() ? device.alias : device.name);
        if (whch520::is_supported_model(device)) {
          std::cout << "\t[supported]";
        }
        std::cout << '\n';
      }
      return 0;
    }

    if (command == "status") {
      auto device = whch520::require_supported_device(optional_mac(argc, argv));
      std::cout << whch520::format_bluez_status(
          device, whch520::read_bluez_status(device));
      return 0;
    }

    int mac_index = 0;
    if (command == "read") {
      if (argc > 3) {
        throw std::invalid_argument("read accepts at most one MAC address");
      }
      mac_index = 2;
    } else if (command == "eq-bands") {
      if (argc != 7 && argc != 8) {
        throw std::invalid_argument(
            "eq-bands requires exactly five levels and optional MAC");
      }
      mac_index = 7;
    } else {
      if (argc != 3 && argc != 4) {
        throw std::invalid_argument(
            "command requires one value and optional MAC");
      }
      mac_index = 3;
    }
    const std::optional<std::string_view> mac =
        argc > mac_index ? std::optional<std::string_view>(argv[mac_index])
                         : std::nullopt;
    auto device = whch520::require_supported_device(mac);
    std::cout << whch520::format_bluez_status(
        device, whch520::read_bluez_status(device));
    whch520::Controller controller(std::move(device));
    controller.connect();
    controller.initialize();
    if (command == "read") {
      controller.print_protocol_status();
    } else if (command == "dsee") {
      controller.set_dsee(whch520::parse_on_off(argv[2]));
    } else if (command == "eq-preset") {
      controller.set_equalizer_preset(whch520::parse_equalizer_preset(argv[2]));
    } else if (command == "eq-clear-bass") {
      controller.set_equalizer_clear_bass(parse_level(argv[2]));
    } else if (command == "eq-bands") {
      std::array<std::int32_t, 5> bands{};
      for (std::size_t i = 0; i < bands.size(); ++i) {
        bands[i] = parse_level(argv[2 + i]);
      }
      controller.set_equalizer_bands(bands);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "whch520ctl: " << error.what() << '\n';
    return 1;
  }
}
