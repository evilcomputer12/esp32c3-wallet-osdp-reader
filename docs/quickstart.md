# Build and first boot

## 1. Install tools

Install Git, a host C compiler, CMake and PlatformIO Core. PlatformIO's VS Code
extension may manage Core for you. On macOS, the Command Line Tools supply
Clang (`xcode-select --install`). For a standalone Core, follow the
[official PlatformIO installation guide](https://docs.platformio.org/en/latest/core/installation/index.html).

The project pins `platformio/espressif32@8.0.0`, which selected ESP-IDF 6.1.0
and the RISC-V GCC 15.2 toolchain in the recorded build. PlatformIO installs the
SDK and cross compiler; an Arduino installation is not needed.

## 2. Get the repository and libraries

```sh
git clone --recurse-submodules https://github.com/evilcomputer12/esp32c3-wallet-osdp-reader.git
cd esp32c3-wallet-osdp-reader
git submodule status
```

For a clone made without `--recurse-submodules`:

```sh
git submodule update --init --recursive
```

The submodule commits, rather than whichever release happens to be newest,
define the dependency versions. Do not replace them without rebuilding and
running the regression tests.

## 3. Add local configuration

Follow [config/README.md](../config/README.md). A public clone contains no
usable SCBK or merchant private keys. The unconfigured image builds, reports
its startup state and stays inactive instead of accepting a default key.

The development checkout has a separately generated local SCBK in ignored
files. That key is not included in the Git repository. For a new checkout,
generate a fresh random 16-byte SCBK and put exactly the same key in your OSDP
controller. Wallet configuration can stay disabled while validating PKOC.

## 4. Build

```sh
pio run
```

`platformio.ini` uses the compatible `esp32-c3-devkitm-1` chip manifest with
an explicit 4 MB Flash setting. The actual GPIO assignments are the SuperMini
map in `include/board.h`, not a DevKitM wiring diagram. The default partition
table reserves 3 MiB for one factory application and has no OTA slot.

The firmware and bootloader are produced under `.pio/build/supermini/`.
Firmware built with private configuration contains those secrets; the build
directory and binary formats are excluded from Git.

### PlatformIO Python dependency workaround

On the development Mac, the first ESP-IDF 6.1 configuration failed with
`No module named idf_component_manager`. Installing the SDK's own core
requirements into **PlatformIO's IDF virtual environment** resolved it:

```sh
uv pip install \
  --python "$HOME/.platformio/penv/.espidf-6.1.0/bin/python" \
  -r "$HOME/.platformio/packages/framework-espidf/tools/requirements/requirements.core.txt"
pio run
```

Use the actual virtual-environment path shown in your PlatformIO error if it
differs. `uv` is a build-tool installation aid, not a firmware dependency. If
that environment already has pip, its Python can instead run `-m pip install
-r ...`. Do not install these requirements into an unrelated system Python.

After changing the component source list or the generated AES compatibility
overlay, use `pio run -t clean` before rebuilding if PlatformIO retains an old
CMake configuration.

## 5. Run the C host tests

```sh
cmake -S tests -B .build-host
cmake --build .build-host --parallel
ctest --test-dir .build-host --output-on-failure
```

The tests use AddressSanitizer and UndefinedBehaviorSanitizer on Clang/GCC.
They include real wolfCrypt crypto, independently generated encrypted wallet
fixtures, OSDP Secure Channel loopback/replay tests, PKOC tests, PN532 framing
and the ATECC slot/presentation policies. They do not require connected boards.

## 6. Wire and program

Finish soldering, check continuity and follow [wiring](wiring.md). In particular,
resolve the 5 V RS-485 RO interface before attaching it to GPIO7. Close any
serial monitor using the same USB port.

```sh
pio device list
pio run -t upload --upload-port /dev/cu.usbmodemYOUR_PORT
pio device monitor --port /dev/cu.usbmodemYOUR_PORT --baud 115200
```

Replace the example port with the actual device. Linux typically uses
`/dev/ttyACM*`; Windows uses `COM*`. The onboard USB Serial/JTAG interface is
used, so a WCH driver for the separate USB-to-RS-485 adapter is not the C3
programming driver. If automatic download mode fails, hold BOOT, tap RESET,
release BOOT and retry the upload. Port names can change after a reset.

These upload commands overwrite the C3 application. No firmware was flashed
as part of the initial repository preparation.

## 7. Check startup and the controller

Expected milestones (PN532 readiness and Secure Channel can appear in either
order because they run in separate tasks):

1. `Flash=4194304` and available heap.
2. `AES-128/AES-256 and SHA-256 self-test passed`.
3. An ATECC address (normally `0x60`) with status zero.
4. PN532 `ready firmware=32010607` or another valid PN532 firmware response.
5. `secure=1` after the controller establishes Secure Channel with your SCBK.

ATECC initialization retries every five seconds. PN532 initialization retries
every two seconds. An absent SCBK prevents reader operation. Press `s` in the
USB monitor for ATECC/NFC/SC state, attempts, queued credentials, failures and
poll counts. It also reports OSDP RX/TX bytes, TX errors, task ticks and RX/DE
pin levels; `h` prints help. There are no key-dumping or provisioning commands.

Run the separate [OSDP.Net controller](https://github.com/Z-bit-Systems-LLC/OSDP.Net)
on its USB-to-RS-485 adapter: 9600 baud, PD address 0, Secure Channel enabled,
and the exact SCBK from your local configuration. Keep its serial port distinct
from the ESP32 USB console. The host controller is a separate .NET application;
it is not part of this C firmware repository.

Present a known PKOC card, confirm an encrypted RAW event in the controller,
hold it still, remove it fully and re-present it. Check wrong-key rejection
and LED feedback next. See [validation](validation.md) for the remaining bench
checks before enabling wallet passes.

## 8. Diagnose a phone tap

A vibration confirms NFC activity, not an authenticated pass read. Enable the
matching wallet configuration, keep other cards away from the antenna and watch
the USB log alongside the controller. A successful read must finish with
`credential queued provider=2` for Google or `provider=1` for Apple, followed by
an encrypted controller event.

The `wallet` log names each exchange: `select-ose`, `select-smarttap`,
`negotiate`, `get-data`, `more-data` or `get-vas`. It reports byte counts,
status words, errors and elapsed milliseconds (including local cryptography).
It does not print APDU bodies, private keys or pass values.

- `sw=9000 error=0` means that step succeeded, not that the whole tap finished.
- `step=negotiate ... sw=9500 error=8` means the phone rejected terminal
  authentication. Check the collector ID, registered key version and matching
  private key together. The reader stops without requesting or delivering a
  credential.
- `transport failed pn_error=... pn_status=...` means the PN532 exchange did
  not produce a usable APDU response. This is distinct from a handset status
  word. Check antenna position and the PN532 transport before changing keys.

For Google's published demo, see the exact configuration and pass link in
[the Google-authored sample app preserved here](https://github.com/kmandalas/smart-tap-sample-app/tree/192d1760bd8f44e8142dda6611c2a1314b35595b).
That demo uses collector **20180608** and key version **1**. These values are
specific to that demo; use your issuer's settings for your own passes.
