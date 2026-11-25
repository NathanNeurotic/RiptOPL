# RiptOPL Round 9 — vendored mbedTLS dead-in-build census (`third_party/mbedtls/library`)

- Pin verified at start and finish: `2e347d5ac885cd0ff73e9ad4143781946396f492`, clone git-clean. Read-only; nothing in the clone was modified.
- Scope (vendored lens only): the tree is byte-for-byte identical to the official mbedtls-3.6.7 release archive (coordinator-verified, SHA-256) — **no pristine-upstream bug is filed here**. Every candidate below is class (a) dead-in-this-build, (c) size cost without use, or (d) local integration, i.e. it is about RiptOPL's two build definitions and two config headers, not about upstream code quality.
- Build facts relied on (coordinator-established, re-read here): `tools/build_edition.py:109-118` (RA editions) and `tools/build_edition.py:131-141` (non-RA standard builds with an update channel) each define an `ee_lib` target `mbedtls` with `sources = ["third_party/mbedtls/library/*.c"]` — a glob, all 109 `.c` files — and `MBEDTLS_CONFIG_FILE` set to `third_party/mbedtls/riptopl_mbedtls_config.h` resp. `third_party/mbedtls/riptopl_mbedtls_slim_config.h`. Builds with neither RA nor an update channel do not compile mbedTLS at all. Sole consumers: `src/platform/ratls.c` + `src/sources/ratls_ps2.cpp`.
- Corpus size: `library/*.c` = 109 files / 129,300 lines; `library/*.h` = 65 files / 21,112 lines; library total 150,412 lines. Public headers under `include/` are a further 53,307 lines and are include-path cost only (not censused per-file).

## Method

1. Both config headers read in full (§1).
2. Programmatic pass: every one of the 109 `.c` files was run through the host C preprocessor (`gcc -E`) once per config with that config's `MBEDTLS_CONFIG_FILE`, and the number of surviving non-blank lines originating from the `.c` file itself (tracked via linemarkers) was counted. A file with **0 surviving own-lines** is EMPTY — this is a stronger test than reading its outer guard, and it caught four files whose outer guard *passes* but whose entire body is inner-guarded out (§4). A file with surviving code is LIVE, except where the guard structure was read and shows only a stub-like remainder (PARTIAL, one file).
3. Derived macros were resolved by dumping the post-`build_info.h` macro set per config (`gcc -E -dM` on a TU including `mbedtls/build_info.h`) and by reading the derivation sites in `include/mbedtls/config_adjust_*.h` / `include/mbedtls/ssl_ciphersuites.h` (§2). The stock `include/mbedtls/mbedtls_config.h` is **not** used by either build (`MBEDTLS_CONFIG_FILE` replaces it), so macros defined only there — notably `MBEDTLS_ERROR_C` and `MBEDTLS_VERSION_C` — are OFF in both builds.
4. Spot validation (guard regions read directly, preprocessor result matched in every case): `aes.c`, `aesni.c`, `block_cipher.c`, `ecp_curves_new.c` (read in full), `entropy_poll.c`, `mps_reader.c`, `platform.c`, `platform_util.c`, `psa_crypto_storage.c`, `psa_util.c`, `sha256.c`, `ssl_tls13_server.c`, `timing.c`, `version_features.c`, `x509write_crt.c` — 15 files. Guard skeletons were also read for `entropy.c`, `ecp_curves.c`, `pk_wrap.c`, `ssl_ciphersuites.c`.
5. Caveat (INFERRED): preprocessing was done on an x86-64 Linux host, not the PS2 EE toolchain. This does not change any EMPTY/LIVE verdict: every arch-specific whole-file module (`aesni.c`, `aesce.c`, `padlock.c`) is already config-OFF, and the LIVE files all have generic C paths. Arch-accelerated *sections inside* LIVE files (AES/SHA-2/constant-time asm) differ on MIPS, but no file's class does. No PS2 build was run; nothing here is hardware- or PS2-build-verified.

## 1. The two config define sets (direct defines, read in full)

### 1a. RA config — `third_party/mbedtls/riptopl_mbedtls_config.h` (85 lines, 58 `MBEDTLS_*` defines)

Platform/entropy: `MBEDTLS_PLATFORM_C`, `MBEDTLS_HAVE_TIME`, `MBEDTLS_HAVE_TIME_DATE`, `MBEDTLS_PLATFORM_TIME_ALT`, `MBEDTLS_PLATFORM_MS_TIME_ALT`, `MBEDTLS_NO_PLATFORM_ENTROPY`, `MBEDTLS_ENTROPY_HARDWARE_ALT`, `MBEDTLS_ENTROPY_C`, `MBEDTLS_CTR_DRBG_C`.
TLS: `MBEDTLS_SSL_TLS_C`, `MBEDTLS_SSL_CLI_C`, `MBEDTLS_SSL_PROTO_TLS1_2`, `MBEDTLS_SSL_PROTO_TLS1_3`, `MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE`, `MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED`, `MBEDTLS_SSL_SERVER_NAME_INDICATION`, `MBEDTLS_SSL_KEEP_PEER_CERTIFICATE`, `MBEDTLS_SSL_ENCRYPT_THEN_MAC`, `MBEDTLS_SSL_EXTENDED_MASTER_SECRET`, `MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED`, `MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED`, `MBEDTLS_SSL_IN_CONTENT_LEN=16384`, `MBEDTLS_SSL_OUT_CONTENT_LEN=4096`.
PSA: `MBEDTLS_PSA_CRYPTO_C`, `MBEDTLS_USE_PSA_CRYPTO`.
Ciphers/hashes: `MBEDTLS_CIPHER_C`, `MBEDTLS_AES_C`, `MBEDTLS_GCM_C`, `MBEDTLS_CHACHA20_C`, `MBEDTLS_POLY1305_C`, `MBEDTLS_CHACHAPOLY_C`, `MBEDTLS_MD_C`, `MBEDTLS_SHA224_C`, `MBEDTLS_SHA256_C`, `MBEDTLS_SHA384_C`, `MBEDTLS_SHA512_C`, `MBEDTLS_HKDF_C`.
Key exchange/signatures: `MBEDTLS_BIGNUM_C`, `MBEDTLS_ECP_C`, `MBEDTLS_ECDH_C`, `MBEDTLS_ECDSA_C`, `MBEDTLS_ECP_DP_SECP256R1_ENABLED`, `MBEDTLS_ECP_DP_SECP384R1_ENABLED`, `MBEDTLS_ECP_DP_CURVE25519_ENABLED`, `MBEDTLS_ECP_NIST_OPTIM`, `MBEDTLS_RSA_C`, `MBEDTLS_PKCS1_V15`, `MBEDTLS_PKCS1_V21`.
Certificates: `MBEDTLS_ASN1_PARSE_C`, `MBEDTLS_ASN1_WRITE_C`, `MBEDTLS_OID_C`, `MBEDTLS_PK_C`, `MBEDTLS_PK_PARSE_C`, `MBEDTLS_X509_USE_C`, `MBEDTLS_X509_CRT_PARSE_C`, `MBEDTLS_X509_CHECK_KEY_USAGE`, `MBEDTLS_X509_CHECK_EXTENDED_KEY_USAGE`, `MBEDTLS_TEST_SW_INET_PTON`.

