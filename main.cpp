/*
 * iUnlocker GLTool — inline kept, crash-hardened
 *
 * Order:
 *  1. dlsym / eglGetProcAddress / vkGetInstanceProcAddr  (always)
 *  2. PLT/GOT patch on /data/app libs only               (safe)
 *  3. Inline on GLES/Vulkan exports                      (try; skip if mprotect fails)
 *
 * Inline never aborts the process on failure.
 * Trampolines live in mmap(RWX), not static data.
 */
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <pthread.h>
#include "zygisk.hpp"
#include <android/log.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <elf.h>
#include <link.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <mutex>
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
static std::mutex g_hook_mu;
static std::atomic<bool> g_gl_inline{false};
static std::atomic<bool> g_vk_inline{false};

static void *(*real_dlsym)(void *, const char *) = nullptr;

/* -------------------- config -------------------- */

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
    auto gm = extract_str(obj, "\"gl_model\""); if (!gm.empty()) c.renderer = gm;
    auto gv = extract_str(obj, "\"gl_vendor\""); if (!gv.empty()) c.vendor = gv;
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
    if (games_pos == std::string::npos) return;
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
}
static void load_config_disk() {
    std::ifstream f(CONFIG);
    if (!f) return;
    std::string json((std::istreambuf_iterator<char>(f)), {});
    parse_config_json(json);
}
static bool path_blocked(const char *p) {
    if (!p || !g_cfg.paths) return false;
    for (int i = 0; BLOCK_PATHS[i]; i++)
        if (strstr(p, BLOCK_PATHS[i])) return true;
    return false;
}

/* -------------------- hook targets -------------------- */

static const char *(*orig_glGetString)(unsigned) = nullptr;
static void *(*orig_eglGetProcAddress)(const char *) = nullptr;

struct VkPDProps {
    uint32_t apiVersion, driverVersion, vendorID, deviceID, deviceType;
    char deviceName[256];
};
using PFN_vkGPDP = void (*)(void *, void *);
using PFN_vkGIPA = void *(*)(void *, const char *);
static PFN_vkGPDP orig_vkGPDP = nullptr;
static PFN_vkGIPA orig_vkGIPA = nullptr;

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
    if (name && !strcmp(name, "glGetString")) return (void *)hook_glGetString;
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(name) : nullptr;
}
static void hook_vkGPDP(void *dev, void *props) {
    if (orig_vkGPDP) orig_vkGPDP(dev, props);
    if (props && g_target) {
        auto *p = (VkPDProps *)props;
        memset(p->deviceName, 0, sizeof(p->deviceName));
        strncpy(p->deviceName, g_cfg.renderer.c_str(), sizeof(p->deviceName) - 1);
    }
}
static void *hook_vkGIPA(void *instance, const char *name) {
    if (name && !strcmp(name, "vkGetPhysicalDeviceProperties"))
        return (void *)hook_vkGPDP;
    return orig_vkGIPA ? orig_vkGIPA(instance, name) : nullptr;
}

static void *call_real_dlsym(void *h, const char *s) {
    return real_dlsym ? real_dlsym(h, s) : nullptr;
}

static void *hook_dlsym(void *handle, const char *symbol) {
    if (symbol && g_target) {
        if (!strcmp(symbol, "glGetString")) {
            if (!orig_glGetString)
                orig_glGetString = (const char *(*)(unsigned))call_real_dlsym(handle, symbol);
            return (void *)hook_glGetString;
        }
        if (!strcmp(symbol, "eglGetProcAddress")) {
            if (!orig_eglGetProcAddress)
                orig_eglGetProcAddress = (void *(*)(const char *))call_real_dlsym(handle, symbol);
            return (void *)hook_eglGetProcAddress;
        }
        if (!strcmp(symbol, "vkGetPhysicalDeviceProperties")) {
            if (!orig_vkGPDP)
                orig_vkGPDP = (PFN_vkGPDP)call_real_dlsym(handle, symbol);
            return (void *)hook_vkGPDP;
        }
        if (!strcmp(symbol, "vkGetInstanceProcAddr")) {
            if (!orig_vkGIPA)
                orig_vkGIPA = (PFN_vkGIPA)call_real_dlsym(handle, symbol);
            return (void *)hook_vkGIPA;
        }
    }
    return call_real_dlsym(handle, symbol);
}

