#ifndef IMPRINT_STICK_INSTALLER_H
#define IMPRINT_STICK_INSTALLER_H

extern const char STICK_INSTALLER_NAME[];

/**
 * The install.sh a bundle carries: on the stick's live system it maps the Ventoy partition read-only, mounts it and
 * runs imprint-engine install on the setup, passing its own arguments on.
 */
char *stick_installer_text(const char *setup_name);

#endif
