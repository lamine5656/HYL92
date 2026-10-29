/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/log.h>
#include <onion/fs.h>
#include <onion/notify.h>
#include <onion/version.h>

#ifndef HYPERHEN_BUILD_ID
#define HYPERHEN_BUILD_ID "unknown"
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <LzmaLib.h>
#define LZMA_PROPS_SIZE 5
#define LZMA_CLI_HEADER_SIZE 13

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;
/* LZMA-compressed raw background (see daemon.c); decoded on demand below. */
extern uint8_t hyl92_bg_lzma_start[];
extern const unsigned int hyl92_bg_lzma_size;
extern uint8_t klogsrv_start[];
extern const unsigned int klogsrv_size;
extern uint8_t ftpsrv_start[];
extern const unsigned int ftpsrv_size;
extern uint8_t debug_elfldr_start[];
extern const unsigned int debug_elfldr_size;

#define ONION_ICON(name)                                                       \
  extern uint8_t name##_start[];                                               \
  extern const unsigned int name##_size;
#include "icon_manifest.inc"
#undef ONION_ICON

typedef struct EmbeddedIcon {
  const char *name;
  const uint8_t *data;
  const unsigned int *size;
} EmbeddedIcon;

static const EmbeddedIcon kEmbeddedIcons[] = {
#define ONION_ICON(name) {#name, name##_start, &name##_size},
#include "icon_manifest.inc"
#undef ONION_ICON
};

/*
 * Stock shell backgrounds replanted with the HYL92 gothic artwork. The
 * console stores these as 1920x1080 BGRA8888 DDS (8 294 548 bytes on disk
 * for every target below; bg_NPXS40053.dds is a different layout and must
 * stay untouched). The size gate below skips any store that does not match
 * byte-for-byte, so unknown/future formats are left alone automatically.
 * Targets are compared before writing so a converged console boots without
 * re-flashing the whole /system_ex tree on every start.
 */
static const char *const kShellBackgrounds[] = {
    "/system_ex/vsh_asset/bg_hub_default.dds",
    "/system_ex/vsh_asset/bg_image_load_error.dds",
    "/system_ex/vsh_asset/bg_NPXS40032.dds",
    "/system_ex/vsh_asset/bg_NPXS40037.dds",
    "/system_ex/vsh_asset/bg_NPXS40047.dds",
    "/system_ex/vsh_asset/bg_NPXS40054.dds",
    "/system_ex/vsh_asset/bg_NPXS40056.dds",
    "/system_ex/vsh_asset/bg_NPXS40063.dds",
    "/system_ex/vsh_asset/bg_NPXS40071.dds",
    "/system_ex/vsh_asset/bg_NPXS40075.dds",
    "/system_ex/vsh_asset/bg_NPXS40080.dds",
    "/system_ex/vsh_asset/bg_NPXS40110.dds",
    "/system_ex/vsh_asset/bg_NPXS40139.dds",
    "/system_ex/vsh_asset/bg_NPXS40144.dds",
    "/system_ex/vsh_asset/bg_NPXS40154.dds",
};

/* Stock layout: 148-byte DDS header + 1920x1080 BGRA8888 pixels. Kept as a
 * shared constant for the size gates below and the LZMA header validation. */
#define kExpectedBGSize (148 + (size_t)1920 * 1080 * 4)

/*
 * FreeBSD-style file flags (this kernel is a FreeBSD fork): the stock shell
 * assets under /system_ex are marked immutable, so unlink()/rename() over
 * them fail with EPERM and the replacement silently never lands — while
 * files we created ourselves (no flag) rewrite fine. Clear the flags on the
 * target before replacing it (SYS_chflags = 34).
 */
static void clear_shell_file_flags(const char *path) {
  if (syscall(SYS_chflags, path, 0UL) != 0)
    LOG_ERROR("assets: chflags %s failed (%s)", path, strerror(errno));
}

/* Whole-file compare so bootstraps skip redundant rewrites when the target
 * is already at the embedded content (size gate first, then streamed read).
 * The chunk buffer is static: the bootstrapper converges single-threaded. */
enum { kCompareChunkSize = 16384 };

