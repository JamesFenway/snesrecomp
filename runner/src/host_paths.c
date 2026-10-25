/*
 * host_paths.c — executable-relative and current-directory path policy.
 *
 * Kept separate from launcher.c because config, saves, mods, and command-line
 * paths all need this policy even when a game supplies its own ROM launcher.
 */
#include "host_paths.h"
#include "host_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <direct.h>
#  define snesrecomp_chdir _chdir
#  define snesrecomp_getcwd _getcwd
#else
#  include <unistd.h>
#  define snesrecomp_chdir chdir
#  define snesrecomp_getcwd getcwd
#endif
#ifdef __APPLE__
#  include <mach-o/dyld.h>
#endif
#if SNESRECOMP_HOST_IOS
#  include <dirent.h>
#  include <errno.h>
#  include <sys/stat.h>
#endif

/* Full path of the running executable. Returns 1 on success. Platforms
 * without a query mechanism (e.g. Switch homebrew) return 0 and callers
 * fall back to cwd-relative behavior. */
static int get_exe_path(char *out, size_t max_len) {
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)max_len);
    return (n > 0 && n < max_len) ? 1 : 0;
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)max_len;
    return _NSGetExecutablePath(out, &size) == 0 ? 1 : 0;
#elif defined(__linux__)
    /* Inside an AppImage, /proc/self/exe resolves inside the read-only mount.
     * $APPIMAGE instead identifies the user-visible file beside config.ini,
     * rom.cfg, and saves. */
    const char *appimg = getenv("APPIMAGE");
    if (appimg && appimg[0]) {
        size_t len = strlen(appimg);
        if (len >= max_len) return 0;
        memcpy(out, appimg, len + 1);
        return 1;
    }
    ssize_t n = readlink("/proc/self/exe", out, max_len - 1);
    if (n <= 0) return 0;
    out[n] = '\0';
    return 1;
#else
    (void)out;
    (void)max_len;
    return 0;
#endif
}

#if SNESRECOMP_HOST_IOS
/* iOS: the app bundle that holds the executable is read-only and replaced on
 * every install, so "beside the executable" -- config.ini, rom.cfg, saves/,
 * mods/ -- is the app's Documents folder instead (HOME is the app's data
 * container; Documents is what the Files app shows when the bundle sets
 * UIFileSharingEnabled). Returns 0 when HOME is unset. */
static int get_ios_documents_dir(char *out, size_t max_len) {
    const char *home = getenv("HOME");
    if (!home || !home[0]) return 0;
    return snprintf(out, max_len, "%s/Documents/", home) < (int)max_len;
}
#endif

/* Directory containing the executable, with trailing separator.
 * Falls back to "./" when the executable path cannot be determined. */
static void get_exe_dir(char *out, size_t max_len) {
#if SNESRECOMP_HOST_IOS
    if (get_ios_documents_dir(out, max_len)) return;
#endif
    char exe_path[1024];
    if (get_exe_path(exe_path, sizeof(exe_path))) {
        char *last_sep = NULL;
        for (char *p = exe_path; *p; p++)
            if (*p == '/' || *p == '\\') last_sep = p;
        if (last_sep) {
            *(last_sep + 1) = '\0';
            snprintf(out, max_len, "%s", exe_path);
            return;
        }
    }
    snprintf(out, max_len, "./");
}

int snesrecomp_abspath(const char *path, char *out, size_t max_len) {
    if (!path || !*path || !out || max_len == 0) return 0;
#ifdef _WIN32
    char tmp[1024];
    if (!_fullpath(tmp, path, sizeof(tmp))) return 0;
    if (strlen(tmp) >= max_len) return 0;
    strcpy(out, tmp);
    return 1;
#else
    if (path[0] == '/') {
        if (strlen(path) >= max_len) return 0;
        strcpy(out, path);
        return 1;
    }
    char cwd[1024];
    if (!snesrecomp_getcwd(cwd, sizeof(cwd))) return 0;
    if (snprintf(out, max_len, "%s/%s", cwd, path) >= (int)max_len) return 0;
    return 1;
#endif
}

int snesrecomp_exe_path(char *out, size_t max_len) {
    if (!out || max_len == 0) return 0;
    out[0] = '\0';
    return get_exe_path(out, max_len);
}

