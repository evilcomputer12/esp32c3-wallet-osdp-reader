// Copy to config/reader_private.h and edit locally. This example authorizes no
// SC. Firmware images built with private keys are private too.
#pragma once
static const reader_settings_t reader_initial_settings = {
    .scbk_configured = false,
    .scbk = {0},
    .apple_enabled = false,
    .google_enabled = false,
    .apple_slot = -1,
    .google_slot = -1,
    .apple_pass_type = "",
    .apple_private = {0},
    .apple_public = {0},
    .google_private = {0},
    .google_collector = 0,
    .google_key_version = 0,
    .google_key_version_set = false,
    .buzzer_enabled = false};
