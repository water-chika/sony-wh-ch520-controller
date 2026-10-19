# Sony WH-CH520 Controller

A source-only Linux command-line controller skeleton for the Sony WH-CH520. It uses BlueZ over the system D-Bus for paired-device discovery and battery/output status, and SonyHeadphonesClient's `libmdr`/`libmdr-bt` targets for the Sony RFCOMM protocol v2 lifecycle.

The controller deliberately rejects every model except the exact BlueZ `Name` or `Alias` `WH-CH520`, and opens only the protocol-v2 service UUID.

## Build

Initialize the submodule, then keep generated output outside this repository:

```sh
git submodule update --init
cmake -S . -B /mnt/build/sony-wh-ch520-controller -DCMAKE_BUILD_TYPE=Release
cmake --build /mnt/build/sony-wh-ch520-controller
```

Dependencies: CMake 3.31+, a C++20 compiler, BlueZ development headers/libraries, D-Bus development files, and Git/network access for SonyHeadphonesClient's pinned `fmt` dependency when it is not already cached.

The top-level project forcibly disables the upstream GUI client, tests, code generation, BLE, and shared libraries.

## Usage

```sh
/mnt/build/sony-wh-ch520-controller/whch520ctl list
/mnt/build/sony-wh-ch520-controller/whch520ctl status [AA:BB:CC:DD:EE:FF]
/mnt/build/sony-wh-ch520-controller/whch520ctl read [AA:BB:CC:DD:EE:FF]
/mnt/build/sony-wh-ch520-controller/whch520ctl dsee on|off [MAC]
/mnt/build/sony-wh-ch520-controller/whch520ctl eq-preset NAME|0..29 [MAC]
/mnt/build/sony-wh-ch520-controller/whch520ctl eq-clear-bass -10..10 [MAC]
/mnt/build/sony-wh-ch520-controller/whch520ctl eq-bands B1 B2 B3 B4 B5 [MAC]
```

`status` is BlueZ-only. `read` validates the model, runs the three ordered GET-only probe phases, and reports the exact firmware/support tables, EQ/DSEE, `MULTIPOINT_SETTING`, paired devices, connected state, and current playback source.

Writes are enabled only when all completed-probe evidence exactly matches the tested WH-CH520 firmware 1.0.4 profile and advertised tables. Each command reads current state, stages one property, requests one commit, waits for apply completion, reconnects and repeats the strict GET-only probe, then reports success only after exact current-state readback. Multipoint is read-only because the physical headset acknowledged the generic write but retained its previous value. No ANC, raw command, pairing, playback, or source-switch actions are exposed.

## Probe safety

The strict libmdr probe has a closed transmit allowlist. It excludes setting/action commands, notification enrollment (`0x94`, `0xC4`), noise-control queries, commit, pairing actions, shutdown, and playback actions. The protocol-reported model must be exactly `WH-CH520` before support-table or state probing proceeds.

## Ownership and license

The original top-level controller code is licensed under the MIT license in `LICENSE`, copyright 2026 water-chika.

`SonyHeadphonesClient` is an independent Git submodule from <https://github.com/mos9527/SonyHeadphonesClient>. It remains owned and licensed by its upstream contributors under `SonyHeadphonesClient/LICENSE`; this project does not claim ownership of that code. Sony and WH-CH520 are trademarks of their respective owner; this project is not affiliated with or endorsed by Sony.
