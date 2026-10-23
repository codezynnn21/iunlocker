/*
 * iUnlocker GLTool — aligned with reference architecture
 *
 * Reference (iUnlocker GLTool v1.1.5):
 *  - Root companion loads package configs
 *  - iUnlockerVMHook / inline on glGetString
 *  - Full Vulkan: vkGetPhysicalDeviceProperties / Features / Memory / GetInstanceProcAddr
 *
 * This build:
 *  - Companion reads /data/adb/iunlocker/config.json as root
 *  - GLES: dlsym intercept + inline hook (correct GL_VENDOR/RENDERER enums)
 *  - Vulkan: spoof deviceName in vkGetPhysicalDeviceProperties
 *  - Path hide + meminfo RAM
 *
 * Game must NOT be on DenyList.
 */
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <pthread.h>
#include "zygisk.hpp"
#include <android/log.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <vector>

#define TAG "iunlocker"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;

static const char *CONFIG = "/data/adb/iunlocker/config.json";

enum {
    GL_VENDOR                   = 0x1F00,
    GL_RENDERER                 = 0x1F01,
    GL_VERSION                  = 0x1F02,
    GL_SHADING_LANGUAGE_VERSION = 0x8B8C,
};

/* Minimal Vulkan types (avoid vulkan headers) */
struct VkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char     deviceName[256];
    uint8_t  pipelineCacheUUID[16];
    /* limits / sparse omitted — we only touch deviceName at fixed offset */
};

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
    int  ram_gb  = 8;
    bool props   = true;
    bool paths   = true;
    bool enabled = true;
};

static std::unordered_map<std::string, GpuConfig> g_games;
static GpuConfig g_cfg;
static bool g_target = false;
static std::atomic<bool> g_gl_ok{false};
static std::atomic<bool> g_vk_ok{false};

/* -------------------- JSON -------------------- */

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
    try { return std::stoi(json.substr(num)); } catch (...) { return def; }
}
static GpuConfig parse_game_obj(const std::string &obj) {
    GpuConfig c;
    auto r = extract_str(obj, "\"renderer\""); if (!r.empty()) c.renderer = r;
    auto v = extract_str(obj, "\"vendor\"");   if (!v.empty()) c.vendor = v;
    auto ver = extract_str(obj, "\"version\""); if (!ver.empty()) c.version = ver;
    auto sh = extract_str(obj, "\"shading\""); if (!sh.empty()) c.shading = sh;
    /* reference-style keys */
    auto gm = extract_str(obj, "\"gl_model\"");   if (!gm.empty()) c.renderer = gm;
    auto gv = extract_str(obj, "\"gl_vendor\"");  if (!gv.empty()) c.vendor = gv;
    auto gver = extract_str(obj, "\"gl_version\""); if (!gver.empty()) c.version = gver;
    int ram = extract_int(obj, "\"ram\"", 8);
    if (ram >= 2 && ram <= 24) c.ram_gb = ram;
    c.props   = extract_bool(obj, "\"props\"", true);
    c.paths   = extract_bool(obj, "\"paths\"", true);
    c.enabled = extract_bool(obj, "\"enabled\"", true);
    return c;
}

static void parse_config_json(const std::string &json) {
    g_games.clear();
    auto games_pos = json.find("\"games\"");
    if (games_pos == std::string::npos) {
        LOGI("config has no games key");
        return;
    }
    size_t i = json.find('{', games_pos);
    if (i == std::string::npos) return;
    i++;
    while (i < json.size()) {
        while (i < json.size() && strchr(" \n\r\t,", json[i])) i++;
        if (i >= json.size() || json[i] == '}') break;
        if (json[i] != '"') break;
        size_t ns = i + 1, ne = json.find('"', ns);
        if (ne == std::string::npos) break;
        std::string pkg = json.substr(ns, ne - ns);
        size_t os = json.find('{', ne);
        if (os == std::string::npos) break;
        int depth = 1; size_t oe = os + 1;
        while (oe < json.size() && depth) {
            if (json[oe] == '{') depth++;
            else if (json[oe] == '}') depth--;
            oe++;
        }
        g_games[pkg] = parse_game_obj(json.substr(os, oe - os));
        LOGI("cfg [%s] en=%d ren=%s", pkg.c_str(),
             (int)g_games[pkg].enabled, g_games[pkg].renderer.c_str());
        i = oe;
    }
    LOGI("loaded %zu packages", g_games.size());
}

