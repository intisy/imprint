#include "base_system.h"
#include "disks.h"
#include "fake_runner.h"
#include "file_tree.h"
#include "install_options.h"
#include "layout.h"
#include "offline.h"
#include "platform.h"
#include "software.h"
#include "utest.h"

#include <stdlib.h>
#include <string.h>

#define INSTALL_FIXTURES IMPRINT_TEST_FIXTURES "/install"
#define WORK IMPRINT_TEST_WORK

static char *read_fixture(const char *path) {
    error_list errors;
    error_list_init(&errors);
    char *text = NULL;
    file_read_all(path, &text, &errors);
    error_list_free(&errors);
    return text;
}

static bool load_disks(disk_list *disks) {
    char *json = read_fixture(IMPRINT_TEST_FIXTURES "/lsblk/p53.json");
    error_list errors;
    error_list_init(&errors);
    disk_list_init(disks);
    bool parsed = json != NULL && disks_parse_lsblk(json, disks, &errors);
    error_list_free(&errors);
    free(json);
    return parsed;
}

static bool load_options(resolved_setup *resolved, install_options *options) {
    char *json = read_fixture(INSTALL_FIXTURES "/workstation.json");
    error_list errors;
    error_list_init(&errors);
    bool loaded = json != NULL && resolved_setup_from_json(json, "workstation.json", resolved, &errors) &&
                  install_options_read(resolved, options, &errors);
    error_list_print(&errors, stderr);
    error_list_free(&errors);
    free(json);
    install_options_replace(&options->disk, "/dev/nvme0n1");
    return loaded;
}

static char *fresh_directory(const char *name) {
    char *path = path_join(WORK, name);
    error_list errors;
    error_list_init(&errors);
    directory_make_all(path, &errors);
    error_list_free(&errors);
    return path;
}

static void write_file(const char *directory, const char *inside, const char *text) {
    char *path = path_join(directory, inside);
    char *parent = text_duplicate(path);
    *strrchr(parent, '/') = '\0';
    error_list errors;
    error_list_init(&errors);
    directory_make_all(parent, &errors);
    file_write_text(path, text, &errors);
    error_list_free(&errors);
    free(parent);
    free(path);
}

UTEST(install_disks, the_default_target_is_the_internal_nvme_never_the_stick) {
    disk_list disks;
    ASSERT_TRUE(load_disks(&disks));
    ASSERT_EQ((size_t)4, disks.count);
    const disk *chosen = disks_default_target(&disks, "/dev/sda");
    ASSERT_TRUE(chosen != NULL);
    ASSERT_STREQ("/dev/nvme0n1", chosen->path);
    ASSERT_STREQ("Vansuny", disks_find(&disks, "/dev/sda")->model);
    disk_list_free(&disks);
}

UTEST(install_disks, the_guards_refuse_the_stick_removable_and_unknown_disks) {
    disk_list disks;
    ASSERT_TRUE(load_disks(&disks));
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(disk_check_target(&disks, "/dev/sda", "/dev/sda", &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "is the stick"));
    ASSERT_FALSE(disk_check_target(&disks, "/dev/sda", NULL, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "mounted"));
    ASSERT_FALSE(disk_check_target(&disks, "/dev/mmcblk0", NULL, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "removable"));
    ASSERT_FALSE(disk_check_target(&disks, "/dev/sdz", NULL, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "not a disk this computer lists"));
    ASSERT_TRUE(disk_check_target(&disks, "/dev/sdb", "/dev/sda", &errors));
    ASSERT_TRUE(disk_check_target(&disks, "/dev/nvme0n1", "/dev/sda", &errors));
    error_list_free(&errors);
    disk_list_free(&disks);
}

UTEST(install_disks, a_used_disk_holds_something_and_a_changed_serial_is_another_disk) {
    disk_list disks;
    ASSERT_TRUE(load_disks(&disks));
    const disk *used = disks_find(&disks, "/dev/sdb");
    ASSERT_TRUE(disk_holds_anything(used));
    ASSERT_TRUE(disk_holds_label(used, "data"));
    ASSERT_FALSE(disk_holds_anything(disks_find(&disks, "/dev/nvme0n1")));
    disk swapped = disks.items[3];
    swapped.serial = "OTHER";
    ASSERT_FALSE(disk_same_identity(disks_find(&disks, "/dev/nvme0n1"), &swapped));
    ASSERT_TRUE(disk_same_identity(disks_find(&disks, "/dev/nvme0n1"), disks_find(&disks, "/dev/nvme0n1")));
    disk_list_free(&disks);
}

