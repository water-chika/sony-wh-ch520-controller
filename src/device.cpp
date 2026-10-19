#include "whch520/device.hpp"

#include <dbus/dbus.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>

namespace whch520 {
namespace {

using MessagePtr = std::unique_ptr<DBusMessage, decltype(&dbus_message_unref)>;

std::string dbus_error_text(const char *operation, DBusError &error) {
  std::string text = operation;
  if (dbus_error_is_set(&error)) {
    text += ": ";
    text += error.message;
    dbus_error_free(&error);
  }
  return text;
}

void read_device_property(DBusMessageIter &entry, Device &device) {
  DBusMessageIter fields;
  dbus_message_iter_recurse(&entry, &fields);
  const char *key = nullptr;
  dbus_message_iter_get_basic(&fields, &key);
  if (!dbus_message_iter_next(&fields)) {
    return;
  }

  DBusMessageIter value;
  dbus_message_iter_recurse(&fields, &value);
  const int type = dbus_message_iter_get_arg_type(&value);
  if ((std::string_view(key) == "Name" || std::string_view(key) == "Alias" ||
       std::string_view(key) == "Address") &&
      type == DBUS_TYPE_STRING) {
    const char *text = nullptr;
    dbus_message_iter_get_basic(&value, &text);
    if (std::string_view(key) == "Name") {
      device.name = text;
    } else if (std::string_view(key) == "Alias") {
      device.alias = text;
    } else {
      device.address = text;
    }
  } else if (std::string_view(key) == "Connected" &&
             type == DBUS_TYPE_BOOLEAN) {
    dbus_bool_t connected = FALSE;
    dbus_message_iter_get_basic(&value, &connected);
    device.connected = connected;
  }
}

void read_battery_property(DBusMessageIter &entry, Device &device) {
  DBusMessageIter fields;
  dbus_message_iter_recurse(&entry, &fields);
  const char *key = nullptr;
  dbus_message_iter_get_basic(&fields, &key);
  if (std::string_view(key) != "Percentage" ||
      !dbus_message_iter_next(&fields)) {
    return;
  }
  DBusMessageIter value;
  dbus_message_iter_recurse(&fields, &value);
  if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BYTE) {
    unsigned char percentage = 0;
    dbus_message_iter_get_basic(&value, &percentage);
    device.battery_percent = percentage;
  }
}

void read_properties(DBusMessageIter &properties, Device &device,
                     bool battery) {
  DBusMessageIter entries;
  dbus_message_iter_recurse(&properties, &entries);
  while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY) {
    if (battery) {
      read_battery_property(entries, device);
    } else {
      read_device_property(entries, device);
    }
    dbus_message_iter_next(&entries);
  }
}

} // namespace

bool is_valid_mac(std::string_view address) noexcept {
  if (address.size() != 17) {
    return false;
  }
  for (std::size_t i = 0; i < address.size(); ++i) {
    if ((i + 1) % 3 == 0) {
      if (address[i] != ':') {
        return false;
      }
    } else if (!std::isxdigit(static_cast<unsigned char>(address[i]))) {
      return false;
    }
  }
  return true;
}

std::string normalize_mac(std::string_view address) {
  if (!is_valid_mac(address)) {
    throw std::invalid_argument("invalid Bluetooth MAC address: " +
                                std::string(address));
  }
  std::string result(address);
  std::ranges::transform(result, result.begin(),
                         [](unsigned char ch) { return std::toupper(ch); });
  return result;
}

bool is_supported_model(const Device &device) noexcept {
  return device.name == supported_model || device.alias == supported_model;
}

std::vector<Device> list_bluez_devices() {
  DBusError error;
  dbus_error_init(&error);
  DBusConnection *connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
  if (connection == nullptr) {
    throw std::runtime_error(
        dbus_error_text("cannot open system D-Bus", error));
  }

  MessagePtr request(dbus_message_new_method_call(
                         "org.bluez", "/", "org.freedesktop.DBus.ObjectManager",
                         "GetManagedObjects"),
                     dbus_message_unref);
  if (!request) {
    throw std::runtime_error("cannot allocate BlueZ D-Bus request");
  }
  MessagePtr reply(dbus_connection_send_with_reply_and_block(
                       connection, request.get(), 5000, &error),
                   dbus_message_unref);
  if (!reply) {
    throw std::runtime_error(
        dbus_error_text("BlueZ GetManagedObjects failed", error));
  }

  DBusMessageIter root;
  if (!dbus_message_iter_init(reply.get(), &root) ||
      dbus_message_iter_get_arg_type(&root) != DBUS_TYPE_ARRAY) {
    throw std::runtime_error("BlueZ returned malformed managed objects");
  }

  std::vector<Device> devices;
  DBusMessageIter objects;
  dbus_message_iter_recurse(&root, &objects);
  while (dbus_message_iter_get_arg_type(&objects) == DBUS_TYPE_DICT_ENTRY) {
    DBusMessageIter object;
    dbus_message_iter_recurse(&objects, &object);
    const char *path = nullptr;
    dbus_message_iter_get_basic(&object, &path);
    dbus_message_iter_next(&object);

    Device candidate;
    candidate.object_path = path;
    bool has_device_interface = false;
    DBusMessageIter interfaces;
    dbus_message_iter_recurse(&object, &interfaces);
    while (dbus_message_iter_get_arg_type(&interfaces) ==
           DBUS_TYPE_DICT_ENTRY) {
      DBusMessageIter interface_entry;
      dbus_message_iter_recurse(&interfaces, &interface_entry);
      const char *interface_name = nullptr;
      dbus_message_iter_get_basic(&interface_entry, &interface_name);
      dbus_message_iter_next(&interface_entry);
      const std::string_view interface(interface_name);
      if (interface == "org.bluez.Device1") {
        has_device_interface = true;
        read_properties(interface_entry, candidate, false);
      } else if (interface == "org.bluez.Battery1") {
        read_properties(interface_entry, candidate, true);
      }
      dbus_message_iter_next(&interfaces);
    }
    if (has_device_interface && is_valid_mac(candidate.address)) {
      candidate.address = normalize_mac(candidate.address);
      devices.push_back(std::move(candidate));
    }
    dbus_message_iter_next(&objects);
  }
  return devices;
}

Device require_supported_device(std::optional<std::string_view> address) {
  const auto requested =
      address ? std::optional<std::string>(normalize_mac(*address))
              : std::nullopt;
  auto devices = list_bluez_devices();
  auto found = std::ranges::find_if(devices, [&](const Device &device) {
    return (!requested || device.address == *requested) &&
           is_supported_model(device);
  });
  if (found != devices.end()) {
    return *found;
  }
  if (requested) {
    const auto wrong_model =
        std::ranges::find(devices, *requested, &Device::address);
    if (wrong_model != devices.end()) {
      throw std::runtime_error("refusing device " + *requested +
                               " (BlueZ model is '" + wrong_model->name +
                               "', expected exactly 'WH-CH520')");
    }
    throw std::runtime_error("Bluetooth device " + *requested +
                             " is not known to BlueZ");
  }
  throw std::runtime_error(
      "no paired BlueZ device with model exactly 'WH-CH520' was found");
}

} // namespace whch520
