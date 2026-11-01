# Initial port validation — 2026-11-01

## What was measured

The connected board was queried with esptool 5.6.0 using `--no-stub flash-id`.
It reported ESP32-C3 QFN32 revision 0.4, 160 MHz, 40 MHz crystal, embedded XMC
Flash, manufacturer 0x46/device 0x4016, and **4 MB** detected Flash. Native USB
Serial/JTAG was available. This was identification only; no erase, upload,
eFuse programming or ATECC provisioning was performed.

Build measurements will vary with settings and toolchain versions. The recorded
release build uses PlatformIO Core 6.2.0, espressif32 8.0.0, ESP-IDF 6.1.0 and
RISC-V GCC 15.2.0+20251204. The local SCBK is configured; Google Smart Tap and
Apple VAS are enabled together for demo-pass bench tests alongside PKOC. Both
wallet implementations and their cryptographic algorithms are compiled and
linked. Public example configuration still contains no operational keys.

| Item | Bytes |
|---|---:|
| Physical Flash detected | 4,194,304 |
| Factory application partition | 3,145,728 |
| PlatformIO reported program use | 233,796 |
| PlatformIO reported static RAM | 18,780 |
| Application `.bin` file | 234,416 |

Static RAM excludes dynamic task stacks, driver allocations, crypto allocations
and other runtime heap use. The 3 MiB application partition is intentionally
larger than needed and provides no OTA slot. The remainder includes NVS, PHY
data and a reserved, currently unused data partition.

## Checks completed

- The complete ESP32-C3 application and bootloader compile/link with PlatformIO.
- **19 C test executables pass** under AddressSanitizer and
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
- The production feedback module is compiled with GPIO/clock boundary stubs
  to check the 100 ms local beep, controller-only LED behavior, timed OSDP
  patterns, invalid/unauthorized commands, disabled-buzzer behavior, session
  loss cleanup and timer wraparound.
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

## First ESP32 flash and startup checks

The firmware and bootloader were subsequently flashed through native USB and
esptool verified their hashes. The actual C3 passed AES-128/AES-256 encryption
and decryption and SHA-256 self-tests. ATECC configuration reads succeeded at
address `0x60`; PN532 HSU initialization returned firmware `0x32010607`.

The first boot exposed an ESP-IDF GPIO ownership warning during the ATECC wake
pulse. The HAL now resets the temporary GPIO configuration before returning
the pins to the I2C driver. A reflash confirmed successful ATECC and PN532
startup without those warnings.

The USB `s` report now includes OSDP RX/TX byte counts, TX errors, loop ticks,
RX pin level and DE pin readback. Initially, the controller received no replies
and the MCU RX count remained at one byte. After the user adjusted the physical
connections, the same firmware established Secure Channel with OSDP.Net.

The first PKOC transaction authenticated in **249 ms** and produced one real
256-bit encrypted credential event at the controller. Holding the card through
the remainder of the 60-second controller observation produced no duplicates:
`events=1`, `card_events=1`, `disconnects=0`, with no NFC transaction failures.
The wrong-key test received ten peer cryptograms but established no Secure
Channel. Restoring the correct key established Secure Channel again.

Google Smart Tap was then enabled locally using the user's supplied sample
key/collector and a trial merchant key version of 1. Initial taps included
transport failures, so per-step metadata diagnostics were added. Complete
exchanges then showed successful OSE and Smart Tap selection followed by
`9500` during negotiation: the handset rejected authentication. The failure
was not treated as a credential. A regression test now verifies rejection,
secret cleanup and absence of a GET DATA request for that status.