UTEST(install_disks, an_empty_list_gives_no_default) {
    disk_list disks;
    disk_list_init(&disks);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(disks_parse_lsblk("{\"blockdevices\": []}", &disks, &errors));
    ASSERT_TRUE(disks_default_target(&disks, NULL) == NULL);
    ASSERT_FALSE(disks_parse_lsblk("not json", &disks, &errors));
    error_list_free(&errors);
    disk_list_free(&disks);
}

UTEST(install_disks, the_stick_disk_is_found_through_device_mapper) {
    command_runner runner = fake_runner_create(stderr);
    fake_runner_answer(&runner, "findmnt", 0, "/dev/mapper/stick\n");
    fake_runner_answer(&runner, "lsblk -n -r -s", 0, "stick dm\nsda1 part\nsda disk\n");
    error_list errors;
    error_list_init(&errors);
    char *found = disks_holding_directory(&runner, "/run/stick", &errors);
    ASSERT_STREQ("/dev/sda", found);
    free(found);
    error_list_free(&errors);
    fake_runner_free(&runner);
}

UTEST(install_layout, partition_paths_follow_the_kernel_naming) {
    char *paths[] = { partition_path("/dev/nvme0n1", 1), partition_path("/dev/sda", 2), partition_path("/dev/loop0", 3),
                      partition_path("/dev/mmcblk0", 4) };
    ASSERT_STREQ("/dev/nvme0n1p1", paths[0]);
    ASSERT_STREQ("/dev/sda2", paths[1]);
    ASSERT_STREQ("/dev/loop0p3", paths[2]);
    ASSERT_STREQ("/dev/mmcblk0p4", paths[3]);
    for (size_t index = 0; index < COUNT_OF(paths); index++) {
        free(paths[index]);
    }
}

UTEST(install_layout, the_workstation_options_come_from_the_setup) {
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    ASSERT_EQ(1024LL, options.esp_size_mb);
    ASSERT_EQ(16384LL, options.swap_size_mb);
    ASSERT_EQ(262144LL, options.root_size_mb);
    ASSERT_STREQ("data", options.data_label);
    ASSERT_STREQ("/data", options.data_mount);
    ASSERT_STREQ("finn", options.username);
    ASSERT_STREQ("lightdm", options.display_manager);
    ASSERT_STREQ("de", options.keyboard_layout);
    ASSERT_TRUE(options.password == NULL);
    install_options_set_data_partition(&options, false);
    ASSERT_TRUE(options.data_label == NULL);
    ASSERT_EQ(0LL, options.root_size_mb);
    install_options_set_data_partition(&options, true);
    ASSERT_EQ(262144LL, options.root_size_mb);
    install_options_free(&options);
    resolved_setup_free(&resolved);
}

UTEST(install_layout, a_password_is_expired_only_without_a_display_manager) {
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    ASSERT_TRUE(options.expire_password);
    ASSERT_FALSE(install_options_should_expire_password(&options));
    install_options_replace(&options.display_manager, NULL);
    ASSERT_TRUE(install_options_should_expire_password(&options));
    install_options_free(&options);
    resolved_setup_free(&resolved);
}

UTEST(install_layout, partitioning_writes_four_partitions_with_a_data_partition) {
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    command_runner runner = fake_runner_create(stderr);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(layout_partition(&runner, &options, &errors));
    ASSERT_TRUE(fake_runner_ran(&runner, "sgdisk -Z /dev/nvme0n1"));
    ASSERT_TRUE(fake_runner_ran(&runner, "sgdisk -n 1:0:+1024M -t 1:ef00 -c 1:ESP -n 2:0:+16384M -t 2:8200 -c 2:swap "
                                         "-n 3:0:+262144M -t 3:8300 -c 3:root -n 4:0:0 -t 4:8300 -c 4:data /dev/nvme0n1"));
    install_options_set_data_partition(&options, false);
    ASSERT_TRUE(layout_partition(&runner, &options, &errors));
    ASSERT_TRUE(fake_runner_ran(&runner, "sgdisk -n 1:0:+1024M -t 1:ef00 -c 1:ESP -n 2:0:+16384M -t 2:8200 -c 2:swap "
                                         "-n 3:0:0 -t 3:8300 -c 3:root /dev/nvme0n1"));
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
}

