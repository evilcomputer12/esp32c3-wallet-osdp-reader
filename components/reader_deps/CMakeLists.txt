set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(OSDP ${ROOT}/dependencies/osdp-embedded)
set(ASYM ${ROOT}/dependencies/asymcred)
set(ATCA ${ROOT}/dependencies/cryptoauthlib/lib)
set(WOLF ${ROOT}/dependencies/wolfssl)
file(GLOB OSDP_SRC ${OSDP}/core/src/shared/*.c ${OSDP}/core/src/sc/*.c
    ${OSDP}/core/src/commands/*.c ${OSDP}/core/src/replies/*.c ${OSDP}/pd/src/*.c)
file(GLOB_RECURSE ASYM_SRC ${ASYM}/core/src/*.c ${ASYM}/pkoc/src/*.c)
file(GLOB ATCA_SRC ${ATCA}/*.c ${ATCA}/calib/*.c ${ATCA}/host/*.c
    ${ATCA}/crypto/*.c ${ATCA}/crypto/hashes/*.c)
set(WOLF_SRC)
foreach(name sha256 hash random wc_port wc_encrypt error memory logging
        ecc sp_int sp_c32 asn coding wolfmath hmac kdf)
    list(APPEND WOLF_SRC ${WOLF}/wolfcrypt/src/${name}.c)
endforeach()
# ESP-IDF 6 moved AES clock control out of periph_module_enable(). Generate a
# narrow compatibility overlay; retain the pinned upstream file and its license.
file(READ ${WOLF}/wolfcrypt/src/port/Espressif/esp32_aes.c AES_SOURCE)
foreach(OLD "periph_module_enable(PERIPH_AES_MODULE);" "periph_module_disable(PERIPH_AES_MODULE);")
    string(FIND "${AES_SOURCE}" "${OLD}" MATCH_AT)
    if(MATCH_AT EQUAL -1)
        message(FATAL_ERROR "Review wolfCrypt ESP-IDF 6 AES compatibility after dependency change")
    endif()
endforeach()
string(REPLACE "periph_module_enable(PERIPH_AES_MODULE);"
    "esp_crypto_sha_aes_lock_acquire(); esp_crypto_aes_enable_periph_clk(true);" AES_SOURCE "${AES_SOURCE}")
string(REPLACE "periph_module_disable(PERIPH_AES_MODULE);"
    "esp_crypto_aes_enable_periph_clk(false); esp_crypto_sha_aes_lock_release();" AES_SOURCE "${AES_SOURCE}")
idf_build_get_property(IDF_BUILD_DIR BUILD_DIR)
file(MAKE_DIRECTORY ${IDF_BUILD_DIR}/reader_compat)
set(AES_OVERLAY ${IDF_BUILD_DIR}/reader_compat/esp32_aes_idf6.c)
file(WRITE ${AES_OVERLAY}
    "#include <esp_crypto_lock.h>\n#include <esp_crypto_periph_clk.h>\n${AES_SOURCE}")
# The pinned generic ECB bulk path calls software tables directly even when
# Espressif SetKey retained a raw hardware key. Route that case through the
# existing per-block dispatch instead (also used by CTR/GCM).
file(READ ${WOLF}/wolfcrypt/src/aes.c AES_CORE)
foreach(DIRECTION Encrypt Decrypt)
    set(OLD "#if defined(NEED_AES_TABLES)\n        Aes${DIRECTION}Blocks_C(aes, in, out, sz);")
    string(FIND "${AES_CORE}" "${OLD}" MATCH_AT)
    if(MATCH_AT EQUAL -1)
        message(FATAL_ERROR "Review wolfCrypt ECB hardware dispatch after dependency change")
    endif()
    string(REPLACE "${OLD}" "#if defined(NEED_AES_TABLES) && !defined(WOLFSSL_ESPIDF)\n        Aes${DIRECTION}Blocks_C(aes, in, out, sz);" AES_CORE "${AES_CORE}")
endforeach()
set(AES_CORE_OVERLAY ${IDF_BUILD_DIR}/reader_compat/aes_dispatch.c)
file(WRITE ${AES_CORE_OVERLAY} "${AES_CORE}")
list(APPEND WOLF_SRC ${AES_OVERLAY} ${AES_CORE_OVERLAY}
    ${WOLF}/wolfcrypt/src/port/Espressif/esp32_util.c)
idf_component_register(SRCS ${OSDP_SRC} ${ASYM_SRC} ${ATCA_SRC} ${WOLF_SRC}
    ${ATCA}/hal/atca_hal.c ${OSDP}/ports/wolfcrypt/osdp_sc_wolfcrypt.c
    INCLUDE_DIRS ${ROOT}/include ${OSDP}/core/include ${OSDP}/core/src
        ${OSDP}/pd/include ${OSDP}/pd/src ${OSDP}/ports/wolfcrypt
        ${ASYM}/core/include ${ASYM}/pkoc/include ${ATCA} ${WOLF}
    PRIV_INCLUDE_DIRS ${WOLF}/wolfcrypt/src
    REQUIRES freertos esp_hw_support esp_timer esp_driver_gpio esp_rom esp_system hal esp_security esp_hal_security)
target_compile_definitions(${COMPONENT_LIB} PUBLIC WOLFSSL_USER_SETTINGS
    OSDP_PD_BUF_LEN=256 OSDP_STREAM_BUFFER_LEN=1440)
target_compile_features(${COMPONENT_LIB} PUBLIC c_std_11)
