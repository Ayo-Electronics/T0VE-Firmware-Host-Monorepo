
#include "app_shared_memory.h"

//D1 SRAM
struct Shared_FASTRAM_t SHARED_FASTMEM SHARED_FASTRAM_VAR = {
	.INPUTS = {0},
	.INPUT_MAPPING = {0},
	.OUTPUTS = {0},
	.OUTPUT_MAPPING = {0},
};

//D3 SRAM
struct Shared_RAM_t SHARED_MEMORY SHARED_RAM_VAR = {
    .PUBLIC_SHARED_UID = {0}
};

//SHARED DRAM
//don't try to zero-init; memory controller needs to be set up
struct Shared_EXTMEM_t SHARED_EXTMEM SHARED_EXTMEM_VAR;