UTEST(install_layout, a_failed_sgdisk_stops_the_layout) {
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    command_runner runner = fake_runner_create(stderr);
    fake_runner_answer(&runner, "sgdisk -n", 4, "");
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(layout_partition(&runner, &options, &errors));
    ASSERT_TRUE(error_list_mentions(&errors, "partitioning failed"));
    ASSERT_EQ(-1L, fake_runner_index(&runner, "partprobe"));
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
}

UTEST(install_layout, btrfs_formats_with_f_and_mounts_five_subvolumes_in_order) {
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    options.root_fs = ROOT_BTRFS;
    command_runner runner = fake_runner_create(stderr);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(layout_format(&runner, &options, &errors));
    ASSERT_TRUE(fake_runner_ran(&runner, "mkfs.btrfs -f -L root /dev/nvme0n1p3"));
    ASSERT_TRUE(fake_runner_ran(&runner, "mkfs.ext4 -F -L data /dev/nvme0n1p4"));
    ASSERT_TRUE(layout_mount(&runner, &options, "/mnt", &errors));
    long root = fake_runner_index(&runner, "mount -o subvol=@,noatime,compress=zstd /dev/nvme0n1p3 /mnt");
    long home = fake_runner_index(&runner, "mount -o subvol=@home,noatime,compress=zstd /dev/nvme0n1p3 /mnt/home");
    long package_cache = fake_runner_index(&runner, "mount -o subvol=@pkg,");
    long boot = fake_runner_index(&runner, "mount /dev/nvme0n1p1 /mnt/boot");
    ASSERT_TRUE(root >= 0 && home > root && package_cache > home && boot > package_cache);
    ASSERT_TRUE(fake_runner_ran(&runner, "mount /dev/nvme0n1p4 /mnt/data"));
    ASSERT_TRUE(fake_runner_ran(&runner, "swapon /dev/nvme0n1p2"));
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
}

UTEST(install_offline, both_stick_layouts_are_found_and_imprint_wins) {
    char *stick = fresh_directory("stick-both");
    write_file(stick, "ventoy-deploy/offline/offline.db", "db");
    write_file(stick, "ventoy-deploy/offline-aur/offline-aur.db", "db");
    write_file(stick, "ventoy-deploy/offline-flatpak/.ostree/repo/config", "x");
    write_file(stick, "ventoy-deploy/personal/tailscale-authkey", "key");
    write_file(stick, "imprint/offline/offline.db", "db");
    offline_stick found;
    offline_stick_find(stick, &found);
    char *imprint_repository = path_join(stick, "imprint/offline");
    char *aur = path_join(stick, "ventoy-deploy/offline-aur");
    char *flatpak = path_join(stick, "ventoy-deploy/offline-flatpak");
    char *personal = path_join(stick, "ventoy-deploy/personal");
    ASSERT_STREQ(imprint_repository, found.repository);
    ASSERT_STREQ(aur, found.aur_repository);
    ASSERT_STREQ(flatpak, found.flatpak_export);
    ASSERT_STREQ(personal, found.personal);
    offline_stick_free(&found);
    free(imprint_repository);
    free(aur);
    free(flatpak);
    free(personal);
    free(stick);
}

UTEST(install_offline, a_stick_without_repositories_finds_nothing_and_attaches_nothing) {
    char *stick = fresh_directory("stick-empty");
    offline_stick found;
    offline_stick_find(stick, &found);
    ASSERT_TRUE(found.repository == NULL && found.aur_repository == NULL && found.flatpak_export == NULL);
    command_runner runner = fake_runner_create(stderr);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(offline_attach(&runner, &found, "/mnt", &errors));
    ASSERT_EQ((size_t)0, fake_runner_commands(&runner)->count);
    error_list_free(&errors);
    fake_runner_free(&runner);
    offline_stick_free(&found);
    free(stick);
}

UTEST(install_offline, the_pacstrap_conf_keeps_the_options_and_uses_only_the_stick) {
    char *conf = offline_pacman_conf_text("[options]\nArchitecture = auto\n\n[core]\nInclude = x\n\n[extra]\nInclude = x\n",
                                          "/run/stick/ventoy-deploy/offline");
    ASSERT_STREQ("[options]\nArchitecture = auto\n\n\n[offline]\nServer = file:///run/stick/ventoy-deploy/offline\n", conf);
    free(conf);
}

