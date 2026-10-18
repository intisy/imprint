#ifndef IMPRINT_RESOLVE_H
#define IMPRINT_RESOLVE_H

#include "catalog.h"
#include "setup.h"

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
    document_node *setup;
} resolved_setup;

void resolved_setup_init(resolved_setup *resolved);
void resolved_setup_free(resolved_setup *resolved);
bool setup_resolve(const setup *loaded, const catalog *available, resolved_setup *resolved, error_list *errors);
char *resolved_setup_to_json(const resolved_setup *resolved);
bool resolved_setup_from_json(const char *text, const char *file, resolved_setup *resolved, error_list *errors);

#endif