/* -------------------- safe mprotect / trampoline -------------------- */

static bool page_protect(void *addr, size_t len, int prot) {
    uintptr_t page = (uintptr_t)addr & ~((uintptr_t)0xFFF);
    size_t span = ((uintptr_t)addr + len + 0xFFF) - page;
    return mprotect((void *)page, span, prot) == 0;
}

/* executable trampoline pool via mmap — survives, properly mapped */
static uint8_t *tramp_alloc(size_t sz) {
    void *p = mmap(nullptr, sz, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        /* some devices reject RWX — try RW then mprotect X */
        p = mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) return nullptr;
        if (mprotect(p, sz, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
            munmap(p, sz);
            return nullptr;
        }
    }
    return (uint8_t *)p;
}

#if defined(__aarch64__)
/*
 * Safe aarch64 inline:
 *  - only if target is 16-byte aligned (common for exported funcs)
 *  - skip if already patched
 *  - skip if first insn is a BTI/PAC hint we don't understand well → still ok for LDR/BR
 *  - on ANY mprotect failure → return false (dlsym path remains)
 */
static bool install_inline(void *sym, void *hook, void **orig_out, const char *tag) {
    if (!sym || !hook) return false;
    if (((uintptr_t)sym & 0xF) != 0) {
        LOGI("inline skip %s: unaligned %p", tag, sym);
        return false;
    }
    auto *p = (uint32_t *)sym;
    /* already our stub */
    if (p[0] == 0x58000050u && p[1] == 0xD61F0200u) return true;

    uint8_t *tramp = tramp_alloc(32);
    if (!tramp) {
        LOGE("inline %s: trampoline mmap failed", tag);
        return false;
    }
    memcpy(tramp, sym, 16);
    auto *t = (uint32_t *)(tramp + 16);
    t[0] = 0x58000050u; /* LDR X16, #8 */
    t[1] = 0xD61F0200u; /* BR X16 */
    *(uint64_t *)(t + 2) = (uint64_t)((uint8_t *)sym + 16);
    __builtin___clear_cache((char *)tramp, (char *)tramp + 32);

    if (!page_protect(sym, 16, PROT_READ | PROT_WRITE | PROT_EXEC) &&
        !page_protect(sym, 16, PROT_READ | PROT_WRITE)) {
        LOGE("inline %s: mprotect write failed errno=%d — skip (no crash)", tag, errno);
        munmap(tramp, 32);
        return false;
    }

    if (orig_out && !*orig_out) *orig_out = tramp;

    p[0] = 0x58000050u;
    p[1] = 0xD61F0200u;
    *(uint64_t *)(p + 2) = (uint64_t)hook;

    page_protect(sym, 16, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)sym, (char *)sym + 16);
    LOGI("inline OK %s @ %p", tag, sym);
    return true;
}
#elif defined(__arm__)
static bool install_inline(void *sym, void *hook, void **orig_out, const char *tag) {
    if (!sym || !hook) return false;
    auto *p = (uint32_t *)sym;
    if (p[0] == 0xE51FF004u) return true;
    uint8_t *tramp = tramp_alloc(16);
    if (!tramp) return false;
    auto *t = (uint32_t *)tramp;
    t[0] = p[0]; t[1] = p[1];
    t[2] = 0xE51FF004u;
    t[3] = (uint32_t)((uint8_t *)sym + 8);
    __builtin___clear_cache((char *)tramp, (char *)tramp + 16);
    if (!page_protect(sym, 8, PROT_READ | PROT_WRITE | PROT_EXEC) &&
        !page_protect(sym, 8, PROT_READ | PROT_WRITE)) {
        LOGE("inline %s: mprotect fail — skip", tag);
        munmap(tramp, 16);
        return false;
    }
    if (orig_out && !*orig_out) *orig_out = tramp;
    p[0] = 0xE51FF004u;
    p[1] = (uint32_t)hook;
    page_protect(sym, 8, PROT_READ | PROT_EXEC);
    __builtin___clear_cache((char *)sym, (char *)sym + 8);
    LOGI("inline OK %s @ %p", tag, sym);
    return true;
}
#else
static bool install_inline(void *, void *, void **, const char *) { return false; }
#endif