static bool file_content_equals(const char *path, const void *data,
                                size_t size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  struct stat st;
  bool equal = false;
  if (fstat(fileno(f), &st) == 0 && st.st_size == (long long)size) {
    static uint8_t chunk[kCompareChunkSize];
    const uint8_t *cursor = (const uint8_t *)data;
    size_t done = 0;
    equal = true;
    while (done < size) {
      const size_t want = size - done > sizeof(chunk) ? sizeof(chunk)
                                                      : size - done;
      if (fread(chunk, 1, want, f) != want ||
          memcmp(chunk, cursor + done, want) != 0) {
        equal = false;
        break;
      }
      done += want;
    }
  }
  fclose(f);
  return equal;
}

/*
 * Sampled convergence check for the 8 MB background targets: 64 KB from the
 * head, middle and tail. The embedded artwork differs from stock in every
 * pixel block, so three matching samples prove the artwork is already on
 * disk — the full 8 MB compare and any write pipeline can be skipped on
 * every boot after the first convergence.
 */
enum { kBGSampleSize = 65536 };

static bool bg_samples_identical(const char *path, const uint8_t *embedded,
                                 size_t size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return false;
  static uint8_t sample[kBGSampleSize];
  const size_t sample_offsets[] = {
      0,
      (size - kBGSampleSize) / 2,
      size - kBGSampleSize,
  };
  bool all_match = true;
  for (size_t i = 0;
       all_match && i < sizeof(sample_offsets) / sizeof(sample_offsets[0]);
       ++i) {
    const size_t offset = sample_offsets[i];
    if (fseek(f, (long)offset, SEEK_SET) != 0 ||
        fread(sample, 1, kBGSampleSize, f) != kBGSampleSize ||
        memcmp(sample, embedded + offset, kBGSampleSize) != 0) {
      all_match = false;
    }
  }
  fclose(f);
  return all_match;
}

/*
 * Converge a shell file to embedded artwork: skip when the target already
 * holds the exact content, else clear immutable flags and replace through
 * the atomic temp+rename path. If the atomic write still fails (some mount
 * flavors refuse rename-over), rewrite the file in place as a fallback.
 * @known_different skips the full compare when a cheaper sampled check has
 * already detected a mismatch (backgrounds).
 */
static bool converge_file(const char *path, const void *data, size_t size,
                          bool known_different) {
  struct stat st;
  if (!known_different && stat(path, &st) == 0 && S_ISREG(st.st_mode) &&
      st.st_size == (long long)size &&
      file_content_equals(path, data, size)) {
    return true;
  }

  clear_shell_file_flags(path);
  if (write_file_atomic(path, data, size))
    return true;

  LOG_ERROR("assets: atomic replace failed, rewriting %s in place", path);
  FILE *f = fopen(path, "r+b");
  if (!f) {
    LOG_ERROR("assets: in-place rewrite of %s failed (%s)", path,
              strerror(errno));
    return false;
  }
  const size_t written = fwrite(data, 1, size, f);
  (void)fflush(f);
  (void)fsync(fileno(f));
  (void)fclose(f);
  if (written != size) {
    LOG_ERROR("assets: %s short write (%zu/%zu)", path, written, size);
    return false;
  }
  return true;
}

static bool write_file_if_different(const char *path, const void *data,
                                    size_t size) {
  return converge_file(path, data, size, false);
}

/*
 * Decode the LZMA-compressed raw background (xz -F lzma CLI header: 5 props
 * bytes + 8-byte LE uncompressed size). The decompression pattern mirrors the
 * unpacker's embedded bootstrapper handling.
 */
