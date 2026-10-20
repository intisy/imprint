#ifndef IMPRINT_FIRSTBOOT_H
#define IMPRINT_FIRSTBOOT_H

#include "hook.h"

extern const char FIRSTBOOT_UNIT_NAME[];
extern const char FIRSTBOOT_BUNDLE_DIRECTORY[];

typedef struct firstboot_request {
    const char *target_root;
    const char *setup_name;
    const char *setup_json;
    const hook_list *hooks;
    const char *data_directory;
    const char *personal_directory;
} firstboot_request;

char *firstboot_unit_text(const char *setup_name);
char *firstboot_runner_text(const char *setup_name, const string_list *scripts);

/**
 * Writes the unit, its multi-user.target.wants link and the bundle (runner, resolved setup, a copy of the data
 * repo and of the personal folder) into the target root. A setup without firstboot hooks writes nothing and
 * leaves *installed false.
 */
bool firstboot_install(const firstboot_request *request, bool *installed, error_list *errors);
void firstboot_report(const char *setup_name, const char *target_root, bool installed, bool succeeded, FILE *output);

#endif
