/*
 * iUnlocker GLTool - multi-game hide + GPU/RAM spoof
 * Works across devices: arm64/arm, Adreno/Mali/PowerVR/Immortalis, Android 8+
 * Reads /data/adb/iunlocker/config.json  (per-package map)
 * Hooks: open/openat path denial, glGetString (multiple GLES/EGL libs),
 *        /proc/self/maps line filter, /proc/meminfo MemTotal rewrite
 */
#include <sys/types.h>
#include <sys/sysmacros.h>
#include "zygisk.hpp"
#include <android/log.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

#define TAG "iunlocker"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static const char *CONFIG = "/data/adb/iunlocker/config.json";

static const char *BLOCK_PATHS[] = {
    "/system/bin/su", "/system/xbin/su", "/sbin/su", "/vendor/bin/su",
    "/data/adb/magisk", "/data/adb/modules", "/data/adb/ksu", "/data/adb/ap",
    "/debug_ramdisk", "/system/bin/magisk", "/system/bin/magiskpolicy",
    "/system/etc/init/magisk.rc", nullptr
};

struct GpuConfig {
    std::string renderer = "Adreno (TM) 740";
    std::string vendor   = "Qualcomm";
    std::string version  = "OpenGL ES 3.2";
    std::string shading  = "OpenGL ES GLSL ES 3.20";
    int  ram_gb = 8;
    bool props  = true;
    bool paths  = true;
    bool enabled = true;
};

static std::unordered_map<std::string, GpuConfig> g_games;
static GpuConfig g_cfg;
static bool g_target = false;

static bool path_blocked(const char *p) {
    if (!p || !g_cfg.paths) return false;
    for (int i = 0; BLOCK_PATHS[i]; i++)
        if (strstr(p, BLOCK_PATHS[i])) return true;
    return false;
}

static std::string extract_str(const std::string &json, const char *key) {
    auto p = json.find(key);
    if (p == std::string::npos) return {};
    auto q = json.find('"', p + strlen(key));
    if (q == std::string::npos) return {};
    auto e = json.find('"', q + 1);
    return e == std::string::npos ? "" : json.substr(q + 1, e - q - 1);
}

static bool extract_bool(const std::string &json, const char *key, bool def) {
    auto p = json.find(key);
    if (p == std::string::npos) return def;
    auto colon = json.find(':', p);
    if (colon == std::string::npos) return def;
    auto rest = json.substr(colon + 1, 16);
    if (rest.find("true")  != std::string::npos) return true;
    if (rest.find("false") != std::string::npos) return false;
    return def;
}

static int extract_int(const std::string &json, const char *key, int def) {
    auto p = json.find(key);
    if (p == std::string::npos) return def;
    auto num = json.find_first_of("0123456789", p);
    if (num == std::string::npos) return def;
    try {
        return std::stoi(json.substr(num));
    } catch (...) { return def; }
}

static GpuConfig parse_game_obj(const std::string &obj) {
    GpuConfig c;
    auto r   = extract_str(obj, "\"renderer\""); if (!r.empty())   c.renderer = r;
    auto v   = extract_str(obj, "\"vendor\"");   if (!v.empty())   c.vendor   = v;
    auto ver = extract_str(obj, "\"version\"");  if (!ver.empty()) c.version  = ver;
    auto sh  = extract_str(obj, "\"shading\"");  if (!sh.empty())  c.shading  = sh;
    int ram = extract_int(obj, "\"ram\"", 8);
    if (ram >= 2 && ram <= 24) c.ram_gb = ram;
    c.props   = extract_bool(obj, "\"props\"", true);
    c.paths   = extract_bool(obj, "\"paths\"", true);
    c.enabled = extract_bool(obj, "\"enabled\"", true);
    return c;
}

