#include "firstboot.h"

#include "file_tree.h"
#include "platform.h"
#include "text.h"

#include <stdlib.h>
#include <string.h>

const char FIRSTBOOT_UNIT_NAME[] = "imprint-firstboot.service";
const char FIRSTBOOT_BUNDLE_DIRECTORY[] = "/var/lib/imprint/firstboot";

static const char UNIT_DIRECTORY[] = "etc/systemd/system";
static const char WANTS_DIRECTORY[] = "etc/systemd/system/multi-user.target.wants";

static const char UNIT_TEMPLATE[] =
    "[Unit]\n"
    "Description=imprint firstboot hooks of the setup %s\n"
    "Wants=network-online.target\n"
    "After=network-online.target\n"
    "ConditionPathExists=%s/run.sh\n"
    "\n"
    "[Service]\n"
    "Type=oneshot\n"
    "ExecStart=/bin/sh %s/run.sh\n"
    "StandardOutput=journal+console\n"
    "StandardError=journal+console\n"
    "TimeoutStartSec=infinity\n"
    "\n"
    "[Install]\n"
    "WantedBy=multi-user.target\n";

static const char RUNNER_HEAD_TEMPLATE[] =
    "#!/bin/sh\n"
    "# Written by imprint for the setup '%s': runs its firstboot hooks once, and all of them again on the next boot "
    "after a failure.\n"
    "set -u\n"
    "bundle=%s\n"
    "log=/var/log/imprint/firstboot.log\n"
    "unit=%s\n"
    "export IMPRINT_SETUP=\"$bundle/setup.json\"\n"
    "export IMPRINT_PERSONAL=\"$bundle/personal\"\n"
    "export IMPRINT_DATA=\"$bundle/data\"\n"
    "export IMPRINT_STAGE=firstboot\n"
    "mkdir -p /var/log/imprint\n"
    "\n"
    "say() {\n"
    "    echo \"imprint: $*\" | tee -a \"$log\"\n"
    "}\n"
    "\n"
    "run_hook() {\n"
    "    say \"firstboot hook $1 of $2: $3\"\n"
    "    status_file=$(mktemp)\n"
    "    { sh \"$IMPRINT_DATA/$3\"; echo \"$?\" >\"$status_file\"; } 2>&1 | tee -a \"$log\"\n"
    "    status=$(cat \"$status_file\")\n"
    "    rm -f \"$status_file\"\n"
    "    if [ \"$status\" -ne 0 ]; then\n"
    "        say \"$3 failed with exit code $status; the firstboot hooks run again on the next boot\"\n"
    "        exit \"$status\"\n"
    "    fi\n"
    "}\n"
    "\n";

static const char RUNNER_TAIL_TEMPLATE[] =
    "\n"
    "say \"every firstboot hook of setup '%s' succeeded; removing $unit\"\n"
    "rm -f \"/etc/systemd/system/multi-user.target.wants/$unit\" \"/etc/systemd/system/$unit\"\n"
    "rm -rf \"$bundle\"\n"
    "systemctl daemon-reload 2>/dev/null || true\n";

char *firstboot_unit_text(const char *setup_name) {
    return text_format(UNIT_TEMPLATE, setup_name, FIRSTBOOT_BUNDLE_DIRECTORY, FIRSTBOOT_BUNDLE_DIRECTORY);
}

static char *append(char *text, char *addition) {
    char *longer = text_format("%s%s", text, addition);
    free(text);
    free(addition);
    return longer;
}

char *firstboot_runner_text(const char *setup_name, const string_list *scripts) {
    char *text = text_format(RUNNER_HEAD_TEMPLATE, setup_name, FIRSTBOOT_BUNDLE_DIRECTORY, FIRSTBOOT_UNIT_NAME);
    for (size_t index = 0; index < scripts->count; index++) {
        text = append(text, text_format("run_hook %zu %zu '%s'\n", index + 1, scripts->count, scripts->items[index]));
    }
    return append(text, text_format(RUNNER_TAIL_TEMPLATE, setup_name));
}