### 1b. Slim (updater) config — `third_party/mbedtls/riptopl_mbedtls_slim_config.h` (72 lines, 45 `MBEDTLS_*` defines)

Identical to §1a **except** the following are absent: `MBEDTLS_SSL_PROTO_TLS1_3`, `MBEDTLS_SSL_TLS1_3_COMPATIBILITY_MODE`, `MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_EPHEMERAL_ENABLED`, `MBEDTLS_SSL_KEEP_PEER_CERTIFICATE`, `MBEDTLS_PSA_CRYPTO_C`, `MBEDTLS_USE_PSA_CRYPTO`, `MBEDTLS_CHACHA20_C`, `MBEDTLS_POLY1305_C`, `MBEDTLS_CHACHAPOLY_C`, `MBEDTLS_SHA224_C`, `MBEDTLS_HKDF_C`, `MBEDTLS_ECP_DP_CURVE25519_ENABLED`, `MBEDTLS_PKCS1_V21`. Everything else — platform/entropy set, TLS 1.2 client set, `MBEDTLS_AES_C`/`MBEDTLS_GCM_C`, SHA-256/384/512, bignum/ECP/ECDH/ECDSA (P-256 + P-384), RSA + PKCS1_V15, and the full certificate set — is present unchanged.

### 1c. Notable absences in BOTH configs (drive most of the EMPTY set)

`MBEDTLS_SSL_SRV_C`, `MBEDTLS_NET_C`, `MBEDTLS_TIMING_C`, `MBEDTLS_DEBUG_C`, `MBEDTLS_FS_IO`, `MBEDTLS_THREADING_C`, `MBEDTLS_ERROR_C`, `MBEDTLS_VERSION_C`, `MBEDTLS_BASE64_C`, `MBEDTLS_PEM_PARSE_C`, `MBEDTLS_PEM_WRITE_C`, `MBEDTLS_PK_WRITE_C`, `MBEDTLS_X509_CREATE_C`, `MBEDTLS_X509_CRT_WRITE_C`, `MBEDTLS_X509_CSR_WRITE_C`, `MBEDTLS_X509_CRL_PARSE_C`, `MBEDTLS_X509_CSR_PARSE_C`, `MBEDTLS_SSL_CACHE_C`, `MBEDTLS_SSL_COOKIE_C`, `MBEDTLS_SSL_TICKET_C`, all non-AES block ciphers (`ARIA`/`CAMELLIA`/`DES`), `MBEDTLS_CCM_C`, `MBEDTLS_CMAC_C`, `MBEDTLS_DHM_C`, `MBEDTLS_ECJPAKE_C`, `MBEDTLS_HMAC_DRBG_C`, `MBEDTLS_LMS_C`, `MBEDTLS_MD5_C`, `MBEDTLS_SHA1_C`, `MBEDTLS_SHA3_C`, `MBEDTLS_RIPEMD160_C`, `MBEDTLS_PKCS5_C`, `MBEDTLS_PKCS7_C`, `MBEDTLS_PKCS12_C`, `MBEDTLS_NIST_KW_C`, `MBEDTLS_MEMORY_BUFFER_ALLOC_C`, `MBEDTLS_AESNI_C`, `MBEDTLS_AESCE_C`, `MBEDTLS_PADLOCK_C`, `MBEDTLS_PSA_CRYPTO_STORAGE_C`, `MBEDTLS_PSA_CRYPTO_SE_C`, `MBEDTLS_PSA_ITS_FILE_C`, `MBEDTLS_ECP_WITH_MPI_UINT`. (`.github/scripts/test_mbedtls_vendor.py` independently asserts the `SRV_C`/`FS_IO`/`NET_C`/`TIMING_C`/`DEBUG_C` absences for both configs.)

## 2. Derived macros that decide guards (traced, not directly defined)

- `MBEDTLS_PSA_CRYPTO_CLIENT` — defined at `include/mbedtls/config_adjust_legacy_crypto.h:97-98` when `MBEDTLS_PSA_CRYPTO_C` is defined. **RA: ON; slim: OFF.** Decides `psa_crypto_client.c` and the body of `psa_util.c`.
- `MBEDTLS_MD_LIGHT` — `config_adjust_legacy_crypto.h:119-120` (from `MBEDTLS_MD_C`, with a second derivation path at `:126-135`). **ON in both.** Decides `md.c`.
- `MBEDTLS_ECP_LIGHT` — `config_adjust_legacy_crypto.h:409-413` (from `MBEDTLS_ECP_C` et al.). **ON in both.** Decides `ecp.c` / `ecp_curves.c`.
- `MBEDTLS_BLOCK_CIPHER_C` — `config_adjust_legacy_crypto.h:372-374`: defined only if (`GCM_C` or `CCM_C`) **and** (`CIPHER_C` undefined **or** `MBEDTLS_BLOCK_CIPHER_SOME_PSA`). Both configs define `MBEDTLS_CIPHER_C` and neither has a PSA-accelerated block cipher, so **OFF in both** — `block_cipher.c` is EMPTY and GCM goes through `cipher.c` via the inline fallbacks in `library/block_cipher_internal.h` (read: `library/gcm.c:28` selects on this macro).
- `PSA_WANT_*` / `MBEDTLS_PSA_BUILTIN_*` (RA only) — derived in `include/mbedtls/config_adjust_psa_from_legacy.h` from the legacy defines (no `MBEDTLS_PSA_CRYPTO_CONFIG` in either config). The load-bearing one: `MBEDTLS_HKDF_C` ⇒ `PSA_WANT_ALG_HKDF` + `PSA_WANT_ALG_HKDF_EXTRACT` + `PSA_WANT_ALG_HKDF_EXPAND` + the three `MBEDTLS_PSA_BUILTIN_ALG_HKDF*` at `config_adjust_psa_from_legacy.h:121-130`. See CENSUS-2 — the *define* is load-bearing even though `hkdf.c` is not.
- `MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_SOME_EPHEMERAL_ENABLED` — derived at `include/mbedtls/ssl_ciphersuites.h:399` from the direct `…EPHEMERAL_ENABLED`. RA: ON; slim: the base macro is `#undef`ed by `include/mbedtls/config_adjust_ssl.h:78-80` when TLS 1.3 is off.
- `MBEDTLS_ECP_HAVE_SECP256R1` / `…SECP384R1` / (RA) `…CURVE25519`, `MBEDTLS_PK_HAVE_ECC_KEYS`, `MBEDTLS_PK_CAN_ECDSA_*`, `MBEDTLS_MD_CAN_SHA*`, `MBEDTLS_SSL_HAVE_AES/GCM/(RA: CHACHAPOLY)/AEAD`, `MBEDTLS_CAN_ECDH` — all derived in `config_adjust_legacy_crypto.h` (ECP_HAVE at :465-502, PK_CAN at :444-461, SSL_HAVE at :539-575); ON in both configs for the enabled algorithms. These decide internal sections, not whole files, except `MBEDTLS_PK_HAVE_ECC_KEYS` in `pk_ecc.c` (ON in both).
- `MBEDTLS_PLATFORM_SNPRINTF_ALT` / `MBEDTLS_PLATFORM_VSNPRINTF_ALT` — auto-derived at `config_adjust_legacy_crypto.h:41-47` (no STD/MACRO override in either config). ON in both; two of the few surviving sections of `platform.c` (§4).