int snesrecomp_exe_basename(char *out, size_t max_len) {
    char exe_path[1024];
    if (!out || max_len == 0) return 0;
    out[0] = '\0';
    if (!get_exe_path(exe_path, sizeof(exe_path))) return 0;

    const char *base = exe_path;
    for (const char *p = exe_path; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    if (!*base) return 0;

    size_t len = strlen(base);
    /* Strip a trailing ".exe" so the same name serves the CMake target on
     * every platform. Case-insensitive: Windows does not promise the case. */
    if (len > 4) {
        const char *ext = base + len - 4;
        if ((ext[0] == '.') &&
            (ext[1] == 'e' || ext[1] == 'E') &&
            (ext[2] == 'x' || ext[2] == 'X') &&
            (ext[3] == 'e' || ext[3] == 'E'))
            len -= 4;
    }
    if (len >= max_len) return 0;
    memcpy(out, base, len);
    out[len] = '\0';
    return 1;
}

int snesrecomp_exe_dir_path(const char *leaf, char *out, size_t max_len) {
    if (!leaf || !out || max_len == 0) return 0;
    char dir[1024];
    get_exe_dir(dir, sizeof(dir));
    if (dir[0] == '.' &&
        (dir[1] == '/' || dir[1] == '\\' || dir[1] == '\0'))
        return 0;
    if (snprintf(out, max_len, "%s%s", dir, leaf) >= (int)max_len) return 0;
    return 1;
}

/* Probe by creating a file: access(W_OK) is unreliable on Windows and inside
 * sandboxed mounts. */
static int dir_is_writable(const char *dir) {
    char probe[1024];
    if (snprintf(probe, sizeof(probe), "%s.snesrecomp_write_probe",
                 dir) >= (int)sizeof(probe))
        return 0;
    FILE *f = fopen(probe, "wb");
    if (!f) return 0;
    fclose(f);
    remove(probe);
    return 1;
}

#if SNESRECOMP_HOST_IOS
/* Files the player (or the game, on their behalf) owns once they exist: a
 * new install must not put the build's copy back over them. Everything else
 * in the payload is the build's and is refreshed on every start. */
static int payload_file_is_players(const char *name) {
    static const char *const kOwned[] = {
        "config.ini", "keybinds.ini", "rom.cfg", "state.toml",
    };
    for (size_t i = 0; i < sizeof(kOwned) / sizeof(kOwned[0]); i++)
        if (strcmp(name, kOwned[i]) == 0) return 1;
    return 0;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    char tmp[1100];
    if (snprintf(tmp, sizeof(tmp), "%s.part", dst) >= (int)sizeof(tmp)) {
        fclose(in);
        return 0;
    }
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    char buf[65536];
    size_t n;
    int ok = 1;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    if (!ok || rename(tmp, dst) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}

/* Mirror `src` (a directory in the bundle) into `dst`, recursively. */
static void sync_payload_dir(const char *src, const char *dst, int *copied, int *failed) {
    DIR *d = opendir(src);
    if (!d) return;
    if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
        (*failed)++;
        closedir(d);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char from[1024], to[1024];
        if (snprintf(from, sizeof(from), "%s/%s", src, e->d_name) >= (int)sizeof(from) ||
            snprintf(to, sizeof(to), "%s/%s", dst, e->d_name) >= (int)sizeof(to)) {
            (*failed)++;
            continue;
        }
        struct stat st;
        if (stat(from, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            sync_payload_dir(from, to, copied, failed);
            continue;
        }
        struct stat have;
        if (payload_file_is_players(e->d_name) && stat(to, &have) == 0) continue;
        if (copy_file(from, to)) (*copied)++;
        else (*failed)++;
    }
    closedir(d);
}

/* The build stages what desktop ports find beside the executable (the mod
 * catalog, config.ini's seed, the ROM for a personal build...) into the
 * bundle's payload/ folder; copy it into Documents before anything reads it. */
static void sync_ios_payload(const char *documents) {
    char exe[1024];
    if (!get_exe_path(exe, sizeof(exe))) return;
    char *sep = strrchr(exe, '/');
    if (!sep) return;
    *sep = '\0';
    char payload[1100];
    if (snprintf(payload, sizeof(payload), "%s/payload", exe) >= (int)sizeof(payload)) return;
    struct stat st;
    if (stat(payload, &st) != 0 || !S_ISDIR(st.st_mode)) return;
    char dst[1024];
    snprintf(dst, sizeof(dst), "%s", documents);
    size_t len = strlen(dst);
    if (len > 1 && dst[len - 1] == '/') dst[len - 1] = '\0';
    int copied = 0, failed = 0;
    sync_payload_dir(payload, dst, &copied, &failed);
    fprintf(stderr, "[Launcher] App payload -> Documents: %d file(s) copied, %d failed.\n",
            copied, failed);
}
#endif

int snesrecomp_anchor_to_exe_dir(void) {
    char dir[1024];
    get_exe_dir(dir, sizeof(dir));
#if SNESRECOMP_HOST_IOS
    sync_ios_payload(dir);
#endif
    if (dir[0] == '.' &&
        (dir[1] == '/' || dir[1] == '\\' || dir[1] == '\0'))
        return 0;
    if (!dir_is_writable(dir)) {
        fprintf(stderr,
                "[Launcher] Executable directory '%s' is not writable; "
                "config and saves stay in the current directory.\n", dir);
        return 0;
    }
    if (snesrecomp_chdir(dir) != 0) {
        fprintf(stderr, "[Launcher] Could not change directory to '%s'.\n", dir);
        return 0;
    }
    fprintf(stderr, "[Launcher] Config/saves anchored to '%s'.\n", dir);
    return 1;
}
