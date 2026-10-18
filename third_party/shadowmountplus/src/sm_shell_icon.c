#include "sm_platform.h"

#include "sm_limits.h"
#include "sm_log.h"
#include "sm_paths.h"
#include "sm_shell_icon.h"

#define SM_SHELL_ICON_TITLE_ID "FAKE10101"
#define SM_SHELL_ICON_SHORTCUT_TITLE_ID "NPXS40165"
#define SM_SHELL_ICON_FAKE_DIR APP_BASE "/" SM_SHELL_ICON_TITLE_ID
#define SM_SHELL_ICON_FAKE_SCE_SYS_DIR SM_SHELL_ICON_FAKE_DIR "/sce_sys"
#define SM_HYL92_TITLE "\xe2\x98\x85 HYL92 Tools"

extern unsigned char smp_icon_png[];
extern unsigned int smp_icon_png_len;
extern unsigned char assets_shell_icon_param_json[];
extern unsigned int assets_shell_icon_param_json_len;

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

static bool dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool path_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/*
 * The wrench tile in the game shelf is the shortcut big app NPXS40165 left
 * behind by an older HEN install. The shelf name and icon come straight from
 * /user/appmeta/<TID>/, so converge any existing copy to the HYL92 branding
 * every boot. Nothing is ever created: absent entries are skipped, so stray
 * files cannot appear on clean installs and no duplicate tile is added.
 */

/* Replace every "titleName": "<old>" string with the HYL92 title. The new
 * value is shorter than typical localized titles and contains no escapes, so
 * the JSON stays valid. Returns whether any value was rewritten. */
static bool patch_title_names(char *json, size_t capacity, size_t *len) {
  const char key[] = "\"titleName\"";
  const size_t key_len = sizeof(key) - 1;
  const char value[] = "\"" SM_HYL92_TITLE "\"";
  const size_t value_len = sizeof(value) - 1;
  bool changed = false;
  size_t offset = 0;

  while (offset < *len) {
    const char *hit =
        (const char *)memchr(json + offset, key[0], *len - offset);
    if (!hit)
      return changed;
    size_t pos = (size_t)(hit - json);
    if (pos + key_len > *len || memcmp(json + pos, key, key_len) != 0) {
      offset = pos + 1;
      continue;
    }

    size_t scan = pos + key_len;
    while (scan < *len &&
           (json[scan] == ' ' || json[scan] == '\t' || json[scan] == '\r' ||
            json[scan] == '\n' || json[scan] == ':')) {
      ++scan;
    }
    if (scan >= *len || json[scan] != '"') {
      offset = pos + key_len;
      continue;
    }
    ++scan; /* opening quote of the old value */
    size_t end = scan;
    while (end < *len && json[end] != '"') {
      if (json[end] == '\\' && end + 1 < *len) {
        ++end;
      }
      ++end;
    }
    if (end >= *len) {
      offset = pos + key_len;
      continue;
    }

    size_t head_len = scan; /* bytes before the old value, quote included */
    size_t tail_len = *len - (end + 1);
    if (head_len + value_len + tail_len + 1 > capacity) {
      offset = pos + key_len;
      continue;
    }
    memmove(json + head_len + value_len, json + end + 1, tail_len);
    memcpy(json + head_len, value, value_len);
    *len = head_len + value_len + tail_len;
    json[*len] = '\0';
    changed = true;
    offset = head_len + value_len;
  }

  return changed;
}

/* Variant of patch_title_names that only rewrites titleName values which
 * contain @needle. The settings entry keeps shelf-name variants like
 * "Debug Settings" / "★Debug Settings" on legacy installs; converge any such
 * appmeta copy to the HYL92 title without touching real game titles. */
static bool patch_title_names_containing(char *json, size_t capacity,
                                         size_t *len, const char *needle) {
  const char key[] = "\"titleName\"";
  const size_t key_len = sizeof(key) - 1;
  const char value[] = "\"" SM_HYL92_TITLE "\"";
  const size_t value_len = sizeof(value) - 1;
  const size_t needle_len = strlen(needle);
  bool changed = false;
  size_t offset = 0;

  while (offset < *len) {
    const char *hit =
        (const char *)memchr(json + offset, key[0], *len - offset);
    if (!hit)
      return changed;
    size_t pos = (size_t)(hit - json);
    if (pos + key_len > *len || memcmp(json + pos, key, key_len) != 0) {
      offset = pos + 1;
      continue;
    }

    size_t scan = pos + key_len;
    while (scan < *len &&
           (json[scan] == ' ' || json[scan] == '\t' || json[scan] == '\r' ||
            json[scan] == '\n' || json[scan] == ':')) {
      ++scan;
    }
    if (scan >= *len || json[scan] != '"') {
      offset = pos + key_len;
      continue;
    }
    ++scan; /* opening quote of the old value */
    size_t end = scan;
    while (end < *len && json[end] != '"') {
      if (json[end] == '\\' && end + 1 < *len) {
        ++end;
      }
      ++end;
    }
    if (end >= *len) {
      offset = pos + key_len;
      continue;
    }

    /* Only rewrite when the current title mentions the settings entry. */
    bool matches = false;
    if (end >= scan + needle_len) {
      for (size_t base = scan; base + needle_len <= end; ++base) {
        if (memcmp(json + base, needle, needle_len) == 0) {
          matches = true;
          break;
        }
      }
    }
    if (!matches) {
      offset = end + 1;
      continue;
    }

    size_t head_len = scan; /* bytes before the old value, quote included */
    size_t tail_len = *len - (end + 1);
    if (head_len + value_len + tail_len + 1 > capacity) {
      offset = pos + key_len;
      continue;
    }
    memmove(json + head_len + value_len, json + end + 1, tail_len);
    memcpy(json + head_len, value, value_len);
    *len = head_len + value_len + tail_len;
    json[*len] = '\0';
    changed = true;
    offset = head_len + value_len;
  }

  return changed;
}

