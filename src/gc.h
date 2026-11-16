#pragma once
#include <stdbool.h>

void gc_switch_card(void);
void gc_init(void);
bool gc_task(void);
void gc_deinit(void);
/* Stop card emulation and write every pending change to the SD (before USB drive mode). */
void gc_stop_for_usb(void);
