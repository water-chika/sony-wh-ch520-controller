#include "whch520/controller.hpp"

#include "whch520/controls.hpp"
#include "whch520/device.hpp"
#include "whch520/probe.hpp"

#include <mdr-bt/ConnectionLinux.h>
#include <mdr-c/Base.h>
#include <mdr-c/Connection.h>
#include <mdr-c/Headphones.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace whch520 {
namespace {

[[noreturn]] void throw_mdr(MDRConnection *connection, MDRResult result,
                            const char *operation) {
  std::string message(operation);
  message += ": ";
  message += mdrResultString(result);
  if (connection != nullptr) {
    const char *detail = mdrConnectionGetLastError(connection);
    if (detail != nullptr && *detail != '\0') {
      message += " (";
      message += detail;
      message += ')';
    }
  }
  throw std::runtime_error(message);
}

std::string get_text(MDRHeadphones *headphones, MDRText field,
                     std::uint32_t index = 0) {
  std::uint32_t size = 0;
  MDRResult result =
      mdrHeadphonesGetText(headphones, field, index, nullptr, &size);
  if (result != MDR_RESULT_ERROR_BUFFER_TOO_SMALL && result != MDR_RESULT_OK) {
    return {};
  }
  if (size == 0) {
    return {};
  }
  std::vector<char> buffer(size);
  result = mdrHeadphonesGetText(headphones, field, index, buffer.data(), &size);
  return result == MDR_RESULT_OK ? std::string(buffer.data()) : std::string{};
}

std::vector<std::uint8_t> get_function_ids(MDRHeadphones *headphones,
                                           std::uint32_t table) {
  std::uint32_t count = 0;
  MDRResult result =
      mdrHeadphonesGetProbeFunctionIds(headphones, table, nullptr, &count);
  if (result != MDR_RESULT_ERROR_BUFFER_TOO_SMALL && result != MDR_RESULT_OK) {
    return {};
  }
  std::vector<std::uint8_t> ids(count);
  if (count != 0 &&
      mdrHeadphonesGetProbeFunctionIds(headphones, table, ids.data(), &count) !=
          MDR_RESULT_OK) {
    return {};
  }
  ids.resize(count);
  return ids;
}

std::vector<std::int32_t> get_bands(MDRHeadphones *headphones,
                                    MDRPropertyType type) {
  std::uint32_t count = 0;
  MDRResult result =
      mdrHeadphonesGetEqualizerBands(headphones, type, nullptr, &count);
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "equalizer band read failed");
  }
  std::vector<std::int32_t> bands(count);
  result =
      mdrHeadphonesGetEqualizerBands(headphones, type, bands.data(), &count);
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "equalizer band read failed");
  }
  bands.resize(count);
  return bands;
}

} // namespace

Controller::Controller(Device device, StrictProbe &probe)
    : device_(std::make_unique<Device>(std::move(device))), probe_(probe) {
  if (!is_supported_model(*device_)) {
    throw std::invalid_argument(
        "controller model guard requires exactly WH-CH520");
  }
  linux_connection_ = mdrConnectionLinuxCreate();
  if (linux_connection_ == nullptr) {
    throw std::runtime_error("cannot create Linux Bluetooth connection");
  }
}

Controller::~Controller() {
  disconnect();
  if (linux_connection_ != nullptr) {
    mdrConnectionLinuxDestroy(linux_connection_);
  }
}

Device &Controller::device() noexcept { return *device_; }

const Device &Controller::device() const noexcept { return *device_; }