static void load_config_from_disk() {
    std::ifstream f(CONFIG);
    if (!f) { LOGI("no config file"); return; }
    std::string json((std::istreambuf_iterator<char>(f)), {});
    parse_config_json(json);
}

static bool path_blocked(const char *p) {
    if (!p || !g_cfg.paths) return false;
    for (int i = 0; BLOCK_PATHS[i]; i++)
        if (strstr(p, BLOCK_PATHS[i])) return true;
    return false;
}

/* -------------------- GL hooks -------------------- */

static const char *(*orig_glGetString)(unsigned) = nullptr;
static void *(*orig_eglGetProcAddress)(const char *) = nullptr;
static void *(*orig_dlsym)(void *, const char *) = nullptr;
static void *(*orig_dlopen)(const char *, int) = nullptr;
static void *(*orig_android_dlopen_ext)(const char *, int, const void *) = nullptr;

static const char *hook_glGetString(unsigned name) {
    switch (name) {
        case GL_VENDOR:                   return g_cfg.vendor.c_str();
        case GL_RENDERER:                 return g_cfg.renderer.c_str();
        case GL_VERSION:                  return g_cfg.version.c_str();
        case GL_SHADING_LANGUAGE_VERSION: return g_cfg.shading.c_str();
        default: break;
    }
    return orig_glGetString ? orig_glGetString(name) : "";
}

static void *hook_eglGetProcAddress(const char *name) {
    if (name && !strcmp(name, "glGetString"))
        return (void *)hook_glGetString;
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(name) : nullptr;
}

/* -------------------- Vulkan hooks -------------------- */

using PFN_vkVoid = void (*)(void);
using PFN_vkGPDP = void (*)(void *dev, VkPhysicalDeviceProperties *props);
using PFN_vkGIPA = void *(*)(void *instance, const char *name);

static PFN_vkGPDP orig_vkGetPhysicalDeviceProperties = nullptr;
static PFN_vkGIPA orig_vkGetInstanceProcAddr = nullptr;

static void hook_vkGetPhysicalDeviceProperties(void *dev, VkPhysicalDeviceProperties *props) {
    if (orig_vkGetPhysicalDeviceProperties)
        orig_vkGetPhysicalDeviceProperties(dev, props);
    if (props && g_target) {
        memset(props->deviceName, 0, sizeof(props->deviceName));
        strncpy(props->deviceName, g_cfg.renderer.c_str(), sizeof(props->deviceName) - 1);
        LOGI("vk deviceName -> %s", props->deviceName);
    }
}

static void *hook_vkGetInstanceProcAddr(void *instance, const char *name) {
    if (name && !strcmp(name, "vkGetPhysicalDeviceProperties"))
        return (void *)hook_vkGetPhysicalDeviceProperties;
    return orig_vkGetInstanceProcAddr ? orig_vkGetInstanceProcAddr(instance, name) : nullptr;
}

