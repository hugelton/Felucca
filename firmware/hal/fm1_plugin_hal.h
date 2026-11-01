/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM-1 plugin engines (src/plugin_fm1.c): their code runs in place in the flash after the app's area, which the PC
 * limit (felucca-bsp chip/fm1_guard.h: window 0 [0x02000120, _etext], window 1 .ram_text) does not let run. Once a
 * plugin is to be called, window 0 goes up to hi (the end of the plugins' area): the app's code and the plugins'. */
#pragma once
#include "fm1_guard.h"

static void fm1_pc_code_up_to(uint32_t hi)
{
    fm1__dbg_unlock();
    FM1_PC_LIMIT0_H = hi;
    fm1__dbg_lock();
}