The supplied C sample's private scalar was compared locally with the
Google-authored demo reference and matched, but its collector ID did not.
The user confirmed Google's demo loyalty pass. The local collector was
corrected to **20180608**, key version **1**, as specified by
[the preserved sample configuration](https://github.com/kmandalas/smart-tap-sample-app/blob/192d1760bd8f44e8142dda6611c2a1314b35595b/app/src/main/java/com/google/smarttapsample/NegotiateCryptoCommand.java).
The updated image was flashed and hash-verified. Private settings and
configured firmware binaries remain excluded from Git. ATECC key slots
have not been provisioned or changed.

With the corrected collector, the actual Android phone returned `9000` for
selection, negotiation and GET DATA. The reader verified the HMAC, decrypted
the pass and queued `provider=2`; OSDP.Net received a real 256-bit event with
`secure=True`. Two successful reads took **1,872 ms** and **1,659 ms**. The user
confirmed two separate taps and successful phone feedback, matching the two
controller events. This proves the tested demo configuration, not arbitrary
Google passes or certification. The merchant signature used wolfCrypt in
software; no claim of an ATECC-held merchant key is made.

An Apple Wallet OSE response was initially received while Apple was disabled.
It was rejected as an unconfigured provider and produced no credential.

The user then confirmed the **PassKit NFC demo**. Apple was enabled locally
with pass type `pass.com.passkit.pksamples.nfcdemo` and the matching private
scalar from the supplied C sample, while retaining Google and PKOC. The
combined image was flashed and hash-verified, and the AES/SHA startup checks,
ATECC initialization, PN532 initialization and OSDP Secure Channel passed.

The actual iPhone returned `9000` for OSE selection and GET VAS DATA. The
reader validated the merchant key identifier and AES-256-GCM tag, decoded the
pass and queued `provider=1`. OSDP.Net received two real encrypted 256-bit
events, and the user confirmed two reads on the phone. These transactions took
**786 ms** and **783 ms**, with zero NFC transaction failures at that point.
This is a successful test of the manually opened PassKit demo; it does not
establish automatic Wallet discovery or one event per presentation under all
phone field/sleep behavior. Apple merchant ECDH used wolfCrypt in software;
the ATECC merchant slots remain unchanged.

During continued testing the PN532 logged intermittent transport timeouts
and recovered automatically, after which another Apple credential was
delivered in **778 ms**. A later `6287` user-action response produced no event;
the next successful read of the same credential was suppressed while the
presentation remained recorded. These observations do not establish that
transport recovery or physical removal detection is reliable under all conditions.

## LED and buzzer checks

The user confirmed S→GPIO3, +→3.3 V and −→GND for the bench buzzer. The local
configuration was enabled and flashed; PKOC, Apple and Google stayed enabled.
GPIO input/output mode now permits physical pin readback using USB command `f`.
The user requested **controller-only LED behavior**, so a read beeps but does
not light the LED or imply access grant.

An OSDP.Net test over Secure Channel sent two 300 ms beeps, checked timer
expiry and explicit OFF, then drove the LED on for five seconds, off for three
and temporarily on for five. Every command was acknowledged and all GPIO3/8
readbacks matched. The user confirmed both the audible and visible sequences.
The test ended with `secure=True`, `disconnects=0` and both outputs off.
Temporary desktop test tooling stayed outside this C firmware repository.

## Remaining hardware acceptance checks

Passing a host test or initializing the NFC chip does not satisfy these checks:

1. Confirm the SuperMini header labels, solder joints, module supplies and 3.3 V
   logic levels, especially RS-485 RO.
2. Repeat the now-passing AES/SHA startup check after a full cold power cycle.
3. Confirm ATECC configuration reads and repeatable random/signature operations
   over the new I2C HAL. Do not provision or lock the chip as a diagnostic.
4. Confirm PN532 cold boot, MCU-only reboot, discovery and APDU exchange over
   the new HSU C driver beyond the already-passing initialization commands.
5. Extend the passing Secure Channel/wrong-key checks to command rejection
   on an unsecured connection and a sustained soak test.
6. Extend the passing PKOC authentication/delivery/held-card check to repeated
   removal and re-presentation cycles.
7. Extend the passing LED/buzzer checks to reader unplug/reconnect and
   controller disconnect during authentication. Old queued events must not
   survive a new session.
8. Extend the successful Google and Apple demo tests to sustained holding,
   removal, other matched passes and phones. ECP discovery and compressed
   bundles are unsupported.
9. Measure task stack headroom, peak heap and OSDP latency during wallet
   cryptography, then run a sustained soak test.

The measured PKOC, Google demo and Apple demo results above are limited bench
results; they do not establish general wallet compatibility, cold-boot
reliability or certification.
