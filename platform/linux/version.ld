/*
 * Linker version script – controls which symbols are exported as dynamic.
 *
 * The game overrides global operator new/delete (to route through its own
 * HeapAlloc/HeapFree with a HeapBlock header).  Without this script the
 * overrides are exported and hijack allocations in shared libraries
 * (libfluidsynth, libmodplug, libicuuc, etc.) that expect standard
 * libstdc++ operator new/delete.  Those libraries may mix malloc/free
 * with new/delete internally, so the HeapBlock pointer arithmetic causes
 * heap corruption and segfaults.
 *
 * Solution: only export "main" (required) and hide everything else.
 * The game's operator new/delete still works for the game's own code
 * (resolved at static link time), but shared libraries see libstdc++'s
 * versions instead.
 */
{
    global:
        main;
    local:
        *;
};
