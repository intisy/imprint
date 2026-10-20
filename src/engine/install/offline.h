#ifndef IMPRINT_OFFLINE_H
#define IMPRINT_OFFLINE_H

#include "command_runner.h"

/** What a stick carries for an install without internet; a NULL member is absent from the stick. */
typedef struct offline_stick {
    char *repository;
    char *aur_repository;
    char *flatpak_export;
    char *personal;
    bool flatpak_attached;
    bool repository_attached;
} offline_stick;

extern const char FLATPAK_EXPORT_INSIDE[];

/** Looks in <stick>/imprint/ first, then in ventoy-deploy's layout <stick>/ventoy-deploy/. */
void offline_stick_find(const char *stick_root, offline_stick *stick);
void offline_stick_free(offline_stick *stick);

/** The live system's pacman options with the stick's repository as the only source, for pacstrap. */
char *offline_pacman_conf_text(const char *host_conf_text, const char *repository);

/** @implNote archiso's pacman-init waits for time-sync.target, which never comes offline, so the keyring stays empty. */
bool offline_ensure_live_keyring(command_runner *runner, const char *gpg_directory, error_list *errors);

/** Puts the stick's repositories first in the target's pacman.conf and parks the mirrors while the stick serves. */
bool offline_attach(command_runner *runner, offline_stick *stick, const char *target_root, error_list *errors);

/**
 * @implNote pacman refuses every transaction while a configured repository has no database, and offline the mirror
 * repositories never get one, so an empty stand-in lets the stick's repositories serve.
 */
void offline_stand_in_missing_databases(command_runner *runner, const char *target_root, error_list *errors);

/** Removes every trace of the stick from the target: sections, databases, stand-ins, mounts, the parked mirrorlist. */
void offline_detach(command_runner *runner, const offline_stick *stick, const char *target_root, error_list *errors);

char *pacman_conf_insert_before_core(const char *conf, const char *section);
char *pacman_conf_remove_section(const char *conf, const char *name);

#endif