static bool hyl92_bg_decode(uint8_t **out, size_t *out_size) {
  *out = NULL;
  *out_size = 0;
  if (hyl92_bg_lzma_size <= LZMA_CLI_HEADER_SIZE)
    return false;

  uint64_t declared = 0;
  for (unsigned i = 0; i < 8; ++i) {
    declared |= (uint64_t)hyl92_bg_lzma_start[LZMA_PROPS_SIZE + i] << (8 * i);
  }
  /* The xz/lzma CLI headers carry the exact uncompressed size; some packers
   * emit the "unknown size" sentinel instead — accept both as long as the
   * result matches the fixed BGRA layout. Anything else is corruption. */
  const uint64_t kUnkownSize = 0xFFFFFFFFFFFFFFFFULL;
  if (declared != (uint64_t)kExpectedBGSize && declared != kUnkownSize) {
    return false;
  }

  uint8_t *buf = (uint8_t *)malloc(kExpectedBGSize);
  if (!buf)
    return false;

  size_t src_len = hyl92_bg_lzma_size - LZMA_CLI_HEADER_SIZE;
  size_t dest_len = kExpectedBGSize;
  if (LzmaUncompress(buf, &dest_len,
                     hyl92_bg_lzma_start + LZMA_CLI_HEADER_SIZE, &src_len,
                     hyl92_bg_lzma_start, LZMA_PROPS_SIZE) != SZ_OK ||
      dest_len != kExpectedBGSize) {
    LOG_ERROR("assets: background LZMA decode failed");
    free(buf);
    return false;
  }
  *out = buf;
  *out_size = dest_len;
  return true;
}

/* Once every BGRA DDS target has been converged we skip decoding the ~3.7 MB
 * compressed blob on later boots. The marker text pins the expected size and
 * the build id so a stale marker from another build can never short-circuit
 * differently-sized artwork. */
#define HYL92_BG_MARKER_PATH "/data/HyperHEN/.bg_converged"

static void bg_marker_text(char text[192], size_t expected) {
  snprintf(text, 192, "HYL92BG1 size=%zu build=%s\n", expected,
           HYPERHEN_BUILD_ID);
}

static bool bg_marker_valid(size_t expected) {
  char expected_text[192];
  bg_marker_text(expected_text, expected);
  FILE *f = fopen(HYL92_BG_MARKER_PATH, "rb");
  if (!f)
    return false;
  char stored[192] = {0};
  const size_t n = fread(stored, 1, sizeof(stored) - 1, f);
  fclose(f);
  const size_t expected_len = strlen(expected_text);
  return n == expected_len && memcmp(stored, expected_text, n) == 0;
}

static void bg_marker_write(size_t expected) {
  char text[192];
  bg_marker_text(text, expected);
  (void)write_file_if_different(HYL92_BG_MARKER_PATH, text, strlen(text));
}

static void bg_marker_clear(void) { (void)unlink(HYL92_BG_MARKER_PATH); }

static void converge_shell_backgrounds(void) {
  const size_t kExpectedSize = kExpectedBGSize;
  if ((size_t)hyl92_bg_lzma_size <= LZMA_CLI_HEADER_SIZE) {
    return;
  }

  /* Size gate first so untouched/unknown consoles skip the decode entirely. */
  bool any_candidate = false;
  for (size_t i = 0;
       !any_candidate &&
       i < sizeof(kShellBackgrounds) / sizeof(kShellBackgrounds[0]); ++i) {
    struct stat st;
    if (stat(kShellBackgrounds[i], &st) == 0 && S_ISREG(st.st_mode) &&
        st.st_size == (long long)kExpectedSize) {
      any_candidate = true;
    }
  }
  if (!any_candidate)
    return;
  if (bg_marker_valid(kExpectedSize)) {
    return;
  }

  uint8_t *decoded = NULL;
  size_t decoded_size = 0;
  if (!hyl92_bg_decode(&decoded, &decoded_size) ||
      decoded_size != kExpectedSize) {
    free(decoded);
    LOG_ERROR("assets: background decode unavailable, skipping reflash");
    bg_marker_clear();
    return;
  }

  bool all_converged = true;
  for (size_t i = 0;
       i < sizeof(kShellBackgrounds) / sizeof(kShellBackgrounds[0]); ++i) {
    struct stat st;
    const char *path = kShellBackgrounds[i];
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != (long long)kExpectedSize) {
      continue;
    }
    if (!bg_samples_identical(path, decoded, (size_t)kExpectedSize)) {
      if (!converge_file(path, decoded, (size_t)kExpectedSize, true)) {
        all_converged = false;
        LOG_ERROR("assets: background %s failed to converge", path);
      }
    }
  }

  if (all_converged) {
    bg_marker_write(kExpectedSize);
  } else {
    bg_marker_clear();
  }
  free(decoded);
}