static void load_config() {
    g_games.clear();
    std::ifstream f(CONFIG);
    if (!f) {
        LOGI("no config yet — waiting for WebUI");
        return;
    }
    std::string json((std::istreambuf_iterator<char>(f)), {});

    auto games_pos = json.find("\"games\"");
    if (games_pos == std::string::npos) {
        // legacy single-object file: only apply if it has a renderer key;
        // do not invent a package name
        LOGI("legacy config ignored (no games map) — re-save from WebUI");
        return;
    }

    size_t i = json.find('{', games_pos);
    if (i == std::string::npos) return;
    i++;

    while (i < json.size()) {
        while (i < json.size() && (json[i]==' '||json[i]=='\n'||json[i]=='\r'||json[i]=='\t'||json[i]==','))
            i++;
        if (i >= json.size() || json[i] == '}') break;
        if (json[i] != '"') break;

        size_t name_start = i + 1;
        size_t name_end = json.find('"', name_start);
        if (name_end == std::string::npos) break;
        std::string pkg = json.substr(name_start, name_end - name_start);

        size_t obj_start = json.find('{', name_end);
        if (obj_start == std::string::npos) break;
        int depth = 1;
        size_t obj_end = obj_start + 1;
        while (obj_end < json.size() && depth > 0) {
            if (json[obj_end] == '{') depth++;
            else if (json[obj_end] == '}') depth--;
            obj_end++;
        }
        std::string obj = json.substr(obj_start, obj_end - obj_start);
        g_games[pkg] = parse_game_obj(obj);
        LOGI("loaded %s ram=%d enabled=%d", pkg.c_str(), g_games[pkg].ram_gb, (int)g_games[pkg].enabled);
        i = obj_end;
    }

    // leave empty if nothing parsed — user must pick a game in WebUI
}

static int (*orig_openat)(int, const char *, int, ...) = nullptr;
static int (*orig_open)(const char *, int, ...) = nullptr;
static ssize_t (*orig_read)(int, void *, size_t) = nullptr;

static int hook_openat(int dirfd, const char *pathname, int flags, ...) {
    if (path_blocked(pathname)) { errno = ENOENT; return -1; }
    return orig_openat ? orig_openat(dirfd, pathname, flags) : -1;
}

static int hook_open(const char *pathname, int flags, ...) {
    if (path_blocked(pathname)) { errno = ENOENT; return -1; }
    return orig_open ? orig_open(pathname, flags) : -1;
}

static ssize_t hook_read(int fd, void *buf, size_t count) {
    ssize_t n = orig_read ? orig_read(fd, buf, count) : -1;
    if (n <= 0) return n;

    char path[64] = {};
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    char real[128] = {};
    ssize_t rl = readlink(path, real, sizeof(real) - 1);
    if (rl <= 0) return n;
    real[rl] = 0;

    if (strstr(real, "maps")) {
        std::string s((char *)buf, n);
        std::string out;
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find("zygisk")    == std::string::npos &&
                line.find("magisk")    == std::string::npos &&
                line.find("lsplt")     == std::string::npos &&
                line.find("iunlocker") == std::string::npos)
                out += line + "\n";
        }
        if (!out.empty() && out.size() <= (size_t)n) {
            memcpy(buf, out.data(), out.size());
            return (ssize_t)out.size();
        }
    }

    if (strstr(real, "meminfo")) {
        std::string s((char *)buf, n);
        auto pos = s.find("MemTotal:");
        if (pos != std::string::npos) {
            long kb = (long)g_cfg.ram_gb * 1024 * 1024;
            char repl[64];
            snprintf(repl, sizeof(repl), "MemTotal:       %ld kB", kb);
            size_t eol = s.find('\n', pos);
            if (eol != std::string::npos) {
                s.replace(pos, eol - pos, repl);
                if (s.size() <= (size_t)n) {
                    memcpy(buf, s.data(), s.size());
                    return (ssize_t)s.size();
                }
            }
        }
    }
    return n;
}

static const char *(*orig_glGetString)(unsigned) = nullptr;
static const char *hook_glGetString(unsigned name) {
    switch (name) {
        case 0x1F01: return g_cfg.vendor.c_str();
        case 0x1F00: return g_cfg.renderer.c_str();
        case 0x1F02: return g_cfg.version.c_str();
        case 0x8B8C: return g_cfg.shading.c_str();
    }
    return orig_glGetString ? orig_glGetString(name) : "";
}