/* -------------------- app-lib PLT patch (safer than system .text) -------------------- */

#if defined(__aarch64__)
using Elf_Ehdr = Elf64_Ehdr;
using Elf_Phdr = Elf64_Phdr;
using Elf_Dyn  = Elf64_Dyn;
using Elf_Sym  = Elf64_Sym;
using Elf_Rela = Elf64_Rela;
using Elf_Rel  = Elf64_Rel;
using Elf_Addr = Elf64_Addr;
#define ELF_R_SYM ELF64_R_SYM
#else
using Elf_Ehdr = Elf32_Ehdr;
using Elf_Phdr = Elf32_Phdr;
using Elf_Dyn  = Elf32_Dyn;
using Elf_Sym  = Elf32_Sym;
using Elf_Rela = Elf32_Rela;
using Elf_Rel  = Elf32_Rel;
using Elf_Addr = Elf32_Addr;
#define ELF_R_SYM ELF32_R_SYM
#endif

static int patch_got_sym(uintptr_t base, const char *sym_name, void *hook, void **orig_out) {
    auto *ehdr = (Elf_Ehdr *)base;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) return 0;
    Elf_Phdr *phdr = (Elf_Phdr *)(base + ehdr->e_phoff);
    Elf_Dyn *dyn = nullptr;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_DYNAMIC) {
            dyn = (Elf_Dyn *)(base + phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;
    Elf_Sym *symtab = nullptr;
    const char *strtab = nullptr;
    void *jmprel = nullptr; size_t pltrelsz = 0; int pltrel = DT_RELA;
    for (auto *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB:   symtab = (Elf_Sym *)(base + d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab = (const char *)(base + d->d_un.d_ptr); break;
            case DT_JMPREL:   jmprel = (void *)(base + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_PLTREL:   pltrel = (int)d->d_un.d_val; break;
            default: break;
        }
    }
    if (!symtab || !strtab || !jmprel || !pltrelsz) return 0;
    int n = 0;
    auto slot = [&](Elf_Addr *got) {
        if (!got) return;
        if (!page_protect(got, sizeof(void *), PROT_READ | PROT_WRITE) &&
            !page_protect(got, sizeof(void *), PROT_READ | PROT_WRITE | PROT_EXEC))
            return;
        void *cur = (void *)(uintptr_t)(*got);
        if (cur == hook) return;
        if (orig_out && !*orig_out && cur) *orig_out = cur;
        *got = (Elf_Addr)(uintptr_t)hook;
        n++;
    };
    if (pltrel == DT_RELA) {
        auto *r = (Elf_Rela *)jmprel;
        size_t c = pltrelsz / sizeof(Elf_Rela);
        for (size_t i = 0; i < c; i++) {
            size_t si = ELF_R_SYM(r[i].r_info);
            if (!strcmp(strtab + symtab[si].st_name, sym_name))
                slot((Elf_Addr *)(base + r[i].r_offset));
        }
    } else {
        auto *r = (Elf_Rel *)jmprel;
        size_t c = pltrelsz / sizeof(Elf_Rel);
        for (size_t i = 0; i < c; i++) {
            size_t si = ELF_R_SYM(r[i].r_info);
            if (!strcmp(strtab + symtab[si].st_name, sym_name))
                slot((Elf_Addr *)(base + r[i].r_offset));
        }
    }
    return n;
}

static void patch_app_plts() {
    struct Ctx { std::vector<std::pair<uintptr_t, std::string>> v; };
    Ctx ctx;
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t, void *data) -> int {
        auto *c = (Ctx *)data;
        if (!info->dlpi_name || !info->dlpi_name[0]) return 0;
        /* only app-private libs — system text stays untouched */
        if (!strstr(info->dlpi_name, "/data/app/") &&
            !strstr(info->dlpi_name, "/data/data/") &&
            !strstr(info->dlpi_name, "/data/user/"))
            return 0;
        c->v.emplace_back((uintptr_t)info->dlpi_addr, info->dlpi_name);
        return 0;
    }, &ctx);

    for (auto &p : ctx.v) {
        int n = 0;
        n += patch_got_sym(p.first, "glGetString", (void *)hook_glGetString, (void **)&orig_glGetString);
        n += patch_got_sym(p.first, "eglGetProcAddress", (void *)hook_eglGetProcAddress, (void **)&orig_eglGetProcAddress);
        n += patch_got_sym(p.first, "vkGetPhysicalDeviceProperties", (void *)hook_vkGPDP, (void **)&orig_vkGPDP);
        n += patch_got_sym(p.first, "vkGetInstanceProcAddr", (void *)hook_vkGIPA, (void **)&orig_vkGIPA);
        if (n) LOGI("app PLT +%d in %s", n, p.second.c_str());
    }
}

