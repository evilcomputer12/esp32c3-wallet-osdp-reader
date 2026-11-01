# Architecture and crypto

## C application and library boundaries

```text
PN532 HSU ←→ pn532.c ←→ nfc_reader.c
                         ├─ AsymCred: PKOC state machine
                         └─ wallet_reader.c: VAS / Smart Tap state machine
                                      ↓
                                wallet_crypto.c
                                      ↓
                                 wolfCrypt
                           AES hardware / P-256 / hashes

ATECC608 ←→ ESP-IDF I2C HAL ←→ CryptoAuthLib ←→ atecc.c
     entropy, PKOC signature verification, optional merchant slot operations

NFC result → bounded event queue → OSDP-Embedded PD → UART/RS-485 → controller
                                             ↘ LED / optional buzzer
```

The project owns ten C implementation files. CMake compiles selected C sources
from pinned submodules rather than importing Arduino classes. `include/board.h`
owns the pin map, `user_settings.h` selects wolfCrypt features, and
`atca_config.h` selects the restricted CryptoAuthLib feature set.

## Scheduling

| Task | Priority | Requested stack | Role |
|---|---:|---:|---|
| OSDP | 10 | 8,192 bytes | Owns PD state, polls transport, sends replies and runs feedback |
| ATECC entropy | 7 | 4,096 bytes | Refills random-byte pool without blocking OSDP on I2C |
| NFC | 5 | 16,384 bytes | Owns PN532 and credential transactions |
| USB console | 2 | 4,096 bytes | Prints status on request |

ESP-IDF stack sizes are bytes. These allocations and driver buffers come from
the heap; the static RAM number printed by PlatformIO is not peak runtime RAM.
Hardware stack high-water marks and latency still need measuring.

The OSDP loop delays 2 ms between iterations, but UART transfer time and
scheduler load also affect response time. NFC can spend time in P-256 math or
waiting for the PN532 without monopolizing the PD task. This is not a measured
real-time guarantee yet.

Only the OSDP task touches the PD library. Other tasks enqueue events with the
Secure Channel generation from the start of the NFC transaction. Disconnects
clear both queues. Changed generations and events older than two seconds are
discarded. This prevents a transaction started on an old connection from being
delivered after a new Secure Channel has been established.

## How wolfCrypt and the ATECC608 work together

They are joined through explicit C callbacks; enabling wolfSSL alone does not
automatically move an algorithm into the secure element.

| Operation | Implementation in this port |
|---|---|
| Entropy for OSDP and wallet crypto | ATECC Random → 512-byte protected pool → `reader_rng_block` |
| PKOC SHA-256 challenge digest | wolfCrypt SHA-256 |
| PKOC ECDSA signature verification | CryptoAuthLib `atcab_verify_extern` on ATECC |
| Apple merchant ECDH | wolfCrypt software key, or explicit ATECC slot callback |
| Google merchant signature | wolfCrypt software key, or explicit ATECC slot callback |
| Google ephemeral ECDH | wolfCrypt P-256 in RAM |
| AES-128 and AES-256 blocks | wolfCrypt's ESP32-C3 hardware AES backend |
| GCM authentication, HMAC, HKDF, X9.63 | wolfCrypt code around the AES/hash primitives |
| X.509 verification | Not part of the reader transaction |

For example, PKOC's `verify` callback calls `reader_atecc_verify`, which calls
`atcab_verify_extern(hash, signature, public_key + 1, &verified)`. Merchant slot
callbacks check the chip's read-only configuration policy before issuing Sign
or ECDH. A mutex serializes all chip operations.

The background entropy task obtains 32 bytes at a time. Consumers never wait
for an I2C command inside the OSDP RNG callback: an empty/insufficient pool
returns an error, with no deterministic or weak fallback. Temporary keys,
decrypted wallet buffers and consumed pool bytes are wiped when finished.

## AES-128 versus AES-256

The C3 has hardware for **both** key lengths. This port uses that block engine
through wolfCrypt for OSDP AES-128, Smart Tap AES-128-CTR and Apple AES-256-GCM;
it does not attempt to turn two ATECC AES-128 calls into AES-256. The ATECC AES
engine is not used here. Keys used by the ESP32 block engine reside in MCU
memory; hardware acceleration does not itself give secure-element key storage.

The pinned wolfSSL Espressif port predates ESP-IDF 6's AES clock API change.
`components/reader_deps/CMakeLists.txt` generates a narrow overlay in the build
directory. It replaces the old `PERIPH_AES_MODULE` enable/disable calls with
the IDF SHA/AES peripheral lock and AES clock functions, retaining the upstream
wolfCrypt mutex and copyright. It checks the expected original strings and
fails if a dependency update requires review. No submodule source is edited.

At boot, `crypto_selftest.c` checks FIPS 197 AES-128/AES-256 encryption and
decryption vectors plus SHA-256 of `abc`. It runs before concurrent crypto
tasks, also initializing the hardware AES mutex. These checks passed on the
physical ESP32-C3 during the first flash, as well as on the host software path.

The same build overlay corrects two ECB bulk dispatch branches in the pinned
wolfCrypt source: with ESP-IDF, they must go through the existing per-block
hardware dispatch rather than use software tables with a raw hardware key.
CTR/GCM already use the per-block dispatch. The generated files remain in the
build directory; the dependency checkout is unchanged. Startup tests cover both
ECB directions, and the final ELF is checked for both hardware entry points.

## Credential flows

PKOC requires a verified challenge signature. The 256-bit credential mapping
comes from AsymCred. A public key's authenticity establishes possession, not
permission to open a door; the controller must authorize the resulting ID.

Only a PKOC SELECT response of `6A82` permits fallback to the configured wallet
providers. A failed PKOC signature is not silently treated as a wallet card.

Apple VAS validates the returned merchant key identifier and compressed P-256
point, performs ECDH, tries the two supported X9.63/SHA-256 derivations and
requires a valid AES-256-GCM tag. The VAS IV is **16 zero bytes**. One derivation
binds the pass identifier hash in the KDF; the other binds it as GCM AAD. The
four-byte Apple timestamp is parsed but no trusted-clock expiry policy is
implemented on the MCU.

Smart Tap binds the reader/phone nonces, collector and ephemeral point in the
merchant signature. It checks session identifiers and sequence values, derives
48 bytes with HKDF, authenticates IV/ciphertext with HMAC before decrypting,
and then parses bounded NDEF service records. The code requires an encrypted
bundle and rejects unexpected compression, plaintext, duplicate credentials
and malformed framing.

Wallet values become a fixed 256-bit identifier using SHA-256 over a domain
label, provider, pass-type hash or collector ID, encoding byte and value.
Raw pass contents are not sent in an OSDP event or printed to the USB log.
The controller must enroll and authorize this derived identifier.

## Compression, certificates and limitations

Compressed **P-256 public keys** are supported and validated before use.
Smart Tap **zlib payload compression** is neither advertised nor implemented.
These are separate features.

The runtime does not fetch or validate X.509 chains. Apple pass/merchant setup
and Google's issuer registration happen outside this reader. ATECC certificate
compression is disabled. ASN.1 code retained in wolfCrypt is used for ECDSA
signature encoding; it does not imply a TLS or certificate-validation flow.

The PN532 driver bounds command size, normal/extended reply size, response
chaining, timeouts and APDU accumulation. These checks do not replace actual
RF tests. ECP polling, arbitrary payment applications, multiple simultaneous
passes, PN532 SPI/I2C, Wi-Fi, BLE, OTA updates and remote provisioning are not
implemented in this version.
