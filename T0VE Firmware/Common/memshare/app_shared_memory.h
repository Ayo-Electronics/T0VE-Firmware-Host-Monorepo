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

#include "stm32h7xx_hal.h" 	//uintxx_t types
#include "stm32h747xx.h" 	//register definitions

/* NOTE: Many linker scripts expect section names to start with a dot. */
#define SHARED_RAM_VAR    	__attribute__((section(".SHARED_RAM_Section"), aligned(4)))
#define SHARED_EXTMEM_VAR 	__attribute__((section(".EXTMEM_Section"), aligned(4)))
#define SHARED_FASTRAM_VAR 	__attribute__((section(".FAST_SHARED_RAM_Section"), aligned(4)))

#define NETWORK_SIZE  	(8 * 1024 * 1024)
#define INPUTS_SIZE 	32768
#define OUTPUTS_SIZE	32768

struct Shared_RAM_t {
    uint8_t PUBLIC_SHARED_UID[16];
};

struct Shared_EXTMEM_t {
    uint8_t NETWORK[NETWORK_SIZE];
};

struct Shared_FASTRAM_t {
	uint16_t INPUTS[INPUTS_SIZE];
	uint32_t INPUT_MAPPING[INPUTS_SIZE];
	uint16_t OUTPUTS[OUTPUTS_SIZE];
	uint32_t OUTPUT_MAPPING[OUTPUTS_SIZE];
};

/* Declarations only; definitions live in the .c file */
extern struct Shared_RAM_t    	SHARED_MEMORY;
extern struct Shared_EXTMEM_t 	SHARED_EXTMEM;
extern struct Shared_FASTRAM_t	SHARED_FASTMEM;

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
 *	 												CM4 releases this semaphore to signal execution stop (in case of timeout or power fail)
 *	 												The CM7 will clear its output flags after this semaphore goes from HIGH --> LOW
 *	 ARM_FIRE_SUCCESS			CM4 <-- CM7			CM7 asserts this flag if the most recent arm + fire executed without errors;
 *	 												cleared when DO_ARM_FIRE goes low
 *	 ARM_FIRE_ERR_READY			CM4 <-- CM7			CM7 asserts this flag if the most recent arm + fire executed terminated with an issue where ALL_READY
 *	 												went low before execution could complete
 */

enum Sem_Mapping {
	SEM_ARM_FIRE_READY = 0,
	SEM_DO_ARM_FIRE,
	SEM_ARM_FIRE_SUCCESS,
	SEM_ARM_FIRE_ERR_READY,
	SEM_BOOT_SIGNAL,		//EXTRA SEMAPHORE USED DURING BOOT SYNCHRONIZATION
};

#endif /* APP_SHARED_MEMORY_H_ */
