#ifndef WATCHDOG_H
#define WATCHDOG_H

#include "elf_loader.h"
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void watchdog_start(const elf_module_t *mod);
void watchdog_register_thread(HANDLE thread, const char *name);
void watchdog_frame(void);
void watchdog_set_focused(int focused);
void watchdog_describe_address(uint32_t addr, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* WATCHDOG_H */
