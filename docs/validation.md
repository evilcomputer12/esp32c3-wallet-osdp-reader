# Initial port validation — 2026-11-01

## What was measured

The connected board was queried with esptool 5.6.0 using `--no-stub flash-id`.
It reported ESP32-C3 QFN32 revision 0.4, 160 MHz, 40 MHz crystal, embedded XMC
Flash, manufacturer 0x46/device 0x4016, and **4 MB** detected Flash. Native USB
Serial/JTAG was available. This was identification only; no erase, upload,
eFuse programming or ATECC provisioning was performed.

Build measurements will vary with settings and toolchain versions. The recorded
release build uses PlatformIO Core 6.2.0, espressif32 8.0.0, ESP-IDF 6.1.0 and
RISC-V GCC 15.2.0+20251204. The local SCBK is configured; merchant providers
are disabled in the local settings, but both wallet protocol implementations
and their cryptographic algorithms are compiled and linked.

| Item | Bytes |
|---|---:|
| Physical Flash detected | 4,194,304 |
| Factory application partition | 3,145,728 |
| PlatformIO reported program use | 232,080 |
| PlatformIO reported static RAM | 18,764 |
| Application `.bin` file | 232,688 |

Static RAM excludes dynamic task stacks, driver allocations, crypto allocations
and other runtime heap use. The 3 MiB application partition is intentionally
larger than needed and provides no OTA slot. The remainder includes NVS, PHY
data and a reserved, currently unused data partition.

## Checks completed

- The complete ESP32-C3 application and bootloader compile/link with PlatformIO.
- **18 C test executables pass** under AddressSanitizer and
  UndefinedBehaviorSanitizer.
- The wallet executable uses fixed encrypted fixtures created independently
  with Python cryptography/OpenSSL: both Apple derivations, a 16-byte GCM IV,
  complete Google negotiation, HMAC/CTR decryption, APDU continuation,
  tamper rejection, credential mapping and secret-buffer reset.
- Eleven upstream OSDP test executables run against the actual wolfCrypt
  adapter, covering AES vectors, session keys, CBC, MAC, protected payloads,
  wrapping, PD/ACU behavior, loopback, replay and adapter lifecycle.
- Four upstream AsymCred executables cover TLV, APDU, PKOC and credential
  mapping. Their scripted verifier tests protocol behavior; they do not test
  the physical secure element.
- PN532 tests cover partial reads, checksums, normal/extended frames, capacity
  errors, bounded MI chaining, successful chaining and wake/SAM-first startup
  command ordering. These are mock transport tests, not RF tests.
- ATECC slot permission and presentation/removal suppression policies pass.
- The previous project's independent phone emulator also passed **30 scenarios**
  against the new C `wallet_driver`. That Python development tool remains in
  the STM32 repository; this repository contains only C test programs and
  public disposable test fixtures.
- Project sources contain no `.cpp`, `.cc`, `.cxx` or Python firmware/test
  implementation. All four submodules remain at their recorded commits with
  no source edits.

Run the self-contained tests with the commands in [quickstart](quickstart.md).
Test fixtures use explicitly public disposable keys, never the merchant keys
from the user's sample applications.

## Not yet checked on the ESP32

The firmware has **not been flashed to the C3**. These remain hardware acceptance
checks; passing a host test or linking a hardware backend does not satisfy them:

1. Confirm the SuperMini header labels, solder joints, module supplies and 3.3 V
   logic levels, especially RS-485 RO.
2. Flash and confirm the AES/SHA startup self-test on the real C3 peripheral.
3. Confirm ATECC configuration reads and repeatable random/signature operations
   over the new I2C HAL. Do not provision or lock the chip as a diagnostic.
4. Confirm PN532 cold boot, MCU-only reboot, discovery and APDU exchange over
   the new HSU C driver.
5. Establish OSDP Secure Channel with OSDP.Net and the new local SCBK. A wrong
   key must fail. Confirm no credential events or feedback commands are accepted
   through an unsecured connection.
6. Present a known PKOC card, verify encrypted delivery, hold it still without
   duplicates, remove it fully and confirm one new event after re-presentation.
7. Test LED feedback, reader unplug/reconnect and controller disconnect during
   authentication. Old queued events must not survive a new session.
8. Supply valid merchant settings and matched wallet passes; test actual Apple
   and Google phones. ECP discovery and compressed bundles are unsupported.
9. Measure task stack headroom, peak heap and OSDP latency during wallet
   cryptography, then run a sustained soak test.

No physical wallet compatibility, latency, cold-boot reliability or certification
claim is made by this initial port.