static char *in_target(const char *target_root, const char *path) {
    return path_join(target_root, path[0] == '/' ? path + 1 : path);
}

static bool write_into(const char *directory, const char *name, const char *text, unsigned int mode, error_list *errors) {
    char *path = path_join(directory, name);
    bool written = file_write_text(path, text, errors) && platform_set_mode(path, mode, errors);
    free(path);
    return written;
}

static bool install_unit(const firstboot_request *request, error_list *errors) {
    char *unit_directory = in_target(request->target_root, UNIT_DIRECTORY);
    char *wants_directory = in_target(request->target_root, WANTS_DIRECTORY);
    char *unit = firstboot_unit_text(request->setup_name);
    char *link_path = path_join(wants_directory, FIRSTBOOT_UNIT_NAME);
    char *link_target = text_format("/%s/%s", UNIT_DIRECTORY, FIRSTBOOT_UNIT_NAME);
    bool installed = directory_make_all(unit_directory, errors) && directory_make_all(wants_directory, errors) &&
                     platform_make_symlink(link_target, link_path, errors) &&
                     write_into(unit_directory, FIRSTBOOT_UNIT_NAME, unit, 0644, errors);
    free(link_target);
    free(link_path);
    free(unit);
    free(wants_directory);
    free(unit_directory);
    return installed;
}

static bool make_private_directory(const char *path, error_list *errors) {
    return directory_make_all(path, errors) && platform_set_mode(path, 0700, errors);
}

static bool fill_personal(const char *bundle, const char *personal_directory, error_list *errors) {
    char *personal = path_join(bundle, "personal");
    bool filled = make_private_directory(personal, errors) &&
                  (personal_directory == NULL || file_tree_copy(personal_directory, personal, errors));
    free(personal);
    return filled;
}

static bool install_bundle(const firstboot_request *request, const string_list *scripts, error_list *errors) {
    char *bundle = in_target(request->target_root, FIRSTBOOT_BUNDLE_DIRECTORY);
    char *data = path_join(bundle, "data");
    char *runner = firstboot_runner_text(request->setup_name, scripts);
    bool installed = make_private_directory(bundle, errors) && write_into(bundle, "run.sh", runner, 0755, errors) &&
                     write_into(bundle, "setup.json", request->setup_json, 0600, errors) &&
                     file_tree_copy(request->data_directory, data, errors) &&
                     fill_personal(bundle, request->personal_directory, errors);
    free(runner);
    free(data);
    free(bundle);
    return installed;
}

static bool check_target(const firstboot_request *request, error_list *errors) {
    if (!platform_is_directory(request->target_root)) {
        error_push(errors, request->target_root, 0, "the target root is not a directory");
        return false;
    }
    if (request->personal_directory != NULL && !platform_is_directory(request->personal_directory)) {
        error_push(errors, request->personal_directory, 0, "the personal folder is not a directory");
        return false;
    }
    return true;
}

bool firstboot_install(const firstboot_request *request, bool *installed, error_list *errors) {
    *installed = false;
    string_list scripts;
    string_list_init(&scripts);
    hook_list_scripts(request->hooks, HOOK_STAGE_FIRSTBOOT, &scripts);
    bool valid = scripts.count == 0 ||
                 (check_target(request, errors) && hooks_check_scripts(&scripts, request->data_directory, errors) &&
                  install_unit(request, errors) && install_bundle(request, &scripts, errors));
    *installed = valid && scripts.count > 0;
    string_list_free(&scripts);
    return valid;
}

void firstboot_report(const char *setup_name, const char *target_root, bool installed, bool succeeded, FILE *output) {
    if (installed) {
        fprintf(output, "imprint: the firstboot hooks of setup '%s' run on the next boot of %s (%s)\n", setup_name,
                target_root, FIRSTBOOT_UNIT_NAME);
    } else if (succeeded) {
        fprintf(output, "imprint: setup '%s' has no firstboot hooks; nothing was written\n", setup_name);
    }
}