void Controller::connect(std::chrono::milliseconds timeout) {
  MDRConnection *connection = mdrConnectionLinuxGet(linux_connection_);
  MDRResult result = mdrConnectionConnect(connection, device().address.c_str(),
                                          MDR_SERVICE_UUID_V2);
  if (result != MDR_RESULT_INPROGRESS && result != MDR_RESULT_OK) {
    throw_mdr(connection, result, "RFCOMM v2 connect failed");
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (result != MDR_RESULT_OK &&
         std::chrono::steady_clock::now() < deadline) {
    result = mdrConnectionPoll(connection, 100);
    if (result != MDR_RESULT_OK && result != MDR_RESULT_INPROGRESS &&
        result != MDR_RESULT_ERROR_TIMEOUT) {
      disconnect();
      throw_mdr(connection, result, "RFCOMM v2 connect failed");
    }
  }
  if (result != MDR_RESULT_OK) {
    disconnect();
    throw_mdr(connection, MDR_RESULT_ERROR_TIMEOUT,
              "RFCOMM v2 connect timed out");
  }

  probe_.on_connected(device().address);
  probe_connected_ = true;
  result = mdrHeadphonesCreate(MDR_ABI_VERSION, connection, MDR_PROTOCOL_V2,
                               &headphones_);
  if (result != MDR_RESULT_OK) {
    disconnect();
    throw_mdr(connection, result, "protocol v2 session creation failed");
  }
  mdrHeadphonesSetPacketCallback(
      headphones_,
      [](void *user, MDRPacketDirection direction, const unsigned char *frame,
         int frame_size) {
        auto &probe = *static_cast<StrictProbe *>(user);
        const auto bytes =
            std::span(frame, static_cast<std::size_t>(frame_size));
        if (direction == MDR_PACKET_DIRECTION_RX) {
          probe.on_receive(bytes);
        } else {
          probe.on_send(bytes);
        }
      },
      &probe_);
}

void Controller::probe(std::chrono::milliseconds timeout) {
  if (headphones_ == nullptr) {
    throw std::logic_error("connect before probe");
  }

  constexpr std::array phases{
      MDR_PROBE_PHASE_IDENTITY,
      MDR_PROBE_PHASE_SUPPORT_TABLES,
      MDR_PROBE_PHASE_APPROVED_READS,
  };
  for (const MDRProbePhase phase : phases) {
    MDRResult result = mdrHeadphonesRequestProbe(headphones_, phase);
    if (result != MDR_RESULT_OK) {
      throw_mdr(mdrConnectionLinuxGet(linux_connection_), result,
                "protocol probe request failed");
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool complete = false;
    while (std::chrono::steady_clock::now() < deadline) {
      MDREvent event = MDR_EVENT_NONE;
      result = mdrHeadphonesPoll(headphones_, &event);
      if (result != MDR_RESULT_OK) {
        throw_mdr(mdrConnectionLinuxGet(linux_connection_), result,
                  "protocol probe poll failed");
      }
      if (event == MDR_EVENT_PROBE_PHASE_COMPLETE) {
        complete = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!complete) {
      throw_mdr(mdrConnectionLinuxGet(linux_connection_),
                MDR_RESULT_ERROR_TIMEOUT, "protocol probe timed out");
    }

    const auto response_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < response_deadline) {
      MDREvent event = MDR_EVENT_NONE;
      result = mdrHeadphonesPoll(headphones_, &event);
      if (result != MDR_RESULT_OK) {
        throw_mdr(mdrConnectionLinuxGet(linux_connection_), result,
                  "protocol probe response poll failed");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (phase == MDR_PROBE_PHASE_IDENTITY) {
      std::string model_name = get_text(headphones_, MDR_TEXT_MODEL_NAME);
      while (model_name.empty() &&
             std::chrono::steady_clock::now() < deadline) {
        MDREvent event = MDR_EVENT_NONE;
        result = mdrHeadphonesPoll(headphones_, &event);
        if (result != MDR_RESULT_OK) {
          throw_mdr(mdrConnectionLinuxGet(linux_connection_), result,
                    "protocol identity response poll failed");
        }
        model_name = get_text(headphones_, MDR_TEXT_MODEL_NAME);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (model_name != supported_model) {
        throw std::runtime_error("protocol identity guard rejected model '" +
                                 model_name + "' (expected exactly WH-CH520)");
      }
    }
  }
}

void print_function_ids(MDRHeadphones *headphones, std::uint32_t table) {
  std::uint32_t count = 0;
  MDRResult result =
      mdrHeadphonesGetProbeFunctionIds(headphones, table, nullptr, &count);
  if (result != MDR_RESULT_ERROR_BUFFER_TOO_SMALL && result != MDR_RESULT_OK) {
    return;
  }
  std::vector<std::uint8_t> ids(count);
  if (count != 0 &&
      mdrHeadphonesGetProbeFunctionIds(headphones, table, ids.data(), &count) !=
          MDR_RESULT_OK) {
    return;
  }
  std::cout << "MDR table " << table << " functions:";
  for (const std::uint8_t id : ids) {
    std::cout << " 0x" << std::hex << static_cast<unsigned int>(id);
  }
  std::cout << std::dec << '\n';
}

void Controller::print_protocol_status() const {
  if (headphones_ == nullptr) {
    throw std::logic_error("probe before reading protocol status");
  }
  MDRProbeState probe_state{};
  if (mdrHeadphonesGetProbeState(headphones_, &probe_state) != MDR_RESULT_OK ||
      probe_state.completed_phase != MDR_PROBE_PHASE_APPROVED_READS) {
    throw std::logic_error("complete all probe phases before reading status");
  }
  const std::string model_name = get_text(headphones_, MDR_TEXT_MODEL_NAME);
  if (model_name != supported_model) {
    throw std::runtime_error("protocol identity guard rejected model '" +
                             model_name + "' (expected exactly WH-CH520)");
  }

  std::cout << "Protocol model: " << model_name << '\n';
  std::cout << "MDR protocol version: 0x" << std::hex
            << probe_state.protocol_version << std::dec << '\n';
  print_function_ids(headphones_, 1);
  if (probe_state.has_table2 == MDR_TRUE) {
    print_function_ids(headphones_, 2);
  }
  const std::string firmware = get_text(headphones_, MDR_TEXT_FIRMWARE_VERSION);
  if (!firmware.empty()) {
    std::cout << "Firmware: " << firmware << '\n';
  }

  MDREqualizer equalizer{};
  if (mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT,
                                &equalizer) == MDR_RESULT_OK) {
    std::cout << "Equalizer: "
              << (equalizer.available == MDR_TRUE ? "available" : "unavailable")
              << ", preset " << equalizer.preset << ", clear bass "
              << equalizer.clear_bass << ", bands " << equalizer.band_count
              << '\n';
    std::cout << "DSEE: "
              << (equalizer.dsee_available == MDR_TRUE ? "available"
                                                       : "unavailable")
              << ", enabled "
              << (equalizer.dsee_enabled == MDR_TRUE ? "yes" : "no") << '\n';
  }

  const auto bands = get_bands(headphones_, MDR_PROPERTY_CURRENT);
  std::cout << "EQ bands:";
  for (const auto band : bands) {
    std::cout << ' ' << band;
  }
  std::cout << '\n';

  std::uint32_t preset_count = 0;
  MDRResult result =
      mdrHeadphonesGetEqualizerPresets(headphones_, nullptr, &preset_count);
  if (result == MDR_RESULT_OK) {
    std::vector<MDREqualizerPreset> presets(preset_count);
    result = mdrHeadphonesGetEqualizerPresets(headphones_, presets.data(),
                                              &preset_count);
    if (result == MDR_RESULT_OK) {
      std::cout << "EQ presets:";
      for (const auto preset : presets) {
        std::cout << ' ' << equalizer_preset_name(preset) << '(' << preset
                  << ')';
      }
      std::cout << '\n';
    }
  }

  std::uint32_t general_count = 0;
  result =
      mdrHeadphonesGetGeneralSettingInfo(headphones_, nullptr, &general_count);
  if (result == MDR_RESULT_OK) {
    std::vector<MDRGeneralSettingInfo> infos(general_count);
    result = mdrHeadphonesGetGeneralSettingInfo(headphones_, infos.data(),
                                                &general_count);
    if (result == MDR_RESULT_OK) {
      for (const auto &info : infos) {
        MDRGeneralSetting setting{};
        if (mdrHeadphonesGetGeneralSetting(headphones_, MDR_PROPERTY_CURRENT,
                                           info.index,
                                           &setting) == MDR_RESULT_OK) {
          std::cout << "General setting "
                    << get_text(headphones_, MDR_TEXT_GENERAL_SETTING_SUBJECT,
                                info.index)
                    << ": "
                    << (setting.boolean_value == MDR_TRUE ? "on" : "off")
                    << '\n';
        }
      }
    }
  }

  std::uint32_t paired_count = 0;
  result = mdrHeadphonesGetPairedDevices(headphones_, nullptr, &paired_count);
  if (result == MDR_RESULT_OK) {
    std::vector<MDRPairedDevice> paired(paired_count);
    result = mdrHeadphonesGetPairedDevices(headphones_, paired.data(),
                                           &paired_count);
    if (result == MDR_RESULT_OK) {
      std::cout << "Paired devices reported: " << paired_count << '\n';
      for (const auto &item : paired) {
        std::cout << "  " << item.macAddress << ' ' << item.name
                  << " connected="
                  << (item.connected == MDR_TRUE ? "yes" : "no") << " source="
                  << (item.playback_device == MDR_TRUE ? "yes" : "no") << '\n';
      }
    }
  }
}

void Controller::require_supported_profile() const {
  if (headphones_ == nullptr) {
    throw std::logic_error("connect and probe before profile validation");
  }
  MDREqualizer equalizer{};
  if (mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT,
                                &equalizer) != MDR_RESULT_OK) {
    throw std::runtime_error("profile gate could not read equalizer evidence");
  }
  std::uint32_t general_count = 0;
  if (mdrHeadphonesGetGeneralSettingInfo(headphones_, nullptr,
                                         &general_count) != MDR_RESULT_OK ||
      general_count != 1) {
    throw std::runtime_error(
        "profile gate expected exactly one general setting");
  }
  std::vector<MDRGeneralSettingInfo> infos(general_count);
  if (mdrHeadphonesGetGeneralSettingInfo(headphones_, infos.data(),
                                         &general_count) != MDR_RESULT_OK) {
    throw std::runtime_error(
        "profile gate could not read general setting evidence");
  }
  const auto table1 = get_function_ids(headphones_, 1);
  const auto table2 = get_function_ids(headphones_, 2);
  const std::string model = get_text(headphones_, MDR_TEXT_MODEL_NAME);
  const std::string firmware = get_text(headphones_, MDR_TEXT_FIRMWARE_VERSION);
  const std::string subject =
      get_text(headphones_, MDR_TEXT_GENERAL_SETTING_SUBJECT, infos[0].index);
  const EvidenceProfile evidence{
      .model = model,
      .firmware = firmware,
      .table1 = table1,
      .table2 = table2,
      .equalizer_available = equalizer.available == MDR_TRUE,
      .equalizer_bands = equalizer.band_count,
      .dsee_available = equalizer.dsee_available == MDR_TRUE,
      .boolean_general_settings =
          infos[0].type == MDR_GENERAL_SETTING_BOOLEAN ? 1u : 0u,
      .general_setting_subject = subject,
  };
  if (!matches_supported_profile(evidence)) {
    std::ostringstream detail;
    detail << "refusing write: protocol evidence does not exactly match "
              "WH-CH520 firmware 1.0.4 completed-probe profile"
           << " (model=" << evidence.model << ", firmware=" << evidence.firmware
           << ", table1=" << evidence.table1.size()
           << ", table2=" << evidence.table2.size()
           << ", eq=" << evidence.equalizer_available
           << ", bands=" << evidence.equalizer_bands
           << ", dsee=" << evidence.dsee_available
           << ", boolean-settings=" << evidence.boolean_general_settings
           << ", subject=" << evidence.general_setting_subject << ')';
    throw std::runtime_error(detail.str());
  }
}

void Controller::commit_and_wait(std::chrono::milliseconds timeout) {
  MDRResult result = mdrHeadphonesRequestCommit(headphones_);
  if (result != MDR_RESULT_OK) {
    throw_mdr(mdrConnectionLinuxGet(linux_connection_), result,
              "commit request failed");
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    MDREvent event = MDR_EVENT_NONE;
    result = mdrHeadphonesPoll(headphones_, &event);
    if (result != MDR_RESULT_OK) {
      throw_mdr(mdrConnectionLinuxGet(linux_connection_), result,
                "commit poll failed");
    }
    if (event == MDR_EVENT_APPLY_COMPLETE) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  throw_mdr(mdrConnectionLinuxGet(linux_connection_), MDR_RESULT_ERROR_TIMEOUT,
            "commit apply timed out");
}

void Controller::reprobe() {
  disconnect();
  connect();
  probe();
  require_supported_profile();
}

void Controller::set_dsee(bool enabled) {
  require_supported_profile();
  MDREqualizer before{};
  MDRResult result =
      mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT, &before);
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "DSEE current-state read failed");
  }
  result = mdrHeadphonesSetDSEE(headphones_, enabled ? MDR_TRUE : MDR_FALSE);
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "DSEE stage failed");
  }
  commit_and_wait();
  reprobe();
  MDREqualizer after{};
  result = mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT, &after);
  if (result != MDR_RESULT_OK ||
      after.dsee_enabled != (enabled ? MDR_TRUE : MDR_FALSE)) {
    throw std::runtime_error("DSEE write failed exact readback verification");
  }
  std::cout << "DSEE: " << (enabled ? "on" : "off") << " (verified)\n";
}