## 3. Census table — all 109 `library/*.c` files

Classes: **LIVE** = substantive code compiles; **EMPTY** = 0 surviving own-lines after preprocessing under that config (or, for `ecp_curves_new.c`, only its dummy typedef); **PARTIAL** = outer guard passes but the read guard structure shows only a stub-like remainder (§4). Guard macro(s) are the file's body guard as read; line cites are to `third_party/mbedtls/library/<file>` unless prefixed otherwise.

| File | Lines | RA config | Slim config | Guard macro(s) |
|---|---|---|---|---|
| aes.c | 2294 | LIVE | LIVE | `defined(MBEDTLS_AES_C)` (aes.c:16) |
| aesce.c | 624 | EMPTY | EMPTY | `MBEDTLS_AESCE_C` (aesce.c:44) |
| aesni.c | 846 | EMPTY | EMPTY | `defined(MBEDTLS_AESNI_C)` (aesni.c:15) |
| aria.c | 969 | EMPTY | EMPTY | `defined(MBEDTLS_ARIA_C)` (aria.c:16) |
| asn1parse.c | 468 | LIVE | LIVE | `MBEDTLS_ASN1_PARSE_C || MBEDTLS_X509_CREATE_C || MBEDTLS_PSA_UTIL_HAVE_ECDSA` (asn1parse.c:10-11) |
| asn1write.c | 440 | LIVE | LIVE | `MBEDTLS_ASN1_WRITE_C || MBEDTLS_X509_USE_C || MBEDTLS_PSA_UTIL_HAVE_ECDSA` (asn1write.c:10-11) |
| base64.c | 322 | EMPTY | EMPTY | `defined(MBEDTLS_BASE64_C)` (base64.c:10) |
| bignum.c | 2583 | LIVE | LIVE | `defined(MBEDTLS_BIGNUM_C)` (bignum.c:26) |
| bignum_core.c | 1240 | LIVE | LIVE | `defined(MBEDTLS_BIGNUM_C)` (bignum_core.c:10) |
| bignum_mod.c | 394 | EMPTY | EMPTY | `defined(MBEDTLS_BIGNUM_C) && defined(MBEDTLS_ECP_WITH_MPI_UINT)` (bignum_mod.c:10) |
| bignum_mod_raw.c | 276 | EMPTY | EMPTY | `defined(MBEDTLS_BIGNUM_C) && defined(MBEDTLS_ECP_WITH_MPI_UINT)` (bignum_mod_raw.c:10) |
| block_cipher.c | 207 | EMPTY | EMPTY | `MBEDTLS_BLOCK_CIPHER_C` (block_cipher.c:22) — derived, OFF in both (see §4) |
| camellia.c | 1058 | EMPTY | EMPTY | `defined(MBEDTLS_CAMELLIA_C)` (camellia.c:16) |
| ccm.c | 777 | EMPTY | EMPTY | `defined(MBEDTLS_CCM_C)` (ccm.c:19) |
| chacha20.c | 563 | LIVE | EMPTY | `defined(MBEDTLS_CHACHA20_C)` (chacha20.c:14) |
| chachapoly.c | 487 | LIVE | EMPTY | `defined(MBEDTLS_CHACHAPOLY_C)` (chachapoly.c:11) |
| cipher.c | 1712 | LIVE | LIVE | `defined(MBEDTLS_CIPHER_C)` (cipher.c:14) |
| cipher_wrap.c | 2482 | LIVE | LIVE | `defined(MBEDTLS_CIPHER_C)` (cipher_wrap.c:14) |
| cmac.c | 1067 | EMPTY | EMPTY | `defined(MBEDTLS_CMAC_C)` (cmac.c:31) |
| constant_time.c | 247 | LIVE | LIVE | (no outer guard — always compiles) |
| ctr_drbg.c | 1016 | LIVE | LIVE | `defined(MBEDTLS_CTR_DRBG_C)` (ctr_drbg.c:15) |
| debug.c | 475 | EMPTY | EMPTY | `defined(MBEDTLS_DEBUG_C)` (debug.c:10) |
| des.c | 1042 | EMPTY | EMPTY | `defined(MBEDTLS_DES_C)` (des.c:16) |
| dhm.c | 700 | EMPTY | EMPTY | `defined(MBEDTLS_DHM_C)` (dhm.c:18) |
| ecdh.c | 696 | LIVE | LIVE | `defined(MBEDTLS_ECDH_C)` (ecdh.c:17) |
| ecdsa.c | 858 | LIVE | LIVE | `defined(MBEDTLS_ECDSA_C)` (ecdsa.c:16) |
| ecjpake.c | 1216 | EMPTY | EMPTY | `defined(MBEDTLS_ECJPAKE_C)` (ecjpake.c:15) |
| ecp.c | 3674 | LIVE | LIVE | `MBEDTLS_ECP_LIGHT` (ecp.c:63) — derived from `MBEDTLS_ECP_C` |
| ecp_curves.c | 6125 | LIVE | LIVE | `defined(MBEDTLS_ECP_LIGHT)` (ecp_curves.c:10) |
| ecp_curves_new.c | 9 | EMPTY | EMPTY | (none — dummy typedef only, ecp_curves_new.c:9) |
| entropy.c | 680 | LIVE | LIVE | `defined(MBEDTLS_ENTROPY_C)` (entropy.c:10) |
| entropy_poll.c | 233 | EMPTY | EMPTY | `MBEDTLS_ENTROPY_C` (entropy_poll.c:19), but every inner source section is off (see §4) |
| error.c | 878 | EMPTY | EMPTY | `defined(MBEDTLS_ERROR_C) || defined(MBEDTLS_ERROR_STRERROR_DUMMY)` (error.c:12) |
| gcm.c | 1330 | LIVE | LIVE | `defined(MBEDTLS_GCM_C)` (gcm.c:20) |
| hkdf.c | 161 | LIVE | EMPTY | `defined(MBEDTLS_HKDF_C)` (hkdf.c:9) |
| hmac_drbg.c | 633 | EMPTY | EMPTY | `defined(MBEDTLS_HMAC_DRBG_C)` (hmac_drbg.c:16) |
| lmots.c | 789 | EMPTY | EMPTY | `defined(MBEDTLS_LMS_C)` (lmots.c:23) |
| lms.c | 778 | EMPTY | EMPTY | `defined(MBEDTLS_LMS_C)` (lms.c:23) |
| md.c | 1108 | LIVE | LIVE | `MBEDTLS_MD_LIGHT` (md.c:30) — derived from `MBEDTLS_MD_C` |
| md5.c | 426 | EMPTY | EMPTY | `defined(MBEDTLS_MD5_C)` (md5.c:15) |
| memory_buffer_alloc.c | 751 | EMPTY | EMPTY | `defined(MBEDTLS_MEMORY_BUFFER_ALLOC_C)` (memory_buffer_alloc.c:10) |
| mps_reader.c | 538 | LIVE | EMPTY | `defined(MBEDTLS_SSL_PROTO_TLS1_3)` (mps_reader.c:10) |
| mps_trace.c | 112 | EMPTY | EMPTY | `MBEDTLS_SSL_PROTO_TLS1_3` (mps_trace.c:10) + inner `MBEDTLS_MPS_ENABLE_TRACE` (mps_trace.c:14), off in both |
| net_sockets.c | 694 | EMPTY | EMPTY | `defined(MBEDTLS_NET_C)` (net_sockets.c:20) |
| nist_kw.c | 729 | EMPTY | EMPTY | `defined(MBEDTLS_NIST_KW_C)` (nist_kw.c:20) |
| oid.c | 1166 | LIVE | LIVE | `defined(MBEDTLS_OID_C)` (oid.c:12) |
| padlock.c | 157 | EMPTY | EMPTY | `defined(MBEDTLS_PADLOCK_C)` (padlock.c:16) |
| pem.c | 554 | EMPTY | EMPTY | `defined(MBEDTLS_PEM_PARSE_C) || defined(MBEDTLS_PEM_WRITE_C)` (pem.c:10) |
| pk.c | 1602 | LIVE | LIVE | `defined(MBEDTLS_PK_C)` (pk.c:10) |
| pk_ecc.c | 261 | LIVE | LIVE | `defined(MBEDTLS_PK_C) && defined(MBEDTLS_PK_HAVE_ECC_KEYS)` (pk_ecc.c:15) |
| pk_wrap.c | 1618 | LIVE | LIVE | `defined(MBEDTLS_PK_C)` (pk_wrap.c:12) |
| pkcs12.c | 437 | EMPTY | EMPTY | `defined(MBEDTLS_PKCS12_C)` (pkcs12.c:16) |
| pkcs5.c | 500 | EMPTY | EMPTY | `defined(MBEDTLS_PKCS5_C)` (pkcs5.c:20) |
| pkcs7.c | 787 | EMPTY | EMPTY | `defined(MBEDTLS_PKCS7_C)` (pkcs7.c:8) |
| pkparse.c | 1392 | LIVE | LIVE | `defined(MBEDTLS_PK_PARSE_C)` (pkparse.c:10) |
| pkwrite.c | 631 | EMPTY | EMPTY | `defined(MBEDTLS_PK_WRITE_C)` (pkwrite.c:10) |
| platform.c | 402 | PARTIAL | PARTIAL | `defined(MBEDTLS_PLATFORM_C)` (platform.c:10) |
| platform_util.c | 258 | LIVE | LIVE | (no outer guard; §4 note on the ms-time section) |
| poly1305.c | 492 | LIVE | EMPTY | `defined(MBEDTLS_POLY1305_C)` (poly1305.c:11) |
| psa_crypto.c | 9532 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto.c:12) |
| psa_crypto_aead.c | 646 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_aead.c:11) |
| psa_crypto_cipher.c | 747 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_cipher.c:11) |
| psa_crypto_client.c | 22 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_CLIENT)` (psa_crypto_client.c:12) |
| psa_crypto_driver_wrappers_no_static.c | 256 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_driver_wrappers_no_static.c:25) |
| psa_crypto_ecp.c | 594 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_ecp.c:11) |
| psa_crypto_ffdh.c | 361 | EMPTY | EMPTY | `MBEDTLS_PSA_CRYPTO_C` (psa_crypto_ffdh.c:11), but inner `MBEDTLS_PSA_BUILTIN_*DH*` guards all off (see §4) |
| psa_crypto_hash.c | 470 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_hash.c:11) |
| psa_crypto_mac.c | 505 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_mac.c:11) |
| psa_crypto_pake.c | 571 | EMPTY | EMPTY | `MBEDTLS_PSA_CRYPTO_C` (psa_crypto_pake.c:11), but inner `MBEDTLS_PSA_BUILTIN_ALG_JPAKE` / `MBEDTLS_PSA_BUILTIN_PAKE` guards off (see §4) |
| psa_crypto_random.c | 181 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C) && !defined(MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG)` (psa_crypto_random.c:11) |
| psa_crypto_rsa.c | 714 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_rsa.c:11) |
| psa_crypto_se.c | 373 | EMPTY | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_SE_C)` (psa_crypto_se.c:11) |
| psa_crypto_slot_management.c | 1137 | LIVE | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_C)` (psa_crypto_slot_management.c:11) |
| psa_crypto_storage.c | 481 | EMPTY | EMPTY | `defined(MBEDTLS_PSA_CRYPTO_STORAGE_C)` (psa_crypto_storage.c:11) |
| psa_its_file.c | 254 | EMPTY | EMPTY | `defined(MBEDTLS_PSA_ITS_FILE_C)` (psa_its_file.c:11) |
| psa_util.c | 614 | LIVE | EMPTY | `MBEDTLS_PSA_CRYPTO_CLIENT` (psa_util.c:21) — derived, RA only (see §2) |
| ripemd160.c | 490 | EMPTY | EMPTY | `defined(MBEDTLS_RIPEMD160_C)` (ripemd160.c:16) |
| rsa.c | 3121 | LIVE | LIVE | `defined(MBEDTLS_RSA_C)` (rsa.c:28) |
| rsa_alt_helpers.c | 455 | LIVE | LIVE | `defined(MBEDTLS_RSA_C)` (rsa_alt_helpers.c:11) |
| sha1.c | 480 | EMPTY | EMPTY | `defined(MBEDTLS_SHA1_C)` (sha1.c:15) |
| sha256.c | 983 | LIVE | LIVE | `MBEDTLS_SHA256_C || MBEDTLS_SHA224_C` (sha256.c:56) |
| sha3.c | 721 | EMPTY | EMPTY | `defined(MBEDTLS_SHA3_C)` (sha3.c:15) |
| sha512.c | 1115 | LIVE | LIVE | `defined(MBEDTLS_SHA512_C) || defined(MBEDTLS_SHA384_C)` (sha512.c:34) |
| ssl_cache.c | 410 | EMPTY | EMPTY | `defined(MBEDTLS_SSL_CACHE_C)` (ssl_cache.c:14) |
| ssl_ciphersuites.c | 2050 | LIVE | LIVE | `defined(MBEDTLS_SSL_TLS_C)` (ssl_ciphersuites.c:12) |
| ssl_client.c | 1023 | LIVE | LIVE | `defined(MBEDTLS_SSL_CLI_C)` (ssl_client.c:10) |
| ssl_cookie.c | 384 | EMPTY | EMPTY | `defined(MBEDTLS_SSL_COOKIE_C)` (ssl_cookie.c:14) |
| ssl_debug_helpers_generated.c | 251 | EMPTY | EMPTY | `defined(MBEDTLS_DEBUG_C)` (ssl_debug_helpers_generated.c:16) |
| ssl_msg.c | 6619 | LIVE | LIVE | `defined(MBEDTLS_SSL_TLS_C)` (ssl_msg.c:15) |
| ssl_ticket.c | 556 | EMPTY | EMPTY | `defined(MBEDTLS_SSL_TICKET_C)` (ssl_ticket.c:10) |
| ssl_tls.c | 10234 | LIVE | LIVE | `defined(MBEDTLS_SSL_TLS_C)` (ssl_tls.c:14) |
| ssl_tls12_client.c | 3688 | LIVE | LIVE | `defined(MBEDTLS_SSL_CLI_C) && defined(MBEDTLS_SSL_PROTO_TLS1_2)` (ssl_tls12_client.c:10) |
| ssl_tls12_server.c | 4311 | EMPTY | EMPTY | `defined(MBEDTLS_SSL_SRV_C) && defined(MBEDTLS_SSL_PROTO_TLS1_2)` (ssl_tls12_server.c:10) |
| ssl_tls13_client.c | 3208 | LIVE | EMPTY | `defined(MBEDTLS_SSL_CLI_C) && defined(MBEDTLS_SSL_PROTO_TLS1_3)` (ssl_tls13_client.c:10) |
| ssl_tls13_generic.c | 1793 | LIVE | EMPTY | `defined(MBEDTLS_SSL_TLS_C) && defined(MBEDTLS_SSL_PROTO_TLS1_3)` (ssl_tls13_generic.c:10) |
| ssl_tls13_keys.c | 1918 | LIVE | EMPTY | `defined(MBEDTLS_SSL_PROTO_TLS1_3)` (ssl_tls13_keys.c:10) |
| ssl_tls13_server.c | 3624 | EMPTY | EMPTY | `defined(MBEDTLS_SSL_SRV_C) && defined(MBEDTLS_SSL_PROTO_TLS1_3)` (ssl_tls13_server.c:10) |
| threading.c | 193 | EMPTY | EMPTY | `defined(MBEDTLS_THREADING_C)` (threading.c:18) |
| timing.c | 152 | EMPTY | EMPTY | `defined(MBEDTLS_TIMING_C)` (timing.c:10) |
| version.c | 32 | EMPTY | EMPTY | `defined(MBEDTLS_VERSION_C)` (version.c:10) |
| version_features.c | 856 | EMPTY | EMPTY | `defined(MBEDTLS_VERSION_C)` (version_features.c:10) |
| x509.c | 1776 | LIVE | LIVE | `defined(MBEDTLS_X509_USE_C)` (x509.c:20) |
| x509_create.c | 570 | EMPTY | EMPTY | `defined(MBEDTLS_X509_CREATE_C)` (x509_create.c:10) |
| x509_crl.c | 708 | EMPTY | EMPTY | `defined(MBEDTLS_X509_CRL_PARSE_C)` (x509_crl.c:20) |
| x509_crt.c | 3311 | LIVE | LIVE | `defined(MBEDTLS_X509_CRT_PARSE_C)` (x509_crt.c:22) |
| x509_csr.c | 648 | EMPTY | EMPTY | `defined(MBEDTLS_X509_CSR_PARSE_C)` (x509_csr.c:20) |
| x509write.c | 174 | EMPTY | EMPTY | `defined(MBEDTLS_X509_CSR_WRITE_C) || defined(MBEDTLS_X509_CRT_WRITE_C)` (x509write.c:8) |
| x509write_crt.c | 688 | EMPTY | EMPTY | `defined(MBEDTLS_X509_CRT_WRITE_C)` (x509write_crt.c:16) |
| x509write_csr.c | 336 | EMPTY | EMPTY | `defined(MBEDTLS_X509_CSR_WRITE_C)` (x509write_csr.c:15) |

