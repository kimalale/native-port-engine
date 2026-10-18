#ifndef ARM32_BRIDGE_H
#define ARM32_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Register a Windows x86 host function so ARM32 GOT/PLT entries can call it via trap thunk.
 * Returns a synthetic 32-bit trap address (0xF0000000 | (index << 2)). */
uintptr_t arm32_register_host_thunk(const char *name, uintptr_t host_fn);

/* Check if an address is an ARM32->x86 host trap thunk */
int arm32_is_host_thunk(uint32_t addr);

/* Execute an ARM32 / Thumb-2 function at guest_entry with up to 6 32-bit arguments */
uint32_t arm32_call(uintptr_t guest_entry, int argc,
                    uint32_t a0, uint32_t a1, uint32_t a2,
                    uint32_t a3, uint32_t a4, uint32_t a5);

/* Describe the current thread's ARM32 state (guest PC, last host shim, args)
 * for crash reports. Writes an empty string if no guest code is running. */
void arm32_describe_context(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* ARM32_BRIDGE_H */
