/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_assets.h"

#include <onion/fs.h>
#include <onion/notify.h>

#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern uint8_t sicon_start[];
extern const unsigned int sicon_size;
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
  }

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
      (void)write_file_atomic(kRebrandIfPresent[i], sicon_start, sicon_size);
    }
  }
  return startup_icon_ready;
}
