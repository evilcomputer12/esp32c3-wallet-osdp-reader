# ESP32-C3 Wallet / PKOC OSDP Reader

A new PlatformIO + ESP-IDF project for the **ESP32-C3 SuperMini**, written in C.
It combines PKOC, Apple VAS and Google Smart Tap with OSDP Secure Channel,
a PN532 NFC reader and an ATECC608 secure element.

**Status: flashed and running on the ESP32-C3; PKOC, Google's demo Smart Tap
pass and the PassKit Apple VAS demo have delivered authenticated credentials
through encrypted OSDP.Net on hardware. Wrong-key rejection and hardware
AES/SHA self-tests also pass. Wider compatibility remains to be validated.**
Host regressions pass. This is a port of the
[STM32 reader](https://github.com/evilcomputer12/stm32-pkoc-osdp-reader), not a
claim of Apple/Google/SIA certification. The STM32 project's bench results do
not establish that this new ESP32 port works on hardware.

The connected SuperMini was interrogated read-only: ESP32-C3 revision 0.4,
160 MHz, 40 MHz crystal, **4,194,304 bytes of Flash**. The application partition
is 3 MiB. Exact current build measurements are in [validation](docs/validation.md).

## Start here

1. Read [wiring](docs/wiring.md) before moving any wires from the STM32.
2. Follow [build and first boot](docs/quickstart.md).
3. Set your private SCBK and optional merchant credentials using
   [configuration](config/README.md).
4. Use [architecture and cryptography](docs/architecture.md) to understand
   what runs in wolfCrypt, the ESP32 and the ATECC608.
5. Follow the hardware checklist in [validation](docs/validation.md).

```sh
git clone --recurse-submodules https://github.com/evilcomputer12/esp32c3-wallet-osdp-reader.git
cd esp32c3-wallet-osdp-reader
pio run
cmake -S tests -B .build-host
cmake --build .build-host --parallel
ctest --test-dir .build-host --output-on-failure
```

All project firmware, drivers and test programs are `.c`/`.h`; there is no
Arduino runtime or project C++ code. PlatformIO, CMake and ESP-IDF provide the
build tooling and SDK, including the SDK's own runtime/assembly components.
This does not mean PlatformIO itself is written in C.

## Included

- PKOC 1.0/1.1 using AsymCred, a fresh challenge and ATECC external ECDSA
  verification. The UID is not treated as an authenticated credential.
- Apple VAS decryption with P-256 ECDH, X9.63/SHA-256 and AES-256-GCM.
- Google Smart Tap negotiation with P-256 signing/ECDH, HKDF/SHA-256,
  AES-128-CTR and HMAC-SHA-256.
- OSDP address 0, 9600 baud, one reader, 256-bit RAW credential events.
  Events and feedback commands require an established Secure Channel.
- A new bounded PN532 HSU transport in C, including wake/SAM startup,
  extended replies, chained responses and recovery.
- Separate FreeRTOS tasks for OSDP, NFC, ATECC entropy and USB diagnostics.
- Controller-driven onboard LED; an optional buzzer output, disabled by default.

Wallet support needs the correct issuer/merchant configuration and a compatible
pass. The public repository includes **no merchant keys**. ATECC merchant slots
must already be provisioned for the requested operation. This firmware never writes,
locks or provisions the secure element. Broader wallet compatibility and
long-run reliability remain under hardware validation. ECP polling and compressed
Smart Tap bundles are not implemented; the reader does not advertise compression.

## Dependencies and licensing

Dependencies are pinned Git submodules, recorded in
[dependencies.lock.json](dependencies.lock.json). Application crypto uses
**wolfCrypt**, the C cryptographic library within wolfSSL. No TLS client/server
is used. ESP-IDF may build its own cryptographic components as SDK dependencies;
the reader protocol code does not call mbedTLS.

Project-authored files are GPL-3.0-or-later; see [LICENSE](LICENSE).
Third-party components retain their licenses. See [credits](docs/credits.md)
for **Jonathan Horvath / bytedreamer**, **Z-bit Systems**, the wallet research
projects and other contributors.

No sponsorship or seller endorsement is implied. The earlier hardware was
purchased from Temu and AliExpress because those were viable shipping options
for the builder's country. This port requires a **3.3 V compatible RS-485 logic
interface**; do not assume the old 5 V module can connect directly to an ESP32.
