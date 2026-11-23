/* Atari 2600 launcher for the Wii U GamePad Homebrew Menu (libwup, the menu's look: kit).
 * Lists the ROMs found in apps/atari/roms (copied there with File Sharing, FTP): .bin / .a26 / .rom files of 2, 4, 8, 12, 16 or 32 KB.
 * A ROM starts the player (player.wup, next to this app) with the ROM's path; the player comes back here when you leave (HOME). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "wup_kit.h"
#include "littlefs/lfs.h"

WUP_APP("Atari 2600");

#define MAXG 64
static struct { char title[48], value[24], target[128]; } games[MAXG];
static int ngames, started;
static char dir[64], roms[80];
static const char *app_dir(void) { return wup_app_dir()[0] ? wup_app_dir() : "/apps/atari"; }

static int is_rom(const char *n, int *ext) {
    size_t l = strlen(n);
    static const char *const e[] = { ".bin", ".a26", ".rom" };
    for (int i = 0; i < 3; i++) if (l > 4 && !strcasecmp(n + l - 4, e[i])) { *ext = 4; return 1; }
    return 0;
}
static void add(const char *title, const char *value, const char *target) {
    if (ngames >= MAXG) return;
    snprintf(games[ngames].title, sizeof games[0].title, "%s", title);
    snprintf(games[ngames].value, sizeof games[0].value, "%s", value);
    snprintf(games[ngames].target, sizeof games[0].target, "%s", target);
    ngames++;
}
static void scan(void) {
    ngames = 0;
    snprintf(dir, sizeof dir, "%s", app_dir());
    snprintf(roms, sizeof roms, "%s/roms", dir);
    if (!wup_fs_ok()) return;
    lfs_mkdir(&fs_lfs, roms);                              /* (there for whoever looks over File Sharing) */
    lfs_dir_t d; struct lfs_info inf;
    if (!lfs_dir_open(&fs_lfs, &d, roms)) {
        while (lfs_dir_read(&fs_lfs, &d, &inf) > 0 && ngames < MAXG) {
            int ext;
            if (inf.type != LFS_TYPE_REG || !is_rom(inf.name, &ext) || strlen(inf.name) > 40) continue;
            char path[160], size[24], name[48];
            snprintf(path, sizeof path, "%s/%s", roms, inf.name);
            snprintf(name, sizeof name, "%.*s", (int)strlen(inf.name) - ext, inf.name);
            unsigned kb = (unsigned)(inf.size / 1024);
            if (inf.size < 2048 || inf.size > 32768 + 1024) snprintf(size, sizeof size, "%u KB ?", kb);   /* (the player takes 2..32 KB) */
            else snprintf(size, sizeof size, "%u KB", kb);
            add(name, size, path);
        }
        lfs_dir_close(&fs_lfs, &d);
    }
}
static void on_game(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    char player[96];
    snprintf(player, sizeof player, "%s/player.wup", dir);
    if (!wup_exec(player, games[i].target)) started = 1;   /* (this app ends; the player is next) */
}
static void home(lv_obj_t *scr) {
    if (!wup_fs_ok()) { kit_outcome(kit_page(scr, "Atari 2600", "B Back"), "Storage Not Available", ""); return; }
    if (!ngames) {
        kit_empty(kit_page(scr, "Atari 2600", "B Back"), "No Games", "Add .bin, .a26 or .rom files (2 to 32 KB) to apps/atari/roms with File Sharing");
        return;
    }
    lv_obj_t *l = kit_page(scr, "Atari 2600", "B Back    A Play");
    for (int i = 0; i < ngames; i++) kit_row(l, NULL, games[i].title, games[i].value, on_game, (void *)(intptr_t)i);
    char note[96];
    snprintf(note, sizeof note, "Storage: %u of %u KB used", (unsigned)fs_used_kb(), (unsigned)fs_size_kb());
    kit_note(l, note);
}
int main(void) {
    scan();
    kit_start(home);
    while (!started && kit_step()) {}
    return 0;
}