## 4. Totals and the files a guard-only reading gets wrong

### Totals (preprocessor-verified)

| Config | LIVE | PARTIAL | EMPTY | EMPTY lines | EMPTY share of 129,300 |
|---|---|---|---|---|---|
| RA (`riptopl_mbedtls_config.h`) | 53 | 1 (`platform.c`) | 55 | 36,695 | 28.4% |
| Slim (`riptopl_mbedtls_slim_config.h`) | 33 | 1 (`platform.c`) | 75 | 61,273 | 47.4% |
| **Empty under BOTH configs (intersection)** | — | — | **55** | **36,695** | 28.4% |

The RA EMPTY set *is* the both-configs intersection: the slim EMPTY set is a strict superset. The 20 slim-only EMPTY files (24,578 lines) are `chacha20.c`, `chachapoly.c`, `hkdf.c`, `mps_reader.c`, `poly1305.c`, `psa_crypto.c`, `psa_crypto_aead.c`, `psa_crypto_cipher.c`, `psa_crypto_client.c`, `psa_crypto_driver_wrappers_no_static.c`, `psa_crypto_ecp.c`, `psa_crypto_hash.c`, `psa_crypto_mac.c`, `psa_crypto_random.c`, `psa_crypto_rsa.c`, `psa_crypto_slot_management.c`, `psa_util.c`, `ssl_tls13_client.c`, `ssl_tls13_generic.c`, `ssl_tls13_keys.c` — exactly the TLS 1.3 + PSA + ChaCha/HKDF surface the slim header's own comment says it removes.