/* -------------------- try system inline (best-effort) -------------------- */

static void try_system_inline() {
    std::lock_guard<std::mutex> lk(g_hook_mu);
    const char *gles_libs[] = {
        "libGLESv2.so", "libEGL.so",
        "/system/lib64/libGLESv2.so", "/system/lib/libGLESv2.so",
        "/vendor/lib64/libGLESv2.so", "/vendor/lib/libGLESv2.so",
        nullptr
    };
    for (int i = 0; gles_libs[i]; i++) {
        void *h = dlopen(gles_libs[i], RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen(gles_libs[i], RTLD_NOW);
        if (!h) continue;
        void *gl = call_real_dlsym(h, "glGetString");
        if (gl && gl != (void *)hook_glGetString) {
            if (install_inline(gl, (void *)hook_glGetString, (void **)&orig_glGetString, "glGetString"))
                g_gl_inline = true;
        }
        void *egl = call_real_dlsym(h, "eglGetProcAddress");
        if (egl && egl != (void *)hook_eglGetProcAddress) {
            install_inline(egl, (void *)hook_eglGetProcAddress, (void **)&orig_eglGetProcAddress, "eglGetProcAddress");
        }
    }
    const char *vk_libs[] = {
        "libvulkan.so",
        "/system/lib64/libvulkan.so", "/system/lib/libvulkan.so",
        nullptr
    };
    for (int i = 0; vk_libs[i]; i++) {
        void *h = dlopen(vk_libs[i], RTLD_NOW | RTLD_NOLOAD);
        if (!h) h = dlopen(vk_libs[i], RTLD_NOW);
        if (!h) continue;
        void *gp = call_real_dlsym(h, "vkGetPhysicalDeviceProperties");
        if (gp && gp != (void *)hook_vkGPDP) {
            if (install_inline(gp, (void *)hook_vkGPDP, (void **)&orig_vkGPDP, "vkGPDP"))
                g_vk_inline = true;
        }
        void *gipa = call_real_dlsym(h, "vkGetInstanceProcAddr");
        if (gipa && gipa != (void *)hook_vkGIPA) {
            install_inline(gipa, (void *)hook_vkGIPA, (void **)&orig_vkGIPA, "vkGIPA");
        }
    }
}

static void *watcher(void *) {
    prctl(PR_SET_NAME, "iun-hook", 0, 0, 0);
    for (int i = 0; i < 20; i++) {
        usleep(500000);
        if (!g_target) break;
        try {
            patch_app_plts();
            try_system_inline();
        } catch (...) {
            LOGE("watcher exception swallowed");
        }
        if (g_gl_inline && g_vk_inline) break;
    }
    LOGI("watcher done gl_inline=%d vk_inline=%d",
         (int)g_gl_inline.load(), (int)g_vk_inline.load());
    return nullptr;
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
    if (n <= 0 || !g_target) return n;
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
    LOGI("plt %s!%s", lib, sym);
}

static void companion_handler(int fd) {
    std::ifstream f(CONFIG);
    std::string json;
    if (f) json.assign((std::istreambuf_iterator<char>(f)), {});
    uint32_t n = (uint32_t)json.size();
    if (write(fd, &n, sizeof(n)) != (ssize_t)sizeof(n)) return;
    if (n) write(fd, json.data(), n);
    LOGI("companion sent %u", n);
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

        if (api->getFlags() & zygisk::StateFlag::PROCESS_ON_DENYLIST)
            LOGE("DenyList ON for %s — remove it", nice);

        {
            void *h = dlopen("libdl.so", RTLD_NOW);
            if (h) real_dlsym = (void *(*)(void *, const char *))dlsym(h, "dlsym");
        }

        bool got = false;
        int cfd = api->connectCompanion();
        if (cfd >= 0) {
            uint32_t n = 0;
            if (read(cfd, &n, sizeof(n)) == (ssize_t)sizeof(n) && n > 0 && n < 2*1024*1024) {
                std::string json(n, '\0');
                size_t off = 0;
                while (off < n) {
                    ssize_t r = read(cfd, &json[off], n - off);
                    if (r <= 0) break;
                    off += (size_t)r;
                }
                if (off == n) { parse_config_json(json); got = true; }
            }
            close(cfd);
        }
        if (!got) load_config_disk();

        auto it = g_games.find(nice);
        if (it == g_games.end() || !it->second.enabled) {
            LOGI("skip %s", nice);
            env->ReleaseStringUTFChars(args->nice_name, nice);
            return;
        }

        g_target = true;
        g_cfg = it->second;
        LOGI("TARGET %s ren=[%s]", nice, g_cfg.renderer.c_str());

        register_plt(api, "libc.so", "open",   (void *)hook_open,   (void **)&orig_open);
        register_plt(api, "libc.so", "openat", (void *)hook_openat, (void **)&orig_openat);
        register_plt(api, "libc.so", "read",   (void *)hook_read,   (void **)&orig_read);
        register_plt(api, "libdl.so", "dlsym", (void *)hook_dlsym,  (void **)&real_dlsym);

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
        /* resolve originals for fallback */
        if (real_dlsym) {
            void *g = dlopen("libGLESv2.so", RTLD_NOW);
            if (g && !orig_glGetString)
                orig_glGetString = (const char *(*)(unsigned))call_real_dlsym(g, "glGetString");
            void *e = dlopen("libEGL.so", RTLD_NOW);
            if (e && !orig_eglGetProcAddress)
                orig_eglGetProcAddress = (void *(*)(const char *))call_real_dlsym(e, "eglGetProcAddress");
            void *v = dlopen("libvulkan.so", RTLD_NOW);
            if (v) {
                if (!orig_vkGPDP) orig_vkGPDP = (PFN_vkGPDP)call_real_dlsym(v, "vkGetPhysicalDeviceProperties");
                if (!orig_vkGIPA) orig_vkGIPA = (PFN_vkGIPA)call_real_dlsym(v, "vkGetInstanceProcAddr");
            }
        }
        /* layer 2 + 3: app PLT then best-effort system inline */
        patch_app_plts();
        try_system_inline();
        pthread_t th;
        pthread_create(&th, nullptr, watcher, nullptr);
        pthread_detach(th);
        LOGI("post armed");
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(StrongHide)
REGISTER_ZYGISK_COMPANION(companion_handler)
