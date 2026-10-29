/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_notify.h"

#include <onion/notify.h>

#include <stdarg.h>

/* The bootstrapper only ever reports the boot verdict (mount/launch/payload
 * failures and a resolved crash), so these toasts bypass the boot silence
 * window: the daemon mutes its own startup noise, but a failed boot must still
 * say so. */
void bootstrap_notify(const char *text, ...) {
  va_list args;
  va_start(args, text);
  onion_notify_final_v(/*show_watermark=*/0, text, args);
  va_end(args);
}