### Files where the outer guard alone misleads (all read directly)

- **`entropy_poll.c` — EMPTY in both, although `MBEDTLS_ENTROPY_C` is ON.** Body guard at `library/entropy_poll.c:19` passes, but the platform-source block (`:30-206`) is compiled out by `MBEDTLS_NO_PLATFORM_ENTROPY` (defined in both configs) and the NV-seed block (`:208-231`) by the absent `MBEDTLS_ENTROPY_NV_SEED`. Nothing else remains. The only entropy source in either build is RiptOPL's own `mbedtls_hardware_poll()` (`src/sources/ratls_ps2.cpp`), registered by `entropy.c:48-52` under `MBEDTLS_ENTROPY_HARDWARE_ALT`.
- **`block_cipher.c` — EMPTY in both via a derived macro** (`MBEDTLS_BLOCK_CIPHER_C`, §2), not via any macro named in either config header.
- **`bignum_mod.c` / `bignum_mod_raw.c` — EMPTY in both.** Guard `MBEDTLS_BIGNUM_C && MBEDTLS_ECP_WITH_MPI_UINT` (`library/bignum_mod.c:10`, `library/bignum_mod_raw.c:10`); the second macro exists only as a commented-out option in the unused stock config (`include/mbedtls/mbedtls_config.h:847`) and is derived nowhere. ECP therefore uses the legacy MPI path in `bignum.c`/`bignum_core.c` in both builds.
- **`psa_crypto_ffdh.c` / `psa_crypto_pake.c` — EMPTY even under RA, although `MBEDTLS_PSA_CRYPTO_C` is ON.** Their inner builtin guards (DH key-pair types at `library/psa_crypto_ffdh.c:26`; `MBEDTLS_PSA_BUILTIN_ALG_JPAKE` at `library/psa_crypto_pake.c:104`) are all off because no `PSA_WANT` for FFDH or JPAKE is derived from either legacy config.
- **`mps_trace.c` — EMPTY in both, including RA where its outer guard (`MBEDTLS_SSL_PROTO_TLS1_3`, `library/mps_trace.c:10`) passes:** the whole body sits under `MBEDTLS_MPS_ENABLE_TRACE` (`:14`), defined nowhere.
- **`error.c` / `version.c` / `version_features.c` — EMPTY in both** because `MBEDTLS_ERROR_C` / `MBEDTLS_VERSION_C` are defined only in the unused stock `include/mbedtls/mbedtls_config.h:2907` / `:3822`. Consequence (checked against the consumers, §5): `mbedtls_strerror()` and `mbedtls_version_get_string()` do not exist in either build; neither consumer calls them, so this is currently harmless — but a future diagnostic call to either will fail at link time, not compile time.
- **`ecp_curves_new.c` — EMPTY in both by upstream design:** the whole 9-line file is a comment plus `typedef int iso_c_forbids_an_empty_translation_unit;` at `library/ecp_curves_new.c:9` ("kept only for compatibility with custom build systems in LTS branches", `:1-3`).
- **`platform.c` — PARTIAL in both (the only PARTIAL).** Guard skeleton read in full (`library/platform.c:10-402`): the memory family (`:20-64`, `MBEDTLS_PLATFORM_MEMORY` off), printf/fprintf/setbuf/exit ALT families (`:169-259`, off) and NV-seed family (`:288-380`, off) all compile out. What survives (~15 code lines by preprocessing) is the derived snprintf/vsnprintf function pointers (`:83-167`), the time pointer + `mbedtls_platform_set_time()` under `MBEDTLS_PLATFORM_TIME_ALT` (`:261-286`), and the empty setup/teardown pair (`:382-400`). That remainder is load-bearing — `src/platform/ratls.c:244` calls `mbedtls_platform_set_time()` — so PARTIAL here means "small but used", not "removable".
- **`platform_util.c` — LIVE in both, with one named section out by design:** it has no outer config guard; `mbedtls_platform_zeroize()` and the `gmtime_r` fallback compile, while the `mbedtls_ms_time()` implementation (`library/platform_util.c:215-258`) is compiled out by `MBEDTLS_PLATFORM_MS_TIME_ALT` because RiptOPL supplies that function itself (`src/sources/ratls_ps2.cpp`).

