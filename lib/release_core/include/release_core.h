#ifndef RELEASE_CORE_H
#define RELEASE_CORE_H

/*
 * release_core — release identity shared by release_pack and the panel.
 *
 * The contract is doc/firmware_release_whitepaper.md §1.  This module has no
 * filesystem, no network, no clock and no git.  SHA-256 here is the one the
 * panel calls; there is no second implementation.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RC_SCHEMA              1
#define RC_TARGET_FLY          "fly_micro4"
#define RC_TARGET_PANEL        "esp32gui_s3"
#define RC_LOAD_ADDRESS_FLY    268451840u   /* 0x10004000 */
#define RC_FLASH_SIZE_PANEL    16777216u    /* 16 MiB */
#define RC_MCU_FLY             "rp2040"
#define RC_CHIP_PANEL          "esp32s3"
#define RC_TAG_MAX             16

/* ------------------------------------------------------------------ */
/*  SHA-256                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    uint32_t buflen;
} rc_sha256_ctx_t;

void rc_sha256_init(rc_sha256_ctx_t *ctx);
void rc_sha256_update(rc_sha256_ctx_t *ctx, const void *data, size_t len);
void rc_sha256_final(rc_sha256_ctx_t *ctx, uint8_t out[32]);
int  rc_sha256_hex(const void *data, size_t len, char out[65]);
int  rc_hex_is_sha256(const char *s);

/* ------------------------------------------------------------------ */
/*  Version and build id                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} rc_version_t;

/* "vMAJOR.MINOR.PATCH": each component a decimal 0…255, no leading zero.
 * A lightweight name with a suffix, an uppercase V, or a missing component
 * is rejected. */
int rc_semver_parse(const char *tag, rc_version_t *out);
int rc_semver_format(const rc_version_t *v, char out[16]);

/* Build id rules: 0 is never a release.  previous 0 means no published
 * release and accepts any non-zero id; otherwise the new id must be
 * strictly greater. */
int rc_build_id_accept(uint32_t new_id, uint32_t previous_id);

/* ------------------------------------------------------------------ */
/*  Time and filename                                                  */
/* ------------------------------------------------------------------ */

/* Epoch seconds to "YYYYMMDDTHHMMSSZ" (16 chars) and
 * "YYYY-MM-DDTHH:MM:SSZ" (21 chars). */
int rc_utc_compact(uint32_t epoch_s, char out[17]);
int rc_utc_iso(uint32_t epoch_s, char out[21]);

/* <target>_<utc>_v<semver>_<git8>_<kind>[_dirty].bin */
int rc_make_filename(const char *target, const char *kind, uint32_t commit_time,
                     const rc_version_t *version, const char *git, int dirty,
                     char *out, size_t cap);

/* ------------------------------------------------------------------ */
/*  Manifest                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    int      schema;
    char     target[16];
    char     kind[8];
    rc_version_t version;
    uint32_t build;
    char     utc[24];           /* YYYY-MM-DDTHH:MM:SSZ */
    char     git[41];
    int      dirty;
    char     file[128];
    uint32_t size;
    char     sha256[65];
    uint64_t load_address;      /* fly */
    char     mcu[16];           /* fly */
    char     katapult_min[16];  /* fly */
    char     chip[16];          /* panel */
    uint64_t flash_size;        /* panel */
    char     partition_table_sha256[65]; /* panel */
    char     slot[8];           /* panel */
    int      rollback;          /* panel */
} rc_manifest_t;

/* Fill the identity fields that are not read from the image. */
void rc_manifest_init(rc_manifest_t *m, const char *target, const char *kind,
                      const rc_version_t *version, uint32_t build,
                      uint32_t commit_time, const char *git, int dirty,
                      const char *file, uint32_t size, const char *sha256);

/* Canonical emitter: schema 1, fixed key order, no whitespace outside a
 * string.  Returns 0 or -1 when the buffer is too small. */
int rc_manifest_emit(const rc_manifest_t *m, char *out, size_t cap);

/* Parse and validate the identity fields a consumer checks before a write.
 * Rejects a missing/short sha256, an unknown target, dirty: true, a wrong
 * load address on fly_micro4, a wrong flash size on esp32gui_s3, a factory
 * kind on fly_micro4, and an unsupported schema. */
int rc_manifest_parse(const char *text, rc_manifest_t *m);

/* Selectors.  Both reject kind: factory and a dirty image. */
int rc_manifest_ota_ok(const rc_manifest_t *m);
int rc_manifest_katapult_ok(const rc_manifest_t *m);

/* Highest valid build for one selector.  Returns 1 with *out set, 0 when
 * there is no candidate, -1 when two manifests claim the same build with
 * different SHA-256 values (a conflict; neither is returned). */
int rc_select_highest(const rc_manifest_t *const *list, int count,
                      int panel_ota, const rc_manifest_t **out);

/* ------------------------------------------------------------------ */
/*  release.json                                                       */
/* ------------------------------------------------------------------ */

#define RC_MAX_RELEASE_ASSETS 12

typedef struct {
    int present;
    rc_version_t version;
    uint32_t build;
    char assets[RC_MAX_RELEASE_ASSETS][128];
    int asset_count;
} rc_release_target_t;

typedef struct {
    int schema;
    char tag[RC_TAG_MAX];
    char published[24];
    rc_release_target_t fly;
    rc_release_target_t panel;
} rc_release_t;

void rc_release_init(rc_release_t *r, const char *tag, uint32_t published_time);
int rc_release_add_asset(rc_release_t *r, const char *target, const char *name);
/* Both targets present with one version and one build; every asset name
 * belongs to its target and a Katapult UF2 is not an asset. */
int rc_release_validate(const rc_release_t *r);
int rc_release_emit(const rc_release_t *r, char *out, size_t cap);
int rc_release_parse(const char *text, rc_release_t *r);

#ifdef __cplusplus
}
#endif

#endif /* RELEASE_CORE_H */
