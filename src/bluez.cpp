#include "whch520/bluez.hpp"

#include "whch520/device.hpp"

#include <sstream>

namespace whch520 {

BluezStatus read_bluez_status(const Device &device) {
  for (const auto &current : list_bluez_devices()) {
    if (current.address == device.address) {
      return {.connected = current.connected,
              .battery_percent = current.battery_percent.value_or(0),
              .has_battery = current.battery_percent.has_value()};
    }
  }
  return {};
}

std::string format_bluez_status(const Device &device,
                                const BluezStatus &status) {
  std::ostringstream output;
  output << device.name << " " << device.address << '\n';
  output << "BlueZ connected: " << (status.connected ? "yes" : "no") << '\n';
  output << "BlueZ battery: ";
  if (status.has_battery) {
    output << status.battery_percent << "%\n";
  } else {
    output << "unavailable\n";
  }
  return output.str();
}

} // namespace whch520
