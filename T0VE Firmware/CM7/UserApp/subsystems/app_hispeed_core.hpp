/*
 * app_hispeed_core.hpp
 *
 *  Created on: Oct 27, 2025
 *      Author: govis
 */

#pragma once

#include "app_mem_helper.hpp"

class Hispeed_Core {
public:
	//constructor takes in references to our
	Hispeed_Core() {}

	//set up all the execution and monitoring threads
	void init();

private:
	//own a memory helper for access to shared and block memory
	Mem_Helper mem_helper;

};
