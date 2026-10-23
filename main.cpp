/*
 * iUnlocker GLTool — multi-game GPU/RAM spoof + path hide
 *
 * - libc open/openat/read: Zygisk pltHook in preAppSpecialize
 * - glGetString: own PLT/GOT patcher on a watcher thread (Zygisk API
 *   is dead after specialize; GLES/game libs load later)
 */
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>
#include <algorithm>

#define TAG "iunlocker"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

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
    int  ram_gb  = 8;
    bool props   = true;
    bool paths   = true;
    bool enabled = true;
};

static std::unordered_map<std::string, GpuConfig> g_games;
static GpuConfig g_cfg;
static bool g_target = false;
static volatile bool g_gl_hooked = false;

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
    if (!f) { LOGI("no config"); return; }
    std::string json((std::istreambuf_iterator<char>(f)), {});
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
        LOGI("cfg %s en=%d", pkg.c_str(), (int)g_games[pkg].enabled);
        i = oe;
    }
}

static bool path_blocked(const char *p) {
    if (!p || !g_cfg.paths) return false;
    for (int i = 0; BLOCK_PATHS[i]; i++)
        if (strstr(p, BLOCK_PATHS[i])) return true;
    return false;
}

/* -------------------- libc hooks (zygisk) -------------------- */

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
    char path[64] = {}, real[128] = {};
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
                line.find("lsplt") == std::string::npos &&
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

/* -------------------- GL spoof -------------------- */

static const char *(*orig_glGetString)(unsigned) = nullptr;
static const char *hook_glGetString(unsigned name) {
    switch (name) {
        case 0x1F01: return g_cfg.vendor.c_str();
        case 0x1F00: return g_cfg.renderer.c_str();
        case 0x1F02: return g_cfg.version.c_str();
        case 0x8B8C: return g_cfg.shading.c_str();
        default: break;
    }
    return orig_glGetString ? orig_glGetString(name) : "";
}

/* -------------------- maps / zygisk plt helpers -------------------- */

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
        unsigned maj=0, min=0; unsigned long inode=0, off=0; char perms[8]={};
        if (sscanf(line, "%*s %7s %lx %x:%x %lu", perms, &off, &maj, &min, &inode) >= 5 && inode) {
            *dev = makedev(maj, min); *ino = (ino_t)inode; ok = true; break;
        }
    }
    fclose(f);
    return ok;
}

static void register_plt(Api *api, const char *lib, const char *sym, void *hook, void **orig) {
    if (!api) return;
    dev_t dev=0; ino_t ino=0;
    if (!maps_dev_ino(lib, &dev, &ino)) { LOGI("maps miss %s", lib); return; }
    api->pltHookRegister(dev, ino, sym, hook, orig);
    LOGI("zygisk plt %s!%s", lib, sym);
}

/* -------------------- own PLT/GOT patcher (post-specialize) -------------------- */

struct MapMod {
    uintptr_t start, end;
    std::string path;
};

static std::vector<MapMod> scan_maps() {
    std::vector<MapMod> out;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return out;
    char line[768];
    while (fgets(line, sizeof(line), f)) {
        if (!strstr(line, " r-xp ")) continue;
        char *path = strchr(line, '/');
        if (!path) continue;
        size_t n = strlen(path);
        while (n && (path[n-1]=='\n'||path[n-1]=='\r')) path[--n]=0;
        uintptr_t start=0, end=0;
        if (sscanf(line, "%lx-%lx", &start, &end) != 2) continue;
        out.push_back({start, end, path});
    }
    fclose(f);
    return out;
}

static bool mprotect_rw(void *addr, size_t len) {
    uintptr_t page = (uintptr_t)addr & ~(uintptr_t)0xFFF;
    size_t span = ((uintptr_t)addr + len + 0xFFF - page);
    return mprotect((void *)page, span, PROT_READ | PROT_WRITE) == 0;
}
static bool mprotect_rx(void *addr, size_t len) {
    uintptr_t page = (uintptr_t)addr & ~(uintptr_t)0xFFF;
    size_t span = ((uintptr_t)addr + len + 0xFFF - page);
    return mprotect((void *)page, span, PROT_READ | PROT_EXEC) == 0;
}

