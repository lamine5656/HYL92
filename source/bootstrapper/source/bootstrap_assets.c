/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/log.h>
#include <onion/fs.h>
#include <onion/notify.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;
extern uint8_t hyl92_bg_start[];
extern const unsigned int hyl92_bg_size;
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

/* Replace a stock shell file with embedded artwork. If the atomic
 * temp+rename still fails after clearing flags (some mount flavors refuse
 * rename-over), rewrite the existing file in place as a fallback. */
static void replace_shell_file(const char *path, const void *data,
                               size_t size) {
  clear_shell_file_flags(path);
  if (write_file_atomic(path, data, size))
    return;
  LOG_ERROR("assets: atomic replace failed, rewriting %s in place", path);
  FILE *f = fopen(path, "r+b");
  if (!f) {
    LOG_ERROR("assets: in-place rewrite of %s failed (%s)", path,
              strerror(errno));
    return;
  }
  const size_t written = fwrite(data, 1, size, f);
  fflush(f);
  fclose(f);
  if (written != size)
    LOG_ERROR("assets: %s short write (%zu/%zu)", path, written, size);
}

static void converge_shell_backgrounds(void) {
  const size_t kExpectedSize = 148 + (size_t)1920 * 1080 * 4;
  if ((size_t)hyl92_bg_size != kExpectedSize) {
    return;
  }
  for (size_t i = 0;
       i < sizeof(kShellBackgrounds) / sizeof(kShellBackgrounds[0]); ++i) {
    struct stat st;
    const char *path = kShellBackgrounds[i];
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != (long long)hyl92_bg_size) {
      continue;
    }
    uint8_t *current = NULL;
    size_t current_size = 0;
    if (read_file_alloc(path, (size_t)hyl92_bg_size, &current, &current_size) &&
        current_size == (size_t)hyl92_bg_size &&
        memcmp(current, hyl92_bg_start, (size_t)hyl92_bg_size) == 0) {
      free(current);
      continue;
    }
    free(current);
    replace_shell_file(path, hyl92_bg_start, (size_t)hyl92_bg_size);
  }
}

bool bootstrap_assets_write(void) {
  mkdir("/data/HyperHEN", 0777);
  mkdir("/data/HyperHEN/assets", 0777);
  mkdir("/data/HyperHEN/tools", 0777);
  mkdir("/user/data", 0777);
  mkdir("/user/data/HyperHEN", 0777);
  const bool startup_icon_ready = write_file_atomic(
      ONION_NOTIFY_ICON_PATH, sicon_start, sicon_size);

  for (size_t i = 0; i < sizeof(kEmbeddedIcons) / sizeof(kEmbeddedIcons[0]);
       ++i) {
    char path[256];
    snprintf(path, sizeof(path), "/data/HyperHEN/assets/%s.png",
             kEmbeddedIcons[i].name);
    (void)write_file_atomic(path, kEmbeddedIcons[i].data,
                            *kEmbeddedIcons[i].size);
  }

  /* Fixed paths make the toolbox toggles independent of USB storage. */
  (void)write_file_atomic("/data/HyperHEN/tools/klogsrv.elf",
                          klogsrv_start, klogsrv_size);
  (void)write_file_atomic("/data/HyperHEN/tools/ftpsrv.elf",
                          ftpsrv_start, ftpsrv_size);
  (void)write_file_atomic("/data/HyperHEN/tools/hyperhen_debug_elfldr.elf",
                          debug_elfldr_start, debug_elfldr_size);

  mkdir("/system_ex/vsh_asset", 0777);
  (void)write_file_atomic("/system_ex/vsh_asset/hyperhen.png", sicon_start,
                          sicon_size);

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
    (void)write_file_atomic(path, sicon_start, sicon_size);

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
      replace_shell_file(kRebrandIfPresent[i], sicon_start, sicon_size);
    }
  }
  return startup_icon_ready;
}
