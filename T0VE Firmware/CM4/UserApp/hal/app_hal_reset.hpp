/*
 * app_hal_reset.hpp
 *
 *  Created on: Oct 27, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_debug_if.hpp"

class Reset {
public:
	//just a static call operator that performs the system reset
	//can do this via NVIC
	static inline void do_reset() {
		Debug::WARN("Programmatic System Reset Called!");
		NVIC_SystemReset();
	}

private:
	//private constructor prevents instantiating the class
	Reset();
};