static void *hook_dlsym(void *handle, const char *symbol) {
    if (symbol) {
        if (!strcmp(symbol, "glGetString")) {
            if (!orig_glGetString && orig_dlsym)
                orig_glGetString = (const char *(*)(unsigned))orig_dlsym(handle, symbol);
            return (void *)hook_glGetString;
        }
        if (!strcmp(symbol, "eglGetProcAddress")) {
            if (!orig_eglGetProcAddress && orig_dlsym)
                orig_eglGetProcAddress = (void *(*)(const char *))orig_dlsym(handle, symbol);
            return (void *)hook_eglGetProcAddress;
        }
        if (!strcmp(symbol, "vkGetPhysicalDeviceProperties")) {
            if (!orig_vkGetPhysicalDeviceProperties && orig_dlsym)
                orig_vkGetPhysicalDeviceProperties = (PFN_vkGPDP)orig_dlsym(handle, symbol);
            return (void *)hook_vkGetPhysicalDeviceProperties;
        }
        if (!strcmp(symbol, "vkGetInstanceProcAddr")) {
            if (!orig_vkGetInstanceProcAddr && orig_dlsym)
                orig_vkGetInstanceProcAddr = (PFN_vkGIPA)orig_dlsym(handle, symbol);
            return (void *)hook_vkGetInstanceProcAddr;
        }
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

/* -------------------- inline hook -------------------- */

static bool mprotect_write(void *addr, size_t len) {
    uintptr_t page = (uintptr_t)addr & ~((uintptr_t)0xFFF);
    size_t span = ((uintptr_t)addr + len + 0xFFF) - page;
    if (mprotect((void *)page, span, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) return true;
    return mprotect((void *)page, span, PROT_READ | PROT_WRITE) == 0;
}
static void mprotect_exec(void *addr, size_t len) {
    uintptr_t page = (uintptr_t)addr & ~((uintptr_t)0xFFF);
    size_t span = ((uintptr_t)addr + len + 0xFFF) - page;
    mprotect((void *)page, span, PROT_READ | PROT_EXEC);
}

#if defined(__aarch64__)
static bool install_inline(void *sym, void *hook, void **orig_out) {
    if (!sym || !hook) return false;
    auto *p = (uint32_t *)sym;
    if (p[0] == 0x58000050u && p[1] == 0xD61F0200u) return true;
    static uint8_t pool[8][32];
    static int pool_i = 0;
    if (pool_i >= 8) return false;
    uint8_t *tramp = pool[pool_i++];
    if (!mprotect_write(tramp, 32)) return false;
    memcpy(tramp, sym, 16);
    auto *t = (uint32_t *)(tramp + 16);
    t[0] = 0x58000050u;
    t[1] = 0xD61F0200u;
    *(uint64_t *)(t + 2) = (uint64_t)((uint8_t *)sym + 16);
    mprotect_exec(tramp, 32);
    if (orig_out && !*orig_out) *orig_out = tramp;
    if (!mprotect_write(sym, 16)) return false;
    p[0] = 0x58000050u;
    p[1] = 0xD61F0200u;
    *(uint64_t *)(p + 2) = (uint64_t)hook;
    mprotect_exec(sym, 16);
    __builtin___clear_cache((char *)sym, (char *)sym + 16);
    return true;
}
#elif defined(__arm__)
static bool install_inline(void *sym, void *hook, void **orig_out) {
    if (!sym || !hook) return false;
    auto *p = (uint32_t *)sym;
    if (p[0] == 0xE51FF004u) return true;
    static uint32_t pool[8][4];
    static int pool_i = 0;
    if (pool_i >= 8) return false;
    uint32_t *tramp = pool[pool_i++];
    if (!mprotect_write(tramp, 16)) return false;
    tramp[0] = p[0]; tramp[1] = p[1];
    tramp[2] = 0xE51FF004u;
    tramp[3] = (uint32_t)((uint8_t *)sym + 8);
    mprotect_exec(tramp, 16);
    if (orig_out && !*orig_out) *orig_out = tramp;
    if (!mprotect_write(sym, 8)) return false;
    p[0] = 0xE51FF004u;
    p[1] = (uint32_t)hook;
    mprotect_exec(sym, 8);
    __builtin___clear_cache((char *)sym, (char *)sym + 8);
    return true;
}
#else
static bool install_inline(void *, void *, void **) { return false; }
#endif

static void *real_dlsym(void *h, const char *s) {
    return orig_dlsym ? orig_dlsym(h, s) : dlsym(h, s);
}

static bool try_hook_gl() {
    const char *libs[] = {
        "libGLESv2.so", "libGLESv3.so", "libEGL.so",
        "/system/lib64/libGLESv2.so", "/system/lib/libGLESv2.so",
        "/vendor/lib64/libGLESv2.so", "/vendor/lib/libGLESv2.so",
        nullptr
    };
    bool any = false;
    for (int i = 0; libs[i]; i++) {
        void *h = dlopen(libs[i], RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen(libs[i], RTLD_NOW);
        if (!h) continue;
        void *gl = real_dlsym(h, "glGetString");
        if (gl && gl != (void *)hook_glGetString) {
            if (install_inline(gl, (void *)hook_glGetString, (void **)&orig_glGetString)) {
                LOGI("GL inline glGetString @ %p (%s)", gl, libs[i]);
                any = true;
            } else LOGE("GL inline FAIL @ %p", gl);
        }
        void *egl = real_dlsym(h, "eglGetProcAddress");
        if (egl && egl != (void *)hook_eglGetProcAddress) {
            if (install_inline(egl, (void *)hook_eglGetProcAddress, (void **)&orig_eglGetProcAddress)) {
                LOGI("GL inline eglGetProcAddress @ %p", egl);
                any = true;
            }
        }
    }
    if (any) g_gl_ok = true;
    return any;
}

static bool try_hook_vk() {
    const char *libs[] = {
        "libvulkan.so",
        "/system/lib64/libvulkan.so", "/system/lib/libvulkan.so",
        "/vendor/lib64/libvulkan.so", "/vendor/lib/libvulkan.so",
        nullptr
    };
    bool any = false;
    for (int i = 0; libs[i]; i++) {
        void *h = dlopen(libs[i], RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen(libs[i], RTLD_NOW);
        if (!h) continue;
        void *gp = real_dlsym(h, "vkGetPhysicalDeviceProperties");
        if (gp && gp != (void *)hook_vkGetPhysicalDeviceProperties) {
            if (install_inline(gp, (void *)hook_vkGetPhysicalDeviceProperties,
                               (void **)&orig_vkGetPhysicalDeviceProperties)) {
                LOGI("VK inline Properties @ %p (%s)", gp, libs[i]);
                any = true;
            } else LOGE("VK inline FAIL @ %p", gp);
        }
        void *gipa = real_dlsym(h, "vkGetInstanceProcAddr");
        if (gipa && gipa != (void *)hook_vkGetInstanceProcAddr) {
            if (install_inline(gipa, (void *)hook_vkGetInstanceProcAddr,
                               (void **)&orig_vkGetInstanceProcAddr)) {
                LOGI("VK inline GetInstanceProcAddr @ %p", gipa);
                any = true;
            }
        }
    }
    if (any) g_vk_ok = true;
    return any;
}

/* -------------------- libc -------------------- */

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
    char path[64] = {}, real[160] = {};
    snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    ssize_t rl = readlink(path, real, sizeof(real) - 1);
    if (rl <= 0) return n;
    real[rl] = 0;
    if (strstr(real, "maps")) {
        std::string s((char *)buf, n), out, line;
        std::istringstream iss(s);
        while (std::getline(iss, line)) {
            if (line.find("zygisk") == std::string::npos &&
                line.find("magisk") == std::string::npos &&
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

static void *hook_dlopen(const char *f, int flags) {
    void *h = orig_dlopen ? orig_dlopen(f, flags) : nullptr;
    if (g_target && f) {
        if (strstr(f, "GLES") || strstr(f, "EGL")) try_hook_gl();
        if (strstr(f, "vulkan") || strstr(f, "Vulkan")) try_hook_vk();
    }
    return h;
}
static void *hook_android_dlopen_ext(const char *f, int flags, const void *ext) {
    void *h = orig_android_dlopen_ext ? orig_android_dlopen_ext(f, flags, ext) : nullptr;
    if (g_target && f) {
        if (strstr(f, "GLES") || strstr(f, "EGL")) try_hook_gl();
        if (strstr(f, "vulkan") || strstr(f, "Vulkan")) try_hook_vk();
    }
    return h;
}

/* -------------------- zygisk plt -------------------- */

static bool maps_dev_ino(const char *lib_name, dev_t *dev, ino_t *ino) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return false;
    char line[768];
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, " r-xp ")) continue;
        char *path = strchr(line, '/');
        if (!path) continue;
        size_t n = strlen(path);
        while (n && (path[n-1]=='\n'||path[n-1]=='\r')) path[--n]=0;
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (strcmp(base, lib_name) != 0 && !strstr(path, lib_name)) continue;
        unsigned maj=0,min=0; unsigned long inode=0,off=0; char perms[8]={};
        if (sscanf(line, "%*s %7s %lx %x:%x %lu", perms, &off, &maj, &min, &inode) >= 5 && inode) {
            *dev = makedev(maj, min); *ino = (ino_t)inode; ok = true; break;
        }
    }
    fclose(f);
    return ok;
}

static void register_plt(Api *api, const char *lib, const char *sym, void *hook, void **orig) {
    dev_t dev=0; ino_t ino=0;
    if (!maps_dev_ino(lib, &dev, &ino)) { LOGI("maps miss %s", lib); return; }
    api->pltHookRegister(dev, ino, sym, hook, orig);
    LOGI("zygisk %s!%s", lib, sym);
}

static void register_sym_all(Api *api, const char *sym, void *hook, void **orig) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[768];
    std::unordered_map<unsigned long, dev_t> seen;
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, " r-xp ")) continue;
        if (strstr(line, "zygisk") || strstr(line, "magisk")) continue;
        unsigned maj=0,min=0; unsigned long inode=0,off=0; char perms[8]={};
        if (sscanf(line, "%*s %7s %lx %x:%x %lu", perms, &off, &maj, &min, &inode) < 5) continue;
        if (!inode || seen.count(inode)) continue;
        seen[inode] = makedev(maj, min);
    }
    fclose(f);
    for (auto &kv : seen)
        api->pltHookRegister(kv.second, (ino_t)kv.first, sym, hook, orig);
    LOGI("zygisk %s x%zu", sym, seen.size());
}