/*
 * Any appmeta whose shelf title still mentions "Debug Settings" (leftover
 * debug-settings shortcut apps from older HEN installs) keeps surfacing the
 * old name next to the HYL92 tile. Converge every such existing copy —
 * title and, when present, its icon — to the HYL92 branding. Directories
 * without such a title (real games, the store, ...) are left untouched and
 * nothing new is ever created.
 */
static void converge_debug_settings_titles(void) {
  DIR *dir = opendir(APPMETA_BASE);
  if (!dir) {
    return;
  }

  char path[MAX_PATH];
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    struct stat st;
    snprintf(path, sizeof(path), APPMETA_BASE "/%s", entry->d_name);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
      continue;
    }

    snprintf(path, sizeof(path), APPMETA_BASE "/%s/param.json",
             entry->d_name);
    FILE *fp = fopen(path, "rb");
    if (!fp) {
      continue;
    }
    struct stat file_st;
    if (fstat(fileno(fp), &file_st) != 0 || file_st.st_size <= 0 ||
        file_st.st_size > 64 * 1024) {
      fclose(fp);
      continue;
    }
    size_t capacity = (size_t)file_st.st_size + 16;
    char *json = (char *)malloc(capacity);
    if (!json) {
      fclose(fp);
      continue;
    }
    size_t len = fread(json, 1, (size_t)file_st.st_size, fp);
    fclose(fp);
    if (len == 0) {
      free(json);
      continue;
    }
    json[len] = '\0';

    if (patch_title_names_containing(json, capacity, &len, "Debug Settings")) {
      if (install_asset(path, (const unsigned char *)json, len)) {
        log_debug("  [SHELLICON] %s param.json -> \"" SM_HYL92_TITLE "\"",
                  entry->d_name);
        snprintf(path, sizeof(path), APPMETA_BASE "/%s/icon0.png",
                 entry->d_name);
        if (path_exists(path)) {
          (void)install_asset(path, smp_icon_png, (size_t)smp_icon_png_len);
        }
      }
    }
    free(json);
  }
  closedir(dir);
}

static bool rebrand_appmeta_param(const char *title_id) {
  char path[MAX_PATH];
  snprintf(path, sizeof(path), APPMETA_BASE "/%s/param.json", title_id);

  FILE *fp = fopen(path, "rb");
  if (!fp)
    return false;
  struct stat st;
  if (fstat(fileno(fp), &st) != 0 || st.st_size <= 0 ||
      st.st_size > 64 * 1024) {
    fclose(fp);
    return false;
  }

  size_t capacity = (size_t)st.st_size + 64;
  char *json = (char *)malloc(capacity);
  if (!json) {
    fclose(fp);
    return false;
  }

  size_t len = fread(json, 1, (size_t)st.st_size, fp);
  fclose(fp);
  if (len == 0) {
    free(json);
    return false;
  }

  if (!patch_title_names(json, capacity, &len)) {
    free(json);
    log_debug("  [SHELLICON] %s param.json has no titleName to rebrand",
              title_id);
    return true;
  }

  if (!install_asset(path, (const unsigned char *)json, len)) {
    free(json);
    log_debug("  [SHELLICON] %s param.json rebrand failed: %s", title_id,
              strerror(errno));
    return false;
  }
  free(json);
  log_debug("  [SHELLICON] %s param.json rebranded to \"" SM_HYL92_TITLE "\"",
            title_id);
  return true;
}

static void converge_appmeta_tiles(const char *title_id) {
  char dir[MAX_PATH];
  char path[MAX_PATH];
  snprintf(dir, sizeof(dir), APPMETA_BASE "/%s", title_id);
  if (!dir_exists(dir)) {
    return;
  }

  snprintf(path, sizeof(path), "%s/icon0.png", dir);
  if (path_exists(path) &&
      install_asset(path, smp_icon_png, (size_t)smp_icon_png_len)) {
    log_debug("  [SHELLICON] %s icon0.png -> \"" SM_HYL92_TITLE "\"", title_id);
  }

  /* Legacy FAKE10101 tiles stage the whole param copy from our asset; the
   * real shortcut big app keeps its own JSON and gets an in-place rebrand. */
  if (strcmp(title_id, SM_SHELL_ICON_TITLE_ID) == 0) {
    snprintf(path, sizeof(path), "%s/param.json", dir);
    install_asset(path, assets_shell_icon_param_json,
                  (size_t)assets_shell_icon_param_json_len);
  } else {
    (void)rebrand_appmeta_param(title_id);
  }
}

void sm_shell_icon_install_if_missing(void) {
  converge_appmeta_tiles(SM_SHELL_ICON_SHORTCUT_TITLE_ID);
  /* Older installs sometimes named the fake tile FAKE10101; rebrand that
   * copy too whenever it exists, but never stage a fresh one. */
  if (dir_exists(SM_SHELL_ICON_FAKE_DIR) ||
      dir_exists(APPMETA_BASE "/" SM_SHELL_ICON_TITLE_ID)) {
    converge_appmeta_tiles(SM_SHELL_ICON_TITLE_ID);
  }

  /* Settings-name leftovers ("★Debug Settings" shelf cards from older HEN
   * installs): converge every appmeta whose title still mentions
   * "Debug Settings" to the HYL92 name. */
  converge_debug_settings_titles();
}