UTEST(install_offline, attach_then_detach_leaves_the_target_as_it_was) {
    char *target = fresh_directory("offline-target");
    const char *conf = "[options]\nHoldPkg = pacman\n\n[core]\nInclude = /etc/pacman.d/mirrorlist\n\n[extra]\nInclude = "
                       "/etc/pacman.d/mirrorlist\n";
    const char *mirrors = "Server = https://mirror.example/$repo/os/$arch\n";
    write_file(target, "etc/pacman.conf", conf);
    write_file(target, "etc/pacman.d/mirrorlist", mirrors);
    write_file(target, "var/lib/pacman/sync/core.db", "real");
    offline_stick stick;
    memset(&stick, 0, sizeof stick);
    stick.repository = text_duplicate("/stick/offline");
    stick.aur_repository = text_duplicate("/stick/offline-aur");
    command_runner runner = fake_runner_create(stderr);
    fake_runner_answer(&runner, "arch-chroot", 0, "offline\noffline-aur\ncore\nextra\n");
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(offline_attach(&runner, &stick, target, &errors));
    char *conf_path = path_join(target, "etc/pacman.conf");
    char *attached = read_fixture(conf_path);
    ASSERT_TRUE(strstr(attached, "[offline]\nServer = file:///var/lib/imprint-offline\n\n[offline-aur]\nSigLevel = "
                                 "Optional TrustAll\nServer = file:///var/lib/imprint-offline-aur\n\n[core]") != NULL);
    offline_stand_in_missing_databases(&runner, target, &errors);
    ASSERT_EQ((size_t)3, fake_runner_count(&runner, "tar -czf"));
    offline_detach(&runner, &stick, target, &errors);
    char *detached = read_fixture(conf_path);
    char *mirror_path = path_join(target, "etc/pacman.d/mirrorlist");
    char *restored = read_fixture(mirror_path);
    ASSERT_STREQ(conf, detached);
    ASSERT_STREQ(mirrors, restored);
    char *stand_ins = path_join(target, "var/lib/pacman/sync/.imprint-stand-ins");
    ASSERT_FALSE(platform_is_file(stand_ins));
    free(stand_ins);
    free(attached);
    free(detached);
    free(restored);
    free(conf_path);
    free(mirror_path);
    error_list_free(&errors);
    fake_runner_free(&runner);
    offline_stick_free(&stick);
    free(target);
}

UTEST(install_base, the_password_never_reaches_a_command_line_and_its_file_is_removed) {
    char *target = fresh_directory("password-target");
    command_runner runner = fake_runner_create(stderr);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(base_set_password(&runner, target, "finn", "s3cret-word", false, &errors));
    const string_list *commands = fake_runner_commands(&runner);
    for (size_t index = 0; index < commands->count; index++) {
        ASSERT_TRUE(strstr(commands->items[index], "s3cret-word") == NULL);
    }
    char *file = path_join(target, "root/.imprint-password");
    ASSERT_FALSE(platform_is_file(file));
    free(file);
    error_list_free(&errors);
    fake_runner_free(&runner);
    free(target);
}

UTEST(install_base, a_display_manager_keeps_the_password_unexpired) {
    char *target = fresh_directory("expiry-target");
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    install_options_replace(&options.password, "typed");
    command_runner runner = fake_runner_create(stderr);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(base_configure(&runner, &options, target, &errors));
    char *expire = text_format("arch-chroot %s chage -d 0 finn", target);
    ASSERT_FALSE(fake_runner_ran(&runner, expire));
    install_options_replace(&options.display_manager, NULL);
    ASSERT_TRUE(base_configure(&runner, &options, target, &errors));
    ASSERT_TRUE(fake_runner_ran(&runner, expire));
    char *keyboard = path_join(target, "etc/X11/xorg.conf.d/00-keyboard.conf");
    ASSERT_TRUE(platform_is_file(keyboard));
    free(keyboard);
    free(expire);
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
    free(target);
}

UTEST(install_base, pacstrap_falls_back_to_three_mirror_attempts) {
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    offline_stick stick;
    memset(&stick, 0, sizeof stick);
    command_runner runner = fake_runner_create(stderr);
    fake_runner_answer(&runner, "pacstrap", 1, "");
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(base_bootstrap(&runner, &options, &stick, "/mnt", &errors));
    ASSERT_EQ((size_t)3, fake_runner_count(&runner, "pacstrap -K /mnt base base-devel"));
    string_list packages;
    string_list_init(&packages);
    base_packages(&options, &packages);
    ASSERT_TRUE(string_list_contains(&packages, "grub"));
    ASSERT_FALSE(string_list_contains(&packages, "sddm"));
    ASSERT_FALSE(string_list_contains(&packages, "btrfs-progs"));
    string_list_free(&packages);
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
}

