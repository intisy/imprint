#ifndef IMPRINT_RESOLVE_H
#define IMPRINT_RESOLVE_H

#include "catalog.h"
#include "setup.h"

/** A window manager's config file: source relative to the data repo, target relative to the user's home. */
typedef struct dotfile {
    char *source;
    char *target;
} dotfile;

typedef struct dotfile_list {
    dotfile *items;
    size_t count;
    size_t capacity;
} dotfile_list;

typedef struct resolved_setup {
    char *name;
    char *distro;
    char *package_manager;
    char *bootloader;
    string_list window_managers;
    string_list apps;
    string_list packages;
    string_list aur;
    string_list flatpak;
    string_list repo_setup;
    string_list services;
    string_list user_services;
    string_list skipped;
    dotfile_list dotfiles;
    document_node *setup;
    /** What the person chose at install, such as `storage`; NULL until an install review fills it. */
    document_node *choices;
} resolved_setup;

void resolved_setup_init(resolved_setup *resolved);
void resolved_setup_free(resolved_setup *resolved);
bool setup_resolve(const setup *loaded, const catalog *available, resolved_setup *resolved, error_list *errors);
char *resolved_setup_to_json(const resolved_setup *resolved);
bool resolved_setup_from_json(const char *text, const char *file, resolved_setup *resolved, error_list *errors);

#endif
