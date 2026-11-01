/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The desktop builds' plugin loader: every shared library in a folder (.so, .dylib, .dll) that exports
 * felucca_plugin() (Felucca-Engines' felucca_plugin.h) is opened and handed to the firmware (fh_plugin_add,
 * firmware/src/plugin.c) before it boots. Included after platforms/host/felucca_host.c. A library that is not a plugin of
 * this build is closed again and named on stderr; the libraries taken stay open for the program's life.
 *
 * A plugin of its own (FE_NEW) gets an engine number (ENGI_PLUG0 and up) the first time it is seen, kept for its id in
 * a text file beside the flash (felucca_plugins.txt: a line "ID NUMBER" each, the id in hex): the sounds saved with it
 * find it again, and a number once given is never given to another plugin. */
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <dlfcn.h>
#endif

#define PLG_REG_MAX (ENGI_UNKNOWN - ENGI_PLUG0)
static struct { uint32_t id, no; } plg_reg[PLG_REG_MAX];
static uint32_t plg_nreg;
static const char *plg_reg_path;

static void plugins_reg_load(const char *path)
{
    FILE *f = fopen(path, "r");
    unsigned id, no;
    plg_reg_path = path;
    plg_nreg = 0;
    if (!f)
        return;
    while (plg_nreg < PLG_REG_MAX && fscanf(f, "%x %u", &id, &no) == 2)
        if (no >= ENGI_PLUG0 && no < ENGI_UNKNOWN) {
            plg_reg[plg_nreg].id = id;
            plg_reg[plg_nreg++].no = no;
        }
    fclose(f);
}

/* the number of plugin id: the one kept, else the lowest no plugin had (kept in the file), 0 = none left */
static uint32_t plugins_number(uint32_t id)
{
    uint32_t k, no;
    FILE *f;
    for (k = 0; k < plg_nreg; k++)
        if (plg_reg[k].id == id)
            return plg_reg[k].no;
    for (no = ENGI_PLUG0; no < ENGI_UNKNOWN; no++) {
        for (k = 0; k < plg_nreg && plg_reg[k].no != no; k++)
            ;
        if (k == plg_nreg)
            break;
    }
    if (no == ENGI_UNKNOWN || plg_nreg == PLG_REG_MAX)
        return 0;
    plg_reg[plg_nreg].id = id;
    plg_reg[plg_nreg++].no = no;
    if (plg_reg_path && (f = fopen(plg_reg_path, "w"))) {
        for (k = 0; k < plg_nreg; k++)
            fprintf(f, "%08x %u\n", (unsigned)plg_reg[k].id, (unsigned)plg_reg[k].no);
        fclose(f);
    } else
        fprintf(stderr, "felucca: cannot write %s: plugin numbers not kept\n", plg_reg_path ? plg_reg_path : "(none)");
    return no;
}

/* the numbers kept, for the firmware (plugin.c plg_reg_id / plg_reg_no: a project's plugin found by its id) */
static uint32_t plugins_reg_id(uint32_t no)
{
    uint32_t k;
    for (k = 0; k < plg_nreg; k++)
        if (plg_reg[k].no == no)
            return plg_reg[k].id;
    return 0;
}
static uint32_t plugins_reg_no(uint32_t id)
{
    uint32_t k;
    for (k = 0; k < plg_nreg; k++)
        if (plg_reg[k].id == id)
            return plg_reg[k].no;
    return 0;
}

static int plugins_ext_ok(const char *name)
{
    const char *d = strrchr(name, '.');
    return d && (!strcmp(d, ".so") || !strcmp(d, ".dylib") || !strcmp(d, ".dll"));
}

/* one library: 1 taken, 0 not */
static int plugins_open(const char *path)
{
    static const char *const WHY[] = {"", "not a plugin of this build", "its engine number cannot be replaced",
                                      "no free slot", "the firmware has booted"};
    fe_entry_t f;
    const fe_plugin_t *d;
    uint32_t r;
#if defined(_WIN32)
    HMODULE h = LoadLibraryA(path);
    if (!h) {
        fprintf(stderr, "felucca: plugin %s: cannot open\n", path);
        return 0;
    }
    f = (fe_entry_t)(void (*)(void))GetProcAddress(h, FE_ENTRY);
#else
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "felucca: plugin %s: %s\n", path, dlerror());
        return 0;
    }
    f = (fe_entry_t)dlsym(h, FE_ENTRY);
#endif
    d = f ? f((const fe_host_t *)fh_plugin_host()) : 0;
    r = d ? fh_plugin_add(d, d->engine == FE_NEW ? plugins_number(d->id) : 0u) : 1u;
    if (r) {
        fprintf(stderr, "felucca: plugin %s: %s\n", path, WHY[r < 5u ? r : 1u]);
#if defined(_WIN32)
        FreeLibrary(h);
#else
        dlclose(h);
#endif
        return 0;
    }
    fprintf(stderr, "felucca: plugin %s: %s (version %u) as engine %u\n", path, d->e->name, (unsigned)d->version,
            (unsigned)(d->engine == FE_NEW ? plugins_number(d->id) : d->engine));
    return 1;
}

/* the plugins of folder dir (none there: 0), their numbers kept in file reg; returns the number taken */
static int plugins_load(const char *dir, const char *reg)
{
    char path[1024];
    int n = 0;
    plugins_reg_load(reg);
    plg_reg_id = plugins_reg_id;
    plg_reg_no = plugins_reg_no;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(path, sizeof path, "%s\\*", dir);
    if ((h = FindFirstFileA(path, &fd)) == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (plugins_ext_ok(fd.cFileName)) {
            snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
            n += plugins_open(path);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dp = opendir(dir);
    struct dirent *de;
    if (!dp)
        return 0;
    while ((de = readdir(dp)))
        if (plugins_ext_ok(de->d_name)) {
            snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
            n += plugins_open(path);
        }
    closedir(dp);
#endif
    return n;
}
