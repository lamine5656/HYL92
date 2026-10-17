/* Copyright (C) 2026 OnionHEN / LightningMods */

#include "bootstrap_notify.h"

#include <onion/notify.h>

#include <stdarg.h>

void bootstrap_notify(const char *text, ...) {
  va_list args;
  va_start(args, text);
  onion_notify_v(/*show_watermark=*/0, text, args);
  va_end(args);
}
