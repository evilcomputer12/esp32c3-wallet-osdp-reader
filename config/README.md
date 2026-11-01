# Private configuration

`src/reader_config.c` includes `config/reader_private.h` when present; otherwise
it uses `include/reader_config.example.h`. The public example enables no Secure
Channel or wallet keys. Files in this directory other than this README are
ignored by Git. Do not force-add them or publish a configured firmware binary.

## OSDP first

```sh
cp include/reader_config.example.h config/reader_private.h
chmod 600 config/reader_private.h
openssl rand -hex 16
```

The last command generates your new secret SCBK. Save it privately and replace
the header's `.scbk = {0}` with the 16 hex bytes, then set
`.scbk_configured = true`. Configure that exact 32-hex-character value in the
controller. The `.scbk` field takes a C byte array, **not a string**:

```c
// Syntax illustration only: replace every byte with your random key.
.scbk_configured = true,
.scbk = { YOUR_16_RANDOM_KEY_BYTES },
```

Do not use the illustration as a configuration. Keep the Apple/Google flags
false for the first PKOC bench test. The development checkout has already been
given a fresh random key in `reader_private.h` and `osdp-scbk.hex` with mode
0600; those files are local, not part of the repository. Do not overwrite an
existing local configuration with the `cp` command above.

Address 0 and 9600 baud are fixed in this version. It does not enable SCBK-D
installation mode and does not implement remote KEYSET. Changing the SCBK
requires a local rebuild/reflash and matching controller update. Treat that
key and the resulting image as secrets.

## Apple VAS

Set these fields for a pass you control:

| Field | Meaning |
|---|---|
| `apple_enabled` | `true` enables the provider |
| `apple_pass_type` | Exact pass type identifier beginning `pass.` |
| `apple_slot` | `-1` for a software private key; 0–15 for an existing ATECC slot |
| `apple_private[32]` | Big-endian P-256 private scalar, used only with slot `-1` |
| `apple_public[65]` | Uncompressed `04 || X || Y`, required with a chip slot |

The software path derives the public key itself. The slot path uses the supplied
public key to compute the four-byte Apple key identifier and calls the chip for
ECDH. The public key must match the provisioned slot and the pass issuer's
configuration. The identifier is the first four bytes of SHA-256 of the 32-byte
public X coordinate, not a hash of a PEM file.

The firmware accepts raw scalars/points, not PEM, PKCS#8 or X.509 files. Extract
and verify those values using your issuer tooling before configuring them.
No sample merchant keys from the old C demonstrations are copied here.

## Google Smart Tap

| Field | Meaning |
|---|---|
| `google_enabled` | `true` enables the provider |
| `google_collector` | Your 32-bit collector ID |
| `google_key_version` | The version registered for this merchant key |
| `google_key_version_set` | Must be `true` after you set the registered value |
| `google_slot` | `-1` for software signing; 0–15 for an existing ATECC signing slot |
| `google_private[32]` | Big-endian P-256 merchant scalar, only with slot `-1` |

The merchant **key version is independent of the Smart Tap protocol version**.
Do not set it to 1 just because the exchange negotiates protocol version 1.
The issuer/collector configuration and the wallet pass must match the key.
Google's ephemeral ECDH key remains temporary software state; selecting a chip
slot moves the long-term merchant signature operation to the ATECC.

## ATECC608 slot requirements

This reader can use an existing private P-256 slot only if its configuration
and data zones are locked and its permissions match the operation. It rejects
slots requiring authorization or limited-use counters and rejects ECDH settings
that require encrypted/protected output unsupported by this driver. See
`include/atecc_wallet_policy.h` for the precise read-only checks.

The merchant keys were **not** in the original chip. Selecting an arbitrary
slot number does not install a key. Provisioning is a separate, deliberate
operation; this firmware disables write, lock, GenKey and PrivWrite commands.
Do not lock a blank chip by copying an unrelated configuration.

The SCBK is stored in application Flash in this version. Software merchant
keys are also present in Flash/RAM. Secure Boot, Flash Encryption, encrypted
configuration storage and ATECC-backed SCBK storage are not implemented.

## Feedback

Keep `buzzer_enabled=false` until the GPIO3 interface is electrically suitable.
Setting it true enables an active-high output and advertises the OSDP buzzer.
The local 100 ms beep means a credential was queued; only the controller
decides access. Controller LED commands work independently of the buzzer.