## 5. Non-compiled vendored files: `library/Makefile`, `library/CMakeLists.txt`, `include/CMakeLists.txt`

| File | Lines | Bytes |
|---|---|---|
| `third_party/mbedtls/library/Makefile` | 410 | 10,761 |
| `third_party/mbedtls/library/CMakeLists.txt` | 379 | 12,756 |
| `third_party/mbedtls/include/CMakeLists.txt` | 22 | 742 |
| Total | **811** | **24,259** |

**Verdict: nothing in RiptOPL invokes any of them.** Evidence:

- The only production build definitions are generated by `tools/build_edition.py:114-115` (RA) and `tools/build_edition.py:137-138` (slim); both list `third_party/mbedtls/library/*.c` as ps2build `ee_lib` sources. ps2build does not read upstream Makefiles or CMakeLists from a source glob.
- A repo-wide search over `tools/`, `.github/` and `ps2.yaml` for `library/Makefile`, `library/CMakeLists`, `include/CMakeLists`, `add_subdirectory` on mbedTLS, or `MBEDTLS_PATH` returned no hits.
- `tools/syntax_sweep.py:22` uses mbedTLS only as `-I` include paths for `src/**/*.cpp` syntax checks; it compiles no `library/*.c` and invokes neither make nor cmake.
- The two test harnesses that do touch the library also bypass these files: `.github/scripts/test_ratls_host.py` (its `build()` function) compiles `sorted((MBED / "library").glob("*.c"))` directly with the host C compiler, and `.github/scripts/test_mbedtls_vendor.py:73` and `:86` assert the *glob itself* as the target's source list.
- Stronger: `library/Makefile` could not run here even if invoked — at `library/Makefile:13-22` it fatal-errors unless `../framework/exported.make` exists, and no `framework/` directory is vendored (directory listing of `third_party/mbedtls/` shows only `LICENSE`, `README.md`, `include/`, `library/` and the two RiptOPL config headers).

## 6. LIVE but never pulled from the static archive (client-only usage)

Consumer API surface (grep of both consumers, read directly): `src/platform/ratls.c` calls only the TLS-client set — `mbedtls_ssl_*` (init/config-defaults/client, `conf_authmode`, `conf_ca_chain`, `conf_rng`, `setup`, `set_hostname`, `set_bio`, `handshake`, `read`, `write`, `close_notify`, `get_verify_result`), `mbedtls_x509_crt_init/parse_der/free`, `mbedtls_platform_set_time`, and for randomness either `psa_crypto_init()` + `mbedtls_psa_get_random` under `MBEDTLS_USE_PSA_CRYPTO` (`src/platform/ratls.c:211-213`, `:257-258`) or `mbedtls_entropy_*` + `mbedtls_ctr_drbg_*` in the `#else` branch (`:219-223`, `:260`). `src/sources/ratls_ps2.cpp` *provides* `mbedtls_hardware_poll` and `mbedtls_ms_time`; it calls no library API. No other `mbedtls_*`/`psa_*` entry point is named by either consumer.

