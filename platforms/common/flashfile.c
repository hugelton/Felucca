/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The flash in a file (after felucca_host.c): the 1 MiB NOR image read before boot, written a second after the device's
 * last change and when the program ends (through a temporary file and a rename: never half written). Every desktop
 * frontend's file is the same format (felucca_flash.bin): copy it between them. */
#include <stdio.h>

typedef struct {
    char path[1024];
    uint32_t saved, changed_ms;          /* fh_flash_writes at the last save; when the first unsaved one came */
} flashfile_t;

/* the image from path (none there: a new, erased one); returns the bytes read, -1 = none */
static long flashfile_load(flashfile_t *f, const char *path)
{
    FILE *fp;
    long n = -1;
    snprintf(f->path, sizeof f->path, "%s", path);
    fh_nor_erase();
    if ((fp = fopen(f->path, "rb"))) {
        n = (long)fread(fh_nor(), 1, fh_nor_size(), fp);
        fclose(fp);
    }
    f->saved = fh_flash_writes();
    f->changed_ms = 0;
    return n;
}

/* 0 written, -1 not */
static int flashfile_save(flashfile_t *f)
{
    char tmp[1100];
    FILE *fp;
    if (!f->path[0])
        return -1;
    snprintf(tmp, sizeof tmp, "%s.tmp", f->path);
    fp = fopen(tmp, "wb");
    if (!fp || fwrite(fh_nor(), 1, fh_nor_size(), fp) != fh_nor_size() || fclose(fp) != 0 || rename(tmp, f->path) != 0) {
        fprintf(stderr, "felucca: cannot write %s\n", f->path);
        return -1;
    }
    f->saved = fh_flash_writes();
    return 0;
}

/* once a frame: a second after the last change, the file written */
static void flashfile_poll(flashfile_t *f)
{
    if (fh_flash_writes() == f->saved)
        return;
    if (!f->changed_ms)
        f->changed_ms = fh_now_ms();
    if (fh_now_ms() - f->changed_ms >= 1000u) {
        flashfile_save(f);
        f->changed_ms = 0;
    }
}