/* Resolve mapped library to (dev, inode) for modern Zygisk pltHookRegister */
static bool maps_dev_ino(const char *lib_suffix, dev_t *dev, ino_t *ino) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[512];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        // ... pathname at end
        char *path = strchr(line, '/');
        if (!path) continue;
        // strip newline
        size_t n = strlen(path);
        while (n && (path[n-1] == '\n' || path[n-1] == '\r')) path[--n] = 0;
        if (!strstr(path, lib_suffix)) continue;
        // parse dev and inode: fields are addr perms offset dev inode path
        // example: 7a2b000000-7a2b100000 r-xp 00000000 08:06 1234567 /system/lib64/libc.so
        unsigned maj = 0, min = 0;
        unsigned long inode = 0;
        // scan from start skipping first 4 fields roughly
        char perms[8] = {};
        unsigned long offset = 0;
        if (sscanf(line, "%*s %7s %lx %x:%x %lu", perms, &offset, &maj, &min, &inode) >= 5) {
            *dev = makedev(maj, min);
            *ino = (ino_t)inode;
            ok = true;
            break;
        }
    }
    fclose(f);
    return ok;
}

static void register_plt(Api *api, const char *lib, const char *sym, void *hook, void **orig) {
    dev_t dev = 0;
    ino_t ino = 0;
    if (!maps_dev_ino(lib, &dev, &ino)) {
        LOGI("maps miss %s", lib);
        return;
    }
    api->pltHookRegister(dev, ino, sym, hook, orig);
}

class StrongHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *nice = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!nice) return;

        static bool loaded = false;
        if (!loaded) {
            load_config();
            loaded = true;
        }

        auto it = g_games.find(nice);
        if (it != g_games.end() && it->second.enabled) {
            g_target = true;
            g_cfg = it->second;
            LOGI("target %s renderer=%s ram=%d", nice, g_cfg.renderer.c_str(), g_cfg.ram_gb);

            register_plt(api, "libc.so",        "open",          (void *)hook_open,        (void **)&orig_open);
            register_plt(api, "libc.so",        "openat",        (void *)hook_openat,      (void **)&orig_openat);
            register_plt(api, "libc.so",        "read",          (void *)hook_read,        (void **)&orig_read);
            register_plt(api, "libGLESv2.so",   "glGetString",   (void *)hook_glGetString, (void **)&orig_glGetString);
            register_plt(api, "libGLESv3.so",   "glGetString",   (void *)hook_glGetString, (void **)&orig_glGetString);
            register_plt(api, "libGLESv1_CM.so","glGetString",   (void *)hook_glGetString, (void **)&orig_glGetString);
            register_plt(api, "libEGL.so",      "eglQueryString",(void *)hook_glGetString, (void **)&orig_glGetString);
            api->pltHookCommit();
        }

        env->ReleaseStringUTFChars(args->nice_name, nice);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_target) return;
        // GLES/EGL often load after specialize — retry map lookup + hook
        register_plt(api, "libGLESv2.so",   "glGetString",   (void *)hook_glGetString, (void **)&orig_glGetString);
        register_plt(api, "libGLESv3.so",   "glGetString",   (void *)hook_glGetString, (void **)&orig_glGetString);
        register_plt(api, "libGLESv1_CM.so","glGetString",   (void *)hook_glGetString, (void **)&orig_glGetString);
        register_plt(api, "libEGL.so",      "eglQueryString",(void *)hook_glGetString, (void **)&orig_glGetString);
        api->pltHookCommit();
        if (g_cfg.props) spoof_props();
        LOGI("applied");
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;

    void spoof_props() {
        __system_property_set("ro.debuggable", "0");
        __system_property_set("ro.secure", "1");
        __system_property_set("ro.build.type", "user");
        __system_property_set("ro.build.tags", "release-keys");
        __system_property_set("ro.boot.verifiedbootstate", "green");
        __system_property_set("ro.boot.flash.locked", "1");
        __system_property_set("ro.boot.vbmeta.device_state", "locked");
        __system_property_set("ro.boot.warranty_bit", "0");
        __system_property_set("ro.vendor.boot.warranty_bit", "0");
        __system_property_set("sys.oem_unlock_allowed", "0");
        __system_property_set("ro.boot.veritymode", "enforcing");
        __system_property_set("ro.boot.verifiedbootstate", "green");
    }
};

REGISTER_ZYGISK_MODULE(StrongHide)
