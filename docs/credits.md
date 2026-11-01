# Credits and license notices

Special thanks to **[Jonathan Horvath / bytedreamer](https://github.com/bytedreamer)**
and **[Z-bit Systems, LLC](https://github.com/Z-bit-Systems-LLC)** for the core
portable protocol libraries and desktop tooling:

- [AsymCred](https://github.com/Z-bit-Systems-LLC/AsymCred): PKOC authentication
  and credential mapping.
- [OSDP-Embedded](https://github.com/Z-bit-Systems-LLC/OSDP-Embedded): PD/ACU
  protocol, Secure Channel, feedback state machines and tests.
- [OSDP.Net](https://github.com/Z-bit-Systems-LLC/OSDP.Net): separate desktop
  controller and interoperability tooling.

Additional dependencies and references:

- [wolfSSL / wolfCrypt](https://github.com/wolfSSL/wolfssl): C cryptography and
  Espressif AES acceleration. Its build-generated ESP-IDF 6 overlay retains
  the original license notice.
- [Microchip CryptoAuthLib](https://github.com/MicrochipTech/cryptoauthlib):
  ATECC608 commands and configuration definitions.
- [Espressif ESP-IDF](https://github.com/espressif/esp-idf) and
  [PlatformIO](https://platformio.org/): SDK and reproducible build tooling.
- [NXP PN532 manual](https://www.nxp.com/docs/en/user-guide/141520.pdf): HSU
  framing and commands. [Adafruit's PN532 libraries](https://github.com/adafruit/Adafruit-PN532)
  informed the wake/SAM cold-start order. The new transport is C and does not
  compile Adafruit's Arduino C++ library.
- [Kormax Apple VAS research](https://github.com/kormax/apple-vas),
  [Kormax Google Smart Tap research](https://github.com/kormax/google-smart-tap)
  and [Grayson Martin / gm3197's VAS derivation research](https://gist.github.com/gm3197/ad0959476346cef69b75ea0523214350)
  informed protocol behavior. Original research source files and sample
  merchant secrets are not copied into this repository.
- The [STM32 project](https://github.com/evilcomputer12/stm32-pkoc-osdp-reader)
  supplied the integration behavior and regression baseline for this C port.

Project-authored source and documentation are GPL-3.0-or-later. This is not
a blanket license for dependencies or a combined binary. Consult each pinned
dependency's own notices:

| Component | License notice |
|---|---|
| AsymCred core/PKOC | `dependencies/asymcred/LICENSE.md` and `LICENSE-GPL-3.0.txt` |
| OSDP-Embedded | `dependencies/osdp-embedded/LICENSE.md`, `COPYING` |
| wolfSSL | `dependencies/wolfssl/COPYING` and source headers |
| CryptoAuthLib | `dependencies/cryptoauthlib/license.txt` |
| Unity test framework | `dependencies/osdp-embedded/tests/vendor/unity/LICENSE.txt` |
| ESP-IDF | Installed SDK's `LICENSE` and component notices |

CryptoAuthLib's notice restricts its software and derivatives to use with
Microchip products; this project interfaces with an ATECC608. Preserve the
upstream terms when redistributing applicable sources or binaries. No combined
firmware binaries are published by this initial repository preparation.

Names and trademarks identify compatibility or attribution. This project is
not sponsored, certified or endorsed by Apple, Google, Microchip, Espressif,
Z-bit Systems, sellers or library authors.