UTEST(install_software, aur_from_the_stick_through_pacman_and_a_missing_one_named) {
    char *target = fresh_directory("software-target");
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    offline_stick stick;
    memset(&stick, 0, sizeof stick);
    stick.flatpak_attached = true;
    command_runner runner = fake_runner_create(stderr);
    char *query_missing = text_format("arch-chroot %s pacman -Si only-in-aur", target);
    char *helper = text_format("arch-chroot %s sudo -u finn -H sh -c cd /tmp", target);
    char *present = text_format("arch-chroot %s sh -c command -v yay", target);
    fake_runner_answer(&runner, query_missing, 1, "");
    fake_runner_answer(&runner, helper, 1, "");
    fake_runner_answer(&runner, present, 1, "");
    software_request request = { .options = &options, .resolved = &resolved, .stick = &stick,
                                 .data_directory = INSTALL_FIXTURES "/data", .target_root = target };
    string_list problems;
    string_list_init(&problems);
    error_list errors;
    error_list_init(&errors);
    ASSERT_TRUE(software_install(&runner, &request, &problems, &errors));
    char *from_repository = text_format("arch-chroot %s pacman -S --noconfirm --needed in-repository-bin", target);
    char *sideload = text_format("arch-chroot %s flatpak install -y --noninteractive "
                                 "--sideload-repo=/var/lib/imprint-flatpak/.ostree/repo flathub org.example.App", target);
    char *greeter = text_format("arch-chroot %s pacman -S --noconfirm --needed lightdm lightdm-gtk-greeter", target);
    char *enable = text_format("arch-chroot %s systemctl enable lightdm", target);
    ASSERT_TRUE(fake_runner_ran(&runner, from_repository));
    ASSERT_TRUE(fake_runner_ran(&runner, sideload));
    ASSERT_TRUE(fake_runner_ran(&runner, greeter));
    ASSERT_TRUE(fake_runner_ran(&runner, enable));
    ASSERT_EQ((size_t)1, problems.count);
    ASSERT_TRUE(strstr(problems.items[0], "only-in-aur") != NULL);
    char *sudoers = build_sudoers_path(target);
    ASSERT_FALSE(platform_is_file(sudoers));
    char *dotfile_copy = path_join(target, "home/finn/.config/i3/config");
    ASSERT_TRUE(platform_is_file(dotfile_copy));
    char *keys = path_join(target, "home/finn/.ssh/authorized_keys");
    ASSERT_TRUE(platform_is_file(keys));
    free(keys);
    free(dotfile_copy);
    free(sudoers);
    free(from_repository);
    free(sideload);
    free(greeter);
    free(enable);
    free(query_missing);
    free(helper);
    free(present);
    string_list_free(&problems);
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
    free(target);
}

UTEST(install_software, failing_repository_packages_fail_after_three_attempts) {
    char *target = fresh_directory("software-failing");
    resolved_setup resolved;
    install_options options;
    ASSERT_TRUE(load_options(&resolved, &options));
    offline_stick stick;
    memset(&stick, 0, sizeof stick);
    command_runner runner = fake_runner_create(stderr);
    char *install = text_format("arch-chroot %s pacman -S --noconfirm --needed git", target);
    fake_runner_answer(&runner, install, 1, "");
    software_request request = { .options = &options, .resolved = &resolved, .stick = &stick,
                                 .data_directory = INSTALL_FIXTURES "/data", .target_root = target };
    string_list problems;
    string_list_init(&problems);
    error_list errors;
    error_list_init(&errors);
    ASSERT_FALSE(software_install(&runner, &request, &problems, &errors));
    ASSERT_EQ((size_t)3, fake_runner_count(&runner, install));
    char *service = text_format("arch-chroot %s systemctl enable sshd", target);
    ASSERT_TRUE(fake_runner_ran(&runner, service));
    free(service);
    free(install);
    string_list_free(&problems);
    error_list_free(&errors);
    fake_runner_free(&runner);
    install_options_free(&options);
    resolved_setup_free(&resolved);
    free(target);
}
