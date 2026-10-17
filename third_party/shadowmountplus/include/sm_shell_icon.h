#ifndef SM_SHELL_ICON_H
#define SM_SHELL_ICON_H

// Converge existing shelf tiles (NPXS40165 shortcut big app and legacy
// FAKE10101 copies) to the HYL92 name and skull icon. No tile is ever
// created; absent entries are skipped.
void sm_shell_icon_install_if_missing(void);

#endif
