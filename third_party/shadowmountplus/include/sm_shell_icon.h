#ifndef SM_SHELL_ICON_H
#define SM_SHELL_ICON_H

// Install and keep the HYL92 shelf tile current: metadata is restaged on
// every boot so tiles left by older installs converge to HYL92 branding.
void sm_shell_icon_install_if_missing(void);

#endif