bool bootstrap_assets_write(void) {
  mkdir("/data/HyperHEN", 0777);
  mkdir("/data/HyperHEN/assets", 0777);
  mkdir("/data/HyperHEN/tools", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/HyperHEN", 0777);

  /*
   * Boot fingerprint: FTP-visible proof of which build actually ran on the
   * console (per-commit id baked into the generated version header at build
   * time). Read /data/HyperHEN/hyl92_build.txt (or the vsh_asset mirror)
   * with the FTP tool to diagnose stale caches / failed injections.
   */
  char fingerprint[128];
  const int fp = snprintf(fingerprint, sizeof(fingerprint), "version=%s\nbuild=%s\n",
                          HYPERHEN_VERSION, HYPERHEN_BUILD_ID);
  if (fp > 0 && (size_t)fp < sizeof(fingerprint)) {
    (void)write_file_if_different("/data/HyperHEN/hyl92_build.txt", fingerprint,
                                  (size_t)fp);
    (void)write_file_if_different(
        "/system_ex/vsh_asset/hyl92_build.txt", fingerprint, (size_t)fp);
  }
  const bool startup_icon_ready = write_file_if_different(
      ONION_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/HyperHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_if_different(path, kEmbeddedIcons[i].data,
                                  *kEmbeddedIcons[i].size);
  }

  /* Fixed paths make the toolbox toggles independent of USB storage. */
  (void)write_file_if_different("/data/HyperHEN/tools/klogsrv.elf",
                                klogsrv_start, klogsrv_size);
  (void)write_file_if_different("/data/HyperHEN/tools/ftpsrv.elf",
                                ftpsrv_start, ftpsrv_size);
  (void)write_file_if_different("/data/HyperHEN/tools/hyperhen_debug_elfldr.elf",
                                debug_elfldr_start, debug_elfldr_size);

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_if_different("/system_ex/vsh_asset/hyperhen.png",
                                sicon_start, sicon_size);

  /*
   * etaHEN-style Settings icon swap: the NPXS40008 bundle is rebranded in
   * memory at decrypt time ("icon_setting" -> "hyl92seticon", done by
   * shellui/settings_bundle_patch.cpp), so the shell looks up a texture that
   * only exists once we plant it beside the stock ones. Both mirrors are
   * covered; a missing tree is skipped rather than created.
   */
  static const char *const kRnpsTextureDirs[] = {
      "/system_ex/rnps/apps/NPXS40008/assets/src/modules/categoriesList/"
      "assets/texture",
      "/mnt/rnps/apps/NPXS40008/assets/src/modules/categoriesList/assets/"
      "texture",
  };
  for (size_t i = 0; i < sizeof(kRnpsTextureDirs) / sizeof(kRnpsTextureDirs[0]);
       ++i) {
    struct stat st;
    if (stat(kRnpsTextureDirs[i], &st) != 0 || !S_ISDIR(st.st_mode)) {
      continue;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/hyl92seticon.png", kRnpsTextureDirs[i]);
    (void)write_file_if_different(path, sicon_start, sicon_size);

    /* Orphan left by earlier etaHEN installs in the same texture dir: our
     * bundle patch redirects "icon_setting" to hyl92seticon.png, so the old
     * etaHEN icon is never referenced again by the shell. */
    snprintf(path, sizeof(path), "%s/etahen_sicon.png", kRnpsTextureDirs[i]);
    unlink(path);
  }

  converge_shell_backgrounds();

  /*
   * Prefer the HYL92 skull on any stock system asset that already exists;
   * never create them, or a stock file becomes HYL92-branded out of nowhere.
   */
  static const char *const kRebrandIfPresent[] = {
      "/system_ex/vsh_asset/icon_setting.png",
  };
  for (size_t i = 0; i < sizeof(kRebrandIfPresent) / sizeof(kRebrandIfPresent[0]);
       ++i) {
    struct stat st;
    if (stat(kRebrandIfPresent[i], &st) == 0 && S_ISREG(st.st_mode)) {
      (void)write_file_if_different(kRebrandIfPresent[i], sicon_start,
                                    sicon_size);
    }
  }
  return startup_icon_ready;
}