Against that surface, two LIVE files have no reachable caller. Both are **INFERRED** (grep-level call-graph proof plus standard static-archive pull semantics — an object is linked only if it resolves an undefined symbol; no PS2 link map was produced in this VM):

- **`hkdf.c` (RA only, 161 lines) — INFERRED unpulled.** The legacy `mbedtls_hkdf()` / `mbedtls_hkdf_extract()` / `mbedtls_hkdf_expand()` defined in `library/hkdf.c` have **zero callers** in any other `library/*.c` file (grep over the whole library). TLS 1.3's key schedule does not use them: `library/ssl_tls13_keys.c:180` calls `psa_key_derivation_setup(…, PSA_ALG_HKDF_EXPAND(…))`, i.e. the PSA implementation inside `psa_crypto.c`. Trap for the unwary: the *config define* `MBEDTLS_HKDF_C` must stay, because it is what derives `PSA_WANT_ALG_HKDF*` (`include/mbedtls/config_adjust_psa_from_legacy.h:121-130`); only the *file* is dead.
- **`mps_reader.c` (RA only, 538 lines) — INFERRED unpulled.** Guarded by `MBEDTLS_SSL_PROTO_TLS1_3` alone (`library/mps_reader.c:10`), so it compiles substantive code under RA — but a grep for `mps_reader` / `mbedtls_mps_` over all other `library/*.c` and `include/mbedtls/*.h` files returns nothing: the 3.6.7 TLS 1.3 stack in this tree does not consume the MPS reader at all (its companion `mps_trace.c` is EMPTY, §4). It is a standalone component the glob sweeps in.

Checked and **cleared** (LIVE and pulled — do not file): `psa_crypto_client.c` (its sole function `psa_reset_key_attributes`, `library/psa_crypto_client.c:16-19`, is called from `library/pk.c:196` and `library/psa_crypto.c:1436`); `psa_crypto_driver_wrappers_no_static.c` (its three non-static wrappers are called from `library/psa_crypto.c:1659`, `:2280` and `library/psa_crypto_slot_management.c:760`); `asn1write.c` (called from `library/ecdsa.c:609-613` and `library/rsa.c:302-337` — signature encoding, not certificate writing); `rsa_alt_helpers.c` (called from `library/rsa.c:784-1187`, private-key validation); `chacha20.c`/`poly1305.c`/`chachapoly.c` under RA (reachable: `MBEDTLS_TLS1_3_CHACHA20_POLY1305_SHA256` heads the TLS 1.3 preference list at `library/ssl_ciphersuites.c:44`, via `library/cipher_wrap.c:1893-1930`).

Unnumbered, INFERRED, subsystem-level (not TU-level): inside the pulled `psa_crypto.c` object (9,532 lines, RA), the PSA key-management surface (`psa_import_key`, `psa_generate_key`, key destruction/copy APIs) is compiled although no consumer and no TLS-internal path exercised by a client-only, ephemeral-key handshake needs most of it; consumers name only `psa_crypto_init`. Archive pull granularity is the whole object, so this cost is real only if the EE link does not also apply `--gc-sections` to it — not verifiable from this VM, hence unnumbered.

## 7. Other notes (unnumbered)

