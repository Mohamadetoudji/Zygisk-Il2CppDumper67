// rva_writer.h
// C++17, ARM64 Android, MSVC-compatible
// Captures CodeRegistration / MetadataRegistration RVAs at runtime
// and writes them to RVA.TXT beside dump.cs

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <android/log.h>
#include "dobby.h"  // الريبو يستخدم Dobby

#define RVA_TAG "Il2CppRVA"
#define RVAI(...) __android_log_print(ANDROID_LOG_INFO,  RVA_TAG, __VA_ARGS__)
#define RVAE(...) __android_log_print(ANDROID_LOG_ERROR, RVA_TAG, __VA_ARGS__)

// ============================================================
// Global state — يتملى من الهوك
// ============================================================
static uintptr_t g_code_reg_rva = 0;
static uintptr_t g_meta_reg_rva = 0;

// ============================================================
// Base address من libil2cpp.so
// ============================================================
static uintptr_t get_il2cpp_base() {
    FILE* maps = fopen("/proc/self/maps", "r");
    if (!maps) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libil2cpp.so") && strstr(line, "r-xp")) {
            sscanf(line, "%lx", &base);
            break;
        }
    }
    fclose(maps);
    return base;
}

// ============================================================
// Hook على MetadataCache::Initialize
// prototype: void Initialize(const Il2CppCodeRegistration*,
//                            const Il2CppMetadataRegistration*)
// ARM64: x0 = codeReg, x1 = metaReg
// ============================================================
static void (*orig_MetadataCache_Initialize)(const void*, const void*) = nullptr;

static void hook_MetadataCache_Initialize(const void* codeReg,
                                          const void* metaReg) {
    uintptr_t base = get_il2cpp_base();

    if (base && codeReg)
        g_code_reg_rva = (uintptr_t)codeReg - base;

    if (base && metaReg)
        g_meta_reg_rva = (uintptr_t)metaReg - base;

    RVAI("CodeRegistration    RVA: 0x%lx", g_code_reg_rva);
    RVAI("MetadataRegistration RVA: 0x%lx", g_meta_reg_rva);

    orig_MetadataCache_Initialize(codeReg, metaReg);
}

// ============================================================
// تسجيل الهوك — ينادى قبل orig_il2cpp_init
// ============================================================
static void register_metadata_hook() {
    // حاول أول بالسيمبول المانغل — شغال لو الـ symbols مش stripped
    const char* mangled =
        "_ZN2il2cpp2vm13MetadataCache10InitializeEPK20"
        "Il2CppCodeRegistrationPK24Il2CppMetadataRegistration";

    void* fn = nullptr;

    void* handle = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (handle) {
        fn = dlsym(handle, mangled);
        dlclose(handle);
    }

    if (!fn) {
        RVAE("Symbol not found — stripped binary, pattern scan needed");
        // لو stripped: حط باترن تاع إصدار Unity اللعبة هنا
        // fn = pattern_scan(...);
        return;
    }

    DobbyHook(fn,
              (void*)hook_MetadataCache_Initialize,
              (void**)&orig_MetadataCache_Initialize);

    RVAI("MetadataCache::Initialize hooked at %p", fn);
}

// ============================================================
// كتابة RVA.TXT — ينادى بعد رجوع orig_il2cpp_init
// basePath = نفس المسار اللي يكتب فيه dump.cs
// مثال: "/data/data/com.example.game/files/"
// ============================================================
static void write_rva_file(const char* basePath) {
    char path[512];
    snprintf(path, sizeof(path), "%sRVA.TXT", basePath);

    FILE* f = fopen(path, "w");
    if (!f) {
        RVAE("Failed to open %s for writing", path);
        return;
    }

    fprintf(f, "CodeRegistration:     0x%lx\n", g_code_reg_rva);
    fprintf(f, "MetadataRegistration: 0x%lx\n", g_meta_reg_rva);
    fclose(f);

    RVAI("RVA.TXT written → %s", path);
}
