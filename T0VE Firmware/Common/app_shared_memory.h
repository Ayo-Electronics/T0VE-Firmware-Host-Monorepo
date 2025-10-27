/*
 * app_shared_memory.hpp
 *
 *  Created on: Oct 24, 2025
 *      Author: govis
 *
 * C-style structs for shared memory definitions across both cores
 * No access control is guaranteed here, but just physical memory addresses
 */

#ifndef APP_SHARED_MEMORY_H_
#define APP_SHARED_MEMORY_H_

#include "app_proctypes.hpp"

/* NOTE: Many linker scripts expect section names to start with a dot. */
#define SHARED_RAM_VAR    __attribute__((section(".SHARED_RAM_Section"), aligned(4)))
#define SHARED_EXTMEM_VAR __attribute__((section(".EXTMEM_Section"), aligned(4)))

#define NETWORK_SIZE  (8 * 1024 * 1024)

struct Shared_RAM_t {
    uint8_t PUBLIC_SHARED_UID[16];
};

struct Shared_EXTMEM_t {
    uint8_t NETWORK[NETWORK_SIZE];
};

/* Declarations only; definitions live in the .c file */
extern struct Shared_RAM_t    SHARED_MEMORY;
extern struct Shared_EXTMEM_t SHARED_EXTMEM;

//=================== SEMAPHORE CHANNELS =================
/*
 * For inter-core communication regarding the hispeed code execution, we need to share some state across cores
 * Here's how I'm imagining the signals go
 * 		SIGNAL_NAME				DIRECTION			DESCRIPTION
 *	 =============================================================
 *	 ARM_FIRE_READY				CM4 <-- CM7			CM7 asserts this line if it's idle and ready to fire. This flag will be cleared after the
 *	 												network is finished executing and and the report flags are read
 *	 DO_ARM_FIRE				CM4	-->	CM7			Once the CM4 has set up the network configuration and inputs, updates state variables,
 *	 												and locks memory/peripheral access, it takes this semaphore.
 *	 												The CM7 will clear its output flags after this semaphore goes from HIGH --> LOW
 *	 IMMEDIATE_PGOOD			CM4 --> CM7			CM4 reports the power-good status on this line, forwarding from the power monitor
 *	 												CM7 will read this flag and exit early if power goes bad (asserting the appropriate error exit code)
 *	 ARM_FIRE_SUCCESS			CM4 <-- CM7			CM7 asserts this flag if the most recent arm + fire executed without errors;
 *	 												cleared when DO_ARM_FIRE goes low
 *	 ARM_FIRE_ERR_PWR			CM4 <-- CM7			CM7 asserts this flag if the most recent arm + fire executed terminated with an issue with power,
 *	 												i.e. IMMEDIATE_PGOOD went down during the execution; cleared when DO_ARM_FIRE goes low
 *	 ARM_FIRE_ERR_SYNC			CM4 <-- CM7			CM7 asserts this flag if the most recent arm + fire executed terminated with a timeout issue
 *	 												waiting for a SYNC signal; cleared when DO_ARM_FIRE goes low
 *	 ARM_FIRE_ERR_READY			CM4 <-- CM7			CM7 asserts this flag if the most recent arm + fire executed terminated with a timeout issue
 *	 												waiting for all nodes to be ready; cleared when DO_ARM_FIRE goes low
 */

enum Sem_Mapping {
	SEM_ARM_FIRE_READY = 0,
	SEM_DO_ARM_FIRE,
	SEM_IMMEDIATE_PGOOD,
	SEM_ARM_FIRE_SUCCESS,
	SEM_ARM_FIRE_ERR_PWR,
	SEM_ARM_FIRE_ERR_SYNC,
	SEM_ARM_FIRE_ERR_READY
};

#endif /* APP_SHARED_MEMORY_H_ */