#if defined(__aarch64__)
using Elf_Ehdr = Elf64_Ehdr;
using Elf_Phdr = Elf64_Phdr;
using Elf_Dyn  = Elf64_Dyn;
using Elf_Sym  = Elf64_Sym;
using Elf_Rela = Elf64_Rela;
using Elf_Rel  = Elf64_Rel;
using Elf_Addr = Elf64_Addr;
#define ELF_R_SYM ELF64_R_SYM
#define ELF_R_TYPE ELF64_R_TYPE
#elif defined(__arm__)
using Elf_Ehdr = Elf32_Ehdr;
using Elf_Phdr = Elf32_Phdr;
using Elf_Dyn  = Elf32_Dyn;
using Elf_Sym  = Elf32_Sym;
using Elf_Rela = Elf32_Rela;
using Elf_Rel  = Elf32_Rel;
using Elf_Addr = Elf32_Addr;
#define ELF_R_SYM ELF32_R_SYM
#define ELF_R_TYPE ELF32_R_TYPE
#else
#error unsupported arch
#endif

/* Patch GOT entries for symbol in one loaded ELF (base = load bias) */
static int patch_plt_symbol(uintptr_t base, const char *sym_name, void *hook, void **orig_out) {
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
    Elf_Rela *rela = nullptr; size_t relasz = 0;
    Elf_Rel  *rel  = nullptr; size_t relsz  = 0;
    Elf_Addr *jmprel = nullptr; size_t pltrelsz = 0; int pltrel = 0;

    for (auto *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_SYMTAB:   symtab = (Elf_Sym *)(base + d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab = (const char *)(base + d->d_un.d_ptr); break;
            case DT_RELA:     rela = (Elf_Rela *)(base + d->d_un.d_ptr); break;
            case DT_RELASZ:   relasz = d->d_un.d_val; break;
            case DT_REL:      rel = (Elf_Rel *)(base + d->d_un.d_ptr); break;
            case DT_RELSZ:    relsz = d->d_un.d_val; break;
            case DT_JMPREL:   jmprel = (Elf_Addr *)(base + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_PLTREL:   pltrel = (int)d->d_un.d_val; break;
            default: break;
        }
    }
    if (!symtab || !strtab) return 0;

    auto try_slot = [&](Elf_Addr *got) -> int {
        if (!got) return 0;
        if (!mprotect_rw(got, sizeof(void *))) return 0;
        void *cur = (void *)(*got);
        // skip if already our hook
        if (cur == hook) return 0;
        if (orig_out && !*orig_out && cur && cur != hook) *orig_out = cur;
        *got = (Elf_Addr)hook;
        mprotect_rx(got, sizeof(void *));
        return 1;
    };

    int patched = 0;

    // PLT relocations
    if (jmprel && pltrelsz) {
        if (pltrel == DT_RELA) {
            auto *r = (Elf_Rela *)jmprel;
            size_t n = pltrelsz / sizeof(Elf_Rela);
            for (size_t i = 0; i < n; i++) {
                size_t si = ELF_R_SYM(r[i].r_info);
                if (strcmp(strtab + symtab[si].st_name, sym_name) == 0) {
                    patched += try_slot((Elf_Addr *)(base + r[i].r_offset));
                }
            }
        } else {
            auto *r = (Elf_Rel *)jmprel;
            size_t n = pltrelsz / sizeof(Elf_Rel);
            for (size_t i = 0; i < n; i++) {
                size_t si = ELF_R_SYM(r[i].r_info);
                if (strcmp(strtab + symtab[si].st_name, sym_name) == 0) {
                    patched += try_slot((Elf_Addr *)(base + r[i].r_offset));
                }
            }
        }
    }

    // Also scan RELA/REL for absolute relocs to the symbol
    if (rela && relasz) {
        size_t n = relasz / sizeof(Elf_Rela);
        for (size_t i = 0; i < n; i++) {
            size_t si = ELF_R_SYM(rela[i].r_info);
            if (strcmp(strtab + symtab[si].st_name, sym_name) == 0) {
                patched += try_slot((Elf_Addr *)(base + rela[i].r_offset));
            }
        }
    }
    if (rel && relsz) {
        size_t n = relsz / sizeof(Elf_Rel);
        for (size_t i = 0; i < n; i++) {
            size_t si = ELF_R_SYM(rel[i].r_info);
            if (strcmp(strtab + symtab[si].st_name, sym_name) == 0) {
                patched += try_slot((Elf_Addr *)(base + rel[i].r_offset));
            }
        }
    }

    return patched;
}

static bool should_patch_path(const std::string &path) {
    // skip self / linker / zygisk
    if (path.find("zygisk") != std::string::npos) return false;
    if (path.find("magisk") != std::string::npos) return false;
    if (path.find("iunlocker") != std::string::npos) return false;
    if (path.find("/linker") != std::string::npos) return false;
    // patch anything that might call glGetString
    return true;
}


/* -------------------- inline hook on exported symbol -------------------- */

#if defined(__aarch64__)
static bool inline_hook(void *sym, void *hook, void **orig_out) {
    if (!sym || !hook) return false;
    auto *p = (uint32_t *)sym;
    // already hooked?
    if (p[0] == 0x58000050 && p[1] == 0xD61F0200) return true; // LDR X16,#8; BR X16
    // trampoline: save first 16 bytes, jump to orig+16
    static uint8_t tramp_pool[16 * 8];
    static int tramp_i = 0;
    if (tramp_i >= 8) return false;
    uint8_t *tramp = tramp_pool + tramp_i * 16;
    tramp_i++;
    if (!mprotect_rw(tramp, 16)) return false;
    memcpy(tramp, sym, 16);
    // at end of tramp: LDR X16, #8; BR X16; .quad sym+16
    auto *t32 = (uint32_t *)(tramp + 16);
    // we only copied 16 bytes into 16-byte tramp — need a separate absolute jump back
    // simpler: allocate 32-byte tramp
    return false; // fall through to simpler path below
}
#endif

static bool hook_export_gl() {
    void *sym = dlsym(RTLD_DEFAULT, "glGetString");
    if (!sym) {
        // try loading first
        dlopen("libGLESv2.so", RTLD_NOW);
        sym = dlsym(RTLD_DEFAULT, "glGetString");
    }
    if (!sym) {
        LOGI("dlsym glGetString miss");
        return false;
    }
    if (sym == (void *)hook_glGetString) return true;

#if defined(__aarch64__)
    // 16-byte absolute branch stub at sym:
    //   LDR X16, #8
    //   BR  X16
    //   .quad hook
    auto *p = (uint32_t *)sym;
    if (!mprotect_rw(sym, 16)) {
        LOGE("mprotect glGetString failed");
        return false;
    }
    // build a tiny trampoline that executes stolen bytes then jumps back
    static uint32_t tramp[8];
    static bool tramp_ready = false;
    if (!tramp_ready) {
        mprotect_rw(tramp, sizeof(tramp));
        // copy first 4 instructions (16 bytes)
        memcpy(tramp, p, 16);
        // LDR X16, #8 ; BR X16 ; .quad (sym+16)
        tramp[4] = 0x58000050;
        tramp[5] = 0xD61F0200;
        *(uint64_t *)(tramp + 6) = (uint64_t)((uint8_t *)sym + 16);
        mprotect_rx(tramp, sizeof(tramp));
        tramp_ready = true;
        if (!orig_glGetString)
            orig_glGetString = (const char *(*)(unsigned))(void *)tramp;
    }
    p[0] = 0x58000050; // LDR X16, #8
    p[1] = 0xD61F0200; // BR X16
    *(uint64_t *)(p + 2) = (uint64_t)hook_glGetString;
    mprotect_rx(sym, 16);
    LOGI("inline hooked glGetString @ %p", sym);
    g_gl_hooked = true;
    return true;
#elif defined(__arm__)
    // ARM 32: LDR PC, [PC, #-4]; .word hook
    auto *p = (uint32_t *)sym;
    if (!mprotect_rw(sym, 8)) return false;
    static uint32_t tramp[4];
    static bool tramp_ready = false;
    if (!tramp_ready) {
        mprotect_rw(tramp, sizeof(tramp));
        tramp[0] = p[0];
        tramp[1] = p[1];
        tramp[2] = 0xE51FF004; // LDR PC, [PC, #-4]
        tramp[3] = (uint32_t)((uint8_t *)sym + 8);
        mprotect_rx(tramp, sizeof(tramp));
        tramp_ready = true;
        if (!orig_glGetString)
            orig_glGetString = (const char *(*)(unsigned))(void *)tramp;
    }
    p[0] = 0xE51FF004;
    p[1] = (uint32_t)hook_glGetString;
    mprotect_rx(sym, 8);
    LOGI("inline hooked glGetString @ %p", sym);
    g_gl_hooked = true;
    return true;
#else
    return false;
#endif
}


static void patch_all_gl() {
    auto mods = scan_maps();
    int total = 0;
    std::unordered_set<uintptr_t> done;
    for (auto &m : mods) {
        if (!should_patch_path(m.path)) continue;
        if (done.count(m.start)) continue;
        done.insert(m.start);
        // ELF header must be readable at start of mapping — for some libs
        // the first r-xp page is the text; ehdr is at file offset 0 which is
        // often a separate r--p mapping. Find the lowest mapping for this path.
        uintptr_t base = m.start;
        for (auto &o : mods) {
            if (o.path == m.path && o.start < base) base = o.start;
        }
        // verify ELF magic at base
        if (base < 0x1000) continue;
        auto *p = (const unsigned char *)base;
        // may fault if not mapped readable — rely on r-xp which is readable
        if (p[0] != 0x7f || p[1] != 'E' || p[2] != 'L' || p[3] != 'F') {
            // try scanning for load bias via other means: skip
            continue;
        }
        int n = patch_plt_symbol(base, "glGetString", (void *)hook_glGetString, (void **)&orig_glGetString);
        n += patch_plt_symbol(base, "eglQueryString", (void *)hook_glGetString, (void **)&orig_glGetString);
        if (n) {
            LOGI("patched %d slots in %s", n, m.path.c_str());
            total += n;
        }
    }
    if (total > 0) {
        g_gl_hooked = true;
        LOGI("gl total patched slots=%d", total);
    }
}

static void *gl_watcher(void *) {
    for (int i = 0; i < 60; i++) {
        usleep(500000);
        if (hook_export_gl()) break;
        patch_all_gl();
        if (g_gl_hooked) break;
    }
    for (int i = 0; i < 30; i++) {
        usleep(1000000);
        hook_export_gl();
        patch_all_gl();
    }
    return nullptr;
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

        uint32_t flags = api->getFlags();
        if (flags & zygisk::StateFlag::PROCESS_ON_DENYLIST) {
            LOGE("DenyList ON for %s — remove game from DenyList or hooks never run", nice);
        }

        static bool cfg_once = false;
        if (!cfg_once) { load_config(); cfg_once = true; }

        auto it = g_games.find(nice);
        if (it != g_games.end() && it->second.enabled) {
            g_target = true;
            g_cfg = it->second;
            LOGI("target %s renderer=[%s] ram=%d", nice, g_cfg.renderer.c_str(), g_cfg.ram_gb);

            register_plt(api, "libc.so", "open",   (void *)hook_open,   (void **)&orig_open);
            register_plt(api, "libc.so", "openat", (void *)hook_openat, (void **)&orig_openat);
            register_plt(api, "libc.so", "read",   (void *)hook_read,   (void **)&orig_read);
            bool ok = api->pltHookCommit();
            LOGI("libc pltHookCommit=%d", (int)ok);
        }

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
            __system_property_set("ro.vendor.boot.warranty_bit", "0");
            __system_property_set("sys.oem_unlock_allowed", "0");
            __system_property_set("ro.boot.veritymode", "enforcing");
        }
        pthread_t th;
        pthread_create(&th, nullptr, gl_watcher, nullptr);
        pthread_detach(th);
        LOGI("post: watcher started");
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(StrongHide)