void Controller::set_equalizer_preset(std::uint32_t preset) {
  require_supported_profile();
  MDREqualizer before{};
  if (mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT, &before) !=
      MDR_RESULT_OK) {
    throw std::runtime_error("EQ current-state read failed");
  }
  MDRResult result = mdrHeadphonesSetEqualizerPreset(headphones_, preset);
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "EQ preset stage failed");
  }
  commit_and_wait();
  reprobe();
  MDREqualizer after{};
  result = mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT, &after);
  if (result != MDR_RESULT_OK || after.preset != preset) {
    throw std::runtime_error(
        "EQ preset write failed exact readback verification");
  }
  std::cout << "EQ preset: " << equalizer_preset_name(preset)
            << " (verified)\n";
}

void Controller::set_equalizer_clear_bass(std::int32_t clear_bass) {
  require_supported_profile();
  MDREqualizer before{};
  if (mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT, &before) !=
      MDR_RESULT_OK) {
    throw std::runtime_error("EQ current-state read failed");
  }
  MDRResult result =
      mdrHeadphonesSetEqualizerClearBass(headphones_, clear_bass);
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "clear bass stage failed");
  }
  commit_and_wait();
  reprobe();
  MDREqualizer after{};
  result = mdrHeadphonesGetEqualizer(headphones_, MDR_PROPERTY_CURRENT, &after);
  if (result != MDR_RESULT_OK || after.clear_bass != clear_bass) {
    throw std::runtime_error(
        "clear bass write failed exact readback verification");
  }
  std::cout << "Clear bass: " << clear_bass << " (verified)\n";
}

void Controller::set_equalizer_bands(const std::array<std::int32_t, 5> &bands) {
  require_supported_profile();
  (void)get_bands(headphones_, MDR_PROPERTY_CURRENT);
  MDRResult result =
      mdrHeadphonesSetEqualizerBands(headphones_, bands.data(), bands.size());
  if (result != MDR_RESULT_OK) {
    throw_mdr(nullptr, result, "EQ bands stage failed");
  }
  commit_and_wait();
  reprobe();
  const auto after = get_bands(headphones_, MDR_PROPERTY_CURRENT);
  if (!std::ranges::equal(after, bands)) {
    throw std::runtime_error(
        "EQ bands write failed exact readback verification");
  }
  std::cout << "EQ bands: verified\n";
}

void Controller::disconnect() noexcept {
  if (headphones_ != nullptr) {
    mdrHeadphonesSetPacketCallback(headphones_, nullptr, nullptr);
    mdrHeadphonesDestroy(headphones_);
    headphones_ = nullptr;
  }
  if (linux_connection_ != nullptr) {
    mdrConnectionDisconnect(mdrConnectionLinuxGet(linux_connection_));
  }
  if (probe_connected_) {
    probe_.on_disconnected();
    probe_connected_ = false;
  }
}

} // namespace whch520