- Non-`.c` files in `library/`: 65 internal headers (21,112 lines) plus the two build files in §5. The headers are include-path cost only; several are themselves large dead weight in the same sense (e.g. `library/psa_crypto_driver_wrappers.h`, 107,973 bytes, is genuinely used by `psa_crypto.c`; `library/ssl_misc.h`, 124,999 bytes, is used by all SSL TUs) — no header-level finding is filed, per the task's include-path-cost framing.
- There are no other non-C, non-header files in `library/` (176 directory entries = 109 `.c` + 65 `.h` + `Makefile` + `CMakeLists.txt`).
- The EMPTY objects are not free of *all* cost even though they contribute no code: each still costs a compile invocation, an archive member, and (in `test_ratls_host.py`'s host build) a full host compile of 129,300 lines' worth of translation units per config. ELF size impact of the EMPTY set is expected to be zero (empty objects resolve no symbols) — INFERRED, no PS2 link was run.
- No config error was found in either RiptOPL header: every LIVE module the client path needs is present in both configs, and the slim header's stated removals match its actual EMPTY delta exactly (§4). The RA header's comment "required by TLS 1.3 in 3.6" for `MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` (`riptopl_mbedtls_config.h`, TLS section) is consistent with the TLS 1.3 files compiling LIVE under RA.

## 8. Candidate findings (unnumbered — CENSUS-n)

### CENSUS-1 — Both mbedTLS targets compile 55 translation units that are empty under every RiptOPL config (75 under the slim config)

- **Title:** Glob source list compiles 36,695 lines of config-empty mbedTLS TUs in every mbedTLS build (61,273 lines in updater builds)
- **Category:** Vendored dead-in-build (a) / size cost without use (c) — LESS-NOT-MORE (removing work, no code added)
- **Locations:** `tools/build_edition.py:114-115` (RA target), `tools/build_edition.py:137-138` (slim target); census in §3–§4 of this section (pin `2e347d5ac885cd0ff73e9ad4143781946396f492`)
- **Problem:** Both targets use `sources = ["third_party/mbedtls/library/*.c"]`. Preprocessor-verified: 55 of the 109 files (36,695 lines, 28.4% of the library's C lines) compile to empty objects under **both** RiptOPL configs — server TLS (`ssl_tls12_server.c` 4,311 + `ssl_tls13_server.c` 3,624 lines), unused ciphers/hashes (ARIA, Camellia, DES, MD5, SHA-1, SHA-3, RIPEMD160), X.509 writing/CRL/CSR, PEM, PKCS#5/7/12, DHM, ECJPAKE, LMS, PSA storage/SE/ITS, networking, timing, threading, debug and version modules. Under the slim config a further 20 files (24,578 lines) are also empty, for 75/109 files and 61,273 lines (47.4%). The vendored tree itself should stay byte-identical (it is a verified release archive); the waste is entirely in RiptOPL's own target definitions.
- **Fix:** Replace the glob in each of the two target definitions with that config's explicit LIVE+PARTIAL source list from §3 (RA: 54 files; slim: 34 files). Do not touch `third_party/mbedtls/`. The glob assertion in RiptOPL's own vendor test (`.github/scripts/test_mbedtls_vendor.py:73` and `:86`, which currently require `sources == ["third_party/mbedtls/library/*.c"]`) must be updated in the same change, or it will (correctly, by its current wording) fail.
- **Benefit:** 55 fewer compile invocations and archive members in every mbedTLS build, 75 in updater builds; proportional mbedTLS build-time saving. Expected ELF change: none (empty objects resolve no symbols) — the win is build time and archive size, stated plainly.
- **Effort:** S (two source lists + one test update; lists are derived mechanically from §3)
- **Risk:** Low. The lists are preprocessor-verified per config; the host test (`.github/scripts/test_ratls_host.py`) globs independently and still exercises the full tree, so a wrongly dropped LIVE file would still be caught there and by any TLS link error (a dropped LIVE file surfaces as an undefined symbol, never as silent behaviour change). Not PS2-build-verified in this VM.
- **Confidence:** High for the census (preprocessor pass + 15 spot-read files, §Method); the build-time benefit magnitude is INFERRED (no timed PS2 build available).

### CENSUS-2 — `hkdf.c` compiles under RA but its legacy API has no caller; only the `MBEDTLS_HKDF_C` define is load-bearing

- **Title:** RA build compiles `hkdf.c` (161 lines) whose `mbedtls_hkdf*()` functions nothing calls — TLS 1.3 uses PSA HKDF
- **Category:** Vendored live-but-unpulled (c)
- **Locations:** `third_party/mbedtls/library/hkdf.c:9` (guard), `third_party/mbedtls/riptopl_mbedtls_config.h` (`MBEDTLS_HKDF_C` in the ciphers/hashes section), `include/mbedtls/config_adjust_psa_from_legacy.h:121-130` (define ⇒ `PSA_WANT_ALG_HKDF*`), `third_party/mbedtls/library/ssl_tls13_keys.c:180` (actual HKDF use, via PSA)
- **Problem:** `hkdf.c` is LIVE under RA (109 surviving code lines) but grep over the whole library finds no caller of `mbedtls_hkdf`, `mbedtls_hkdf_extract` or `mbedtls_hkdf_expand` outside `hkdf.c` itself. The TLS 1.3 key schedule derives keys through the PSA key-derivation API inside `psa_crypto.c`. The file therefore compiles into the archive and can never be pulled by the client-only consumers (`src/platform/ratls.c`, `src/sources/ratls_ps2.cpp`).
- **Fix:** If CENSUS-1's explicit source lists are adopted, omit `hkdf.c` from the RA list — and **keep** `#define MBEDTLS_HKDF_C` in `riptopl_mbedtls_config.h`: removing the define instead would un-derive `PSA_WANT_ALG_HKDF*` and break TLS 1.3 key derivation. File-only removal, define untouched.
- **Benefit:** One fewer substantive TU compiled per RA build; removes a standing trap where the file's presence suggests the legacy HKDF API is available/used.
- **Effort:** S (one list entry, given CENSUS-1; otherwise not separately actionable under the glob)
- **Risk:** Low, provided the define is kept as specified. The failure mode of the *wrong* fix (dropping the define) is a TLS 1.3 handshake break, which the host test's TLS 1.3 cases would catch.
- **Confidence:** INFERRED — grep-level call-graph proof plus static-archive pull semantics; no PS2 link map produced in this VM.

### CENSUS-3 — `mps_reader.c` compiles under RA (538 lines) with no consumer anywhere in the tree

- **Title:** RA build compiles the MPS reader, a standalone component no other mbedTLS file references
- **Category:** Vendored live-but-unpulled (c)
- **Locations:** `third_party/mbedtls/library/mps_reader.c:10` (sole guard: `MBEDTLS_SSL_PROTO_TLS1_3`); absence of references verified by grep for `mps_reader` / `mbedtls_mps_` over all other `library/*.c` and `include/mbedtls/*.h` files (zero hits)
- **Problem:** Because its only guard is the TLS 1.3 protocol macro, `mps_reader.c` compiles substantive code (318 surviving code lines) in every RA build, yet the 3.6.7 TLS 1.3 implementation in this tree never calls it — it is an unintegrated component that the glob sweeps in, and its companion `mps_trace.c` is EMPTY in both configs (§4).
- **Fix:** If CENSUS-1's explicit source lists are adopted, omit `mps_reader.c` from the RA list. No config change.
- **Benefit:** 538 fewer lines compiled per RA build; no functional change (no caller exists to break).
- **Effort:** S (one list entry, given CENSUS-1)
- **Risk:** Low. A hidden caller would surface as an undefined symbol at link, and the host test links the full client path.
- **Confidence:** INFERRED — grep-level proof (whole-tree reference search) plus archive pull semantics; no PS2 link map produced in this VM.

### CENSUS-4 — Upstream `Makefile`/`CMakeLists.txt` files (811 lines) are never invoked by any RiptOPL flow, and the Makefile cannot run in this tree

- **Title:** Vendored mbedTLS build files are inert: no RiptOPL build, script or workflow reads them; `library/Makefile` fatal-errors without the un-vendored `framework/` directory
- **Category:** Vendored size cost without use (c) — weakest of this set; benefit is repository size only
- **Locations:** `third_party/mbedtls/library/Makefile` (410 lines; framework requirement at `:13-22`), `third_party/mbedtls/library/CMakeLists.txt` (379 lines), `third_party/mbedtls/include/CMakeLists.txt` (22 lines); negative evidence in §5
- **Problem:** 811 lines / 24,259 bytes of build system that nothing executes: production builds are ps2build targets generated by `tools/build_edition.py`, the syntax sweep uses include paths only, and both mbedTLS test harnesses compile the glob directly with a host compiler. `library/Makefile` additionally misleads: invoked, it stops with a fatal "framework/exported.make not found" error because the release archive's framework submodule content was (correctly) not vendored.
- **Fix:** Two honest options, in LESS-NOT-MORE order: (1) delete the three files — saves 24 KB and removes a misleading entry point, at the cost of diverging from the byte-for-byte release-archive fidelity the vendoring currently guarantees (coordinator-verified) and complicating any future re-vendor diff; (2) keep them for archive fidelity at zero build cost. This section recommends (2) unless repository size is independently valued — the finding is recorded so the non-use is proven, not assumed.
- **Benefit:** Option (1): 811 lines / 24,259 bytes removed. Option (2): none beyond documentation. No build-time or ELF benefit under either option.
- **Effort:** S either way
- **Risk:** Low for builds (nothing references the files — grep-verified in §5); the real cost of option (1) is to the re-vendoring workflow, which is a maintainer judgement call.
- **Confidence:** High on non-use (exhaustive reference search over `tools/`, `.github/`, `ps2.yaml`, plus the two test harnesses read in full); the keep-vs-delete choice is explicitly left to the maintainer.
