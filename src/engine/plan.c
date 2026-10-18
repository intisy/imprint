#include "plan.h"

#include <stdlib.h>

static void print_step(FILE *output, int number, const char *label, const string_list *values) {
    char *joined = string_list_join(values, ", ");
    fprintf(output, "%d. %s (%zu)%s%s\n", number, label, values->count, values->count > 0 ? ": " : "", joined);
    free(joined);
}

void plan_print(const resolved_setup *resolved, FILE *output) {
    fprintf(output, "imprint-engine plan for setup '%s'\n", resolved->name);
    fprintf(output, "distro: %s, packages through %s\n", resolved->distro, resolved->package_manager);
    fprintf(output, "bootloader: %s\n", resolved->bootloader);
    char *window_managers = string_list_join(&resolved->window_managers, ", ");
    fprintf(output, "window managers: %s\n", window_managers[0] == '\0' ? "none" : window_managers);
    free(window_managers);
    fprintf(output, "1. repository setup (%zu commands)\n", resolved->repo_setup.count);
    print_step(output, 2, "packages", &resolved->packages);
    print_step(output, 3, "aur", &resolved->aur);
    print_step(output, 4, "flatpak", &resolved->flatpak);
    print_step(output, 5, "system services", &resolved->services);
    print_step(output, 6, "user services", &resolved->user_services);
    if (resolved->skipped.count > 0) {
        char *skipped = string_list_join(&resolved->skipped, ", ");
        fprintf(output, "skipped, no recipe for %s: %s\n", resolved->distro, skipped);
        free(skipped);
    }
    fputs("Nothing was installed: this engine only plans.\n", output);
}
