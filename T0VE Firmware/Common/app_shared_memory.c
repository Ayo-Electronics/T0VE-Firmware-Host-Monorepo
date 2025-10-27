
#include "app_shared_memory.h"

/* Put attributes on the definitions; zero-init with {0} in C */
struct Shared_RAM_t SHARED_MEMORY SHARED_RAM_VAR = {
    .PUBLIC_SHARED_UID = {0}
};

struct Shared_EXTMEM_t SHARED_EXTMEM SHARED_EXTMEM_VAR;
