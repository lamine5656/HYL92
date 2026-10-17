#include "sm_platform.h"

#include "sm_appdb.h"
#include "sm_filesystem.h"
#include "sm_install.h"
#include "sm_limits.h"
#include "sm_log.h"
#include "sm_paths.h"
#include "sm_shell_icon.h"

#define SM_SHELL_ICON_TITLE_ID "FAKE10101"
#define SM_SHELL_ICON_DIR APP_BASE "/" SM_SHELL_ICON_TITLE_ID
#define SM_SHELL_ICON_SCE_SYS_DIR SM_SHELL_ICON_DIR "/sce_sys"

extern unsigned char smp_icon_png[];
extern unsigned int smp_icon_png_len;
extern unsigned char assets_shell_icon_param_json[];
extern unsigned int assets_shell_icon_param_json_len;

static bool ensure_directory(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    if (S_ISDIR(st.st_mode))
      return true;
    errno = ENOTDIR;
    return false;
  }
  if (errno != ENOENT)
    return false;
  return mkdir(path, 0755) == 0;
}

static bool install_asset(const char *path, const unsigned char *data,
                          size_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    return false;

  size_t offset = 0;
  while (offset < size) {
    ssize_t written = write(fd, data + offset, size - offset);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (written == 0) {
      errno = EIO;
      break;
    }
    offset += (size_t)written;
  }

  int saved_errno = offset == size ? 0 : errno;
  if (close(fd) != 0 && saved_errno == 0)
    saved_errno = errno;
  if (saved_errno == 0)
    return true;

  (void)unlink(path);
  errno = saved_errno;
  return false;
}

void sm_shell_icon_install_if_missing(void) {
  /*
   * Stage the HYL92 tile metadata on every boot, not only on first install.
   * A tile left behind by an older automount keeps its old name and icon for
   * as long as the title directory exists, so converge it to the current
   * HYL92 branding every run.
   */
  if (!ensure_directory(SM_SHELL_ICON_DIR) ||
      !ensure_directory(SM_SHELL_ICON_SCE_SYS_DIR)) {
    log_debug("  [SHELLICON] failed to create app directory: %s",
              strerror(errno));
    return;
  }

  if (!install_asset(SM_SHELL_ICON_SCE_SYS_DIR "/icon0.png", smp_icon_png,
                     (size_t)smp_icon_png_len)) {
    log_debug("  [SHELLICON] failed to stage icon0.png: %s",
              strerror(errno));
    return;
  }
  if (!install_asset(SM_SHELL_ICON_SCE_SYS_DIR "/param.json",
                     assets_shell_icon_param_json,
                     (size_t)assets_shell_icon_param_json_len)) {
    log_debug("  [SHELLICON] failed to stage param.json: %s",
              strerror(errno));
    return;
  }

  /* Keep the registered appmeta mirrors in sync with the staged files; the
   * home shelf reads its name/icon from these copies. */
  bool appmeta_synced = true;
  if (ensure_directory("/user/appmeta/" SM_SHELL_ICON_TITLE_ID)) {
    static const char *const staged_names[] = {"icon0.png", "param.json"};
    for (size_t i = 0; i < sizeof(staged_names) / sizeof(staged_names[0]);
         ++i) {
      char src_path[MAX_PATH];
      char dst_path[MAX_PATH];
      snprintf(src_path, sizeof(src_path), SM_SHELL_ICON_SCE_SYS_DIR "/%s",
               staged_names[i]);
      snprintf(dst_path, sizeof(dst_path), "/user/appmeta/" SM_SHELL_ICON_TITLE_ID "/%s",
               staged_names[i]);
      if (copy_file(src_path, dst_path) != 0) {
        log_debug("  [SHELLICON] appmeta sync failed: %s -> %s", src_path,
                  dst_path);
        appmeta_synced = false;
      }
    }
  }

  if (is_installed(SM_SHELL_ICON_TITLE_ID)) {
    log_debug("  [SHELLICON] refreshed HYL92 tile: %s (appmeta=%s)",
              SM_SHELL_ICON_TITLE_ID, appmeta_synced ? "synced" : "stale");
    invalidate_app_db_title_cache();
    return;
  }

  int result = 0;
  if (!sm_install_register_title_dir(SM_SHELL_ICON_TITLE_ID, APP_BASE "/",
                                     &result)) {
    log_debug("  [SHELLICON] AppInstallTitleDir unavailable");
    return;
  }

  if (result != 0 && (uint32_t)result != 0x80990002u) {
    log_debug("  [SHELLICON] registration failed: code=0x%08X",
              (uint32_t)result);
    return;
  }

  invalidate_app_db_title_cache();
  log_debug("  [SHELLICON] installed: %s", SM_SHELL_ICON_TITLE_ID);
}