static void *watcher(void *) {
    for (int i = 0; i < 40; i++) {
        usleep(250000);
        try_hook_gl();
        try_hook_vk();
        if (g_gl_ok && g_vk_ok) break;
    }
    for (int i = 0; i < 20; i++) {
        usleep(1000000);
        try_hook_gl();
        try_hook_vk();
    }
    LOGI("watcher done gl=%d vk=%d", (int)g_gl_ok.load(), (int)g_vk_ok.load());
    return nullptr;
}

/* -------------------- companion (root config load) -------------------- */

static void companion_handler(int fd) {
    LOGI("Root companion started, reading config files");
    std::ifstream f(CONFIG);
    std::string json;
    if (f) json.assign((std::istreambuf_iterator<char>(f)), {});
    uint32_t n = (uint32_t)json.size();
    if (write(fd, &n, sizeof(n)) != (ssize_t)sizeof(n)) {
        LOGE("companion: send size fail");
        return;
    }
    if (n && write(fd, json.data(), n) != (ssize_t)n) {
        LOGE("companion: send data fail");
        return;
    }
    LOGI("companion: sent %u bytes", n);
}

/* -------------------- module -------------------- */

class StrongHide : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *nice = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!nice) return;

        if (api->getFlags() & zygisk::StateFlag::PROCESS_ON_DENYLIST)
            LOGE("DenyList ON for %s — REMOVE from DenyList", nice);

        /* prefer companion (root) config, fallback disk */
        bool got = false;
        int cfd = api->connectCompanion();
        if (cfd >= 0) {
            uint32_t n = 0;
            if (read(cfd, &n, sizeof(n)) == (ssize_t)sizeof(n) && n > 0 && n < 2 * 1024 * 1024) {
                std::string json(n, '\0');
                size_t off = 0;
                while (off < n) {
                    ssize_t r = read(cfd, &json[off], n - off);
                    if (r <= 0) break;
                    off += (size_t)r;
                }
                if (off == n) {
                    parse_config_json(json);
                    got = true;
                    LOGI("config from companion (%u bytes)", n);
                }
            }
            close(cfd);
        } else {
            LOGI("companion unavailable, disk fallback");
        }
        if (!got) load_config_from_disk();

        auto it = g_games.find(nice);
        if (it == g_games.end() || !it->second.enabled) {
            LOGI("skip %s", nice);
            env->ReleaseStringUTFChars(args->nice_name, nice);
            return;
        }

        g_target = true;
        g_cfg = it->second;
        LOGI("TARGET %s renderer=[%s] vendor=[%s] — Setting up GPU renderer spoofer hooks",
             nice, g_cfg.renderer.c_str(), g_cfg.vendor.c_str());

        register_plt(api, "libc.so", "open",   (void *)hook_open,   (void **)&orig_open);
        register_plt(api, "libc.so", "openat", (void *)hook_openat, (void **)&orig_openat);
        register_plt(api, "libc.so", "read",   (void *)hook_read,   (void **)&orig_read);

        register_plt(api, "libdl.so", "dlsym", (void *)hook_dlsym, (void **)&orig_dlsym);
        register_sym_all(api, "dlsym", (void *)hook_dlsym, (void **)&orig_dlsym);

        register_plt(api, "libdl.so", "dlopen", (void *)hook_dlopen, (void **)&orig_dlopen);
        register_plt(api, "libdl.so", "android_dlopen_ext",
                     (void *)hook_android_dlopen_ext, (void **)&orig_android_dlopen_ext);
        register_sym_all(api, "android_dlopen_ext",
                         (void *)hook_android_dlopen_ext, (void **)&orig_android_dlopen_ext);

        bool ok = api->pltHookCommit();
        LOGI("pltHookCommit=%d", (int)ok);

        env->ReleaseStringUTFChars(args->nice_name, nice);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (!g_target) return;
        if (g_cfg.props) {
            __system_property_set("ro.debuggable", "0");
            __system_property_set("ro.secure", "1");
            __system_property_set("ro.build.type", "user");
            __system_property_set("ro.build.tags", "release-keys");
            __system_property_set("ro.boot.verifiedbootstate", "green");
            __system_property_set("ro.boot.flash.locked", "1");
            __system_property_set("ro.boot.vbmeta.device_state", "locked");
            __system_property_set("ro.boot.warranty_bit", "0");
            __system_property_set("sys.oem_unlock_allowed", "0");
        }
        try_hook_gl();
        try_hook_vk();
        pthread_t th;
        pthread_create(&th, nullptr, watcher, nullptr);
        pthread_detach(th);
        LOGI("post: hooks armed");
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(StrongHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
