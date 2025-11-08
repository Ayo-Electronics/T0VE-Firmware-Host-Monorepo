/*
 * app_hal_hsem.hpp
 *
 *  Created on: Oct 24, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_registers.hpp"

class Hard_Semaphore {
public:

	//====================== TYPEDEFS ======================
	//right now, just an enum to select which semaphore channel to use
	//then grab the register addresses from HAL
	enum HSem_Channel {
		HSEM_CHANNEL_0 = 0,
		HSEM_CHANNEL_1, 	HSEM_CHANNEL_2, 	HSEM_CHANNEL_3, 	HSEM_CHANNEL_4,
		HSEM_CHANNEL_5, 	HSEM_CHANNEL_6, 	HSEM_CHANNEL_7, 	HSEM_CHANNEL_8,
		HSEM_CHANNEL_9, 	HSEM_CHANNEL_10, 	HSEM_CHANNEL_11,	HSEM_CHANNEL_12,
		HSEM_CHANNEL_13, 	HSEM_CHANNEL_14, 	HSEM_CHANNEL_15, 	HSEM_CHANNEL_16,
		HSEM_CHANNEL_17, 	HSEM_CHANNEL_18, 	HSEM_CHANNEL_19, 	HSEM_CHANNEL_20,
		HSEM_CHANNEL_21, 	HSEM_CHANNEL_22, 	HSEM_CHANNEL_23, 	HSEM_CHANNEL_24,
		HSEM_CHANNEL_25, 	HSEM_CHANNEL_26, 	HSEM_CHANNEL_27, 	HSEM_CHANNEL_28,
		HSEM_CHANNEL_29, 	HSEM_CHANNEL_30, 	HSEM_CHANNEL_31
	};

	//===================== PUBLIC FUNCTIONS ==================
	//constructor takes a hardware semaphore channel
	Hard_Semaphore(HSem_Channel channel);

	//have an init function that sets up clocking and gets the calling CPU's core ID
	void init();

	//expose mutex-style functions --> inlining for speed
	__attribute__((always_inline)) inline void LOCK() {
		//busy-loop until we can claim the mutex
		while(!TRY_LOCK()) {
			__NOP();	//cooldown for a little, relax AHB traffic
		}
	}

	__attribute__((always_inline)) inline void UNLOCK() {
		__DMB();	//ensure all writes complete before unlocking
		READ_CLEAR_REGISTER = (CORE_ID & 0x0F) << HSEM_R_COREID_Pos; //PROC ID not used in our schema
	}

	__attribute__((always_inline)) inline bool TRY_LOCK() {	//true if claimed, false if busy
		//check that the RLR register is now claimed by us (and that it's locked)
		//claiming via the RLR register sets PROCID to 0, so no need to compare those bits
		return TAKE_REGISTER.read() == (HSEM_RLR_LOCK | ((CORE_ID & 0x0F) << HSEM_R_COREID_Pos));
	}

	//perform an operation atomically, in lambda function
	//lambda function should take no arguments
	//BLOCKS if mutex isn't available
	template <typename Func>
	void WITH(Func&& f) noexcept(noexcept(std::declval<Func&>()())) {
		LOCK();
		f();
		UNLOCK();
	}

	//perform an operation atomically, in lambda function
	//lambda function should take no arguments
	//performs a mutex availability check before claiming
	template <typename Func>
	bool TRY_WITH(Func&& f) noexcept(noexcept(std::declval<Func&>()())) {
		//check if the mutex is available; claim if so
		//fail immediately/return false if not
		if(!TRY_LOCK()) return false;

		//run the function, then unlock the mutex, then return success
		f();
		UNLOCK();
		return true;
	}

	//another function that just checks whether the semaphore is locked
	//useful for using semaphores as atomic flags rather than mutexes
	__attribute__((always_inline)) inline bool READ() {
		return READ_CLEAR_REGISTER.read() & HSEM_R_LOCK;
	}

	//delete the copy constructor and assignment operator
	Hard_Semaphore(const Hard_Semaphore& other) = delete;
	void operator=(const Hard_Semaphore& other) = delete;

private:
	//need the calling CPU's Core ID to check and release
	uint32_t CORE_ID = UINT32_MAX;	//invalid coreID until init

	//registers to read/write to the semaphore
	Register<> TAKE_REGISTER;
	Register<> READ_CLEAR_REGISTER;
};
