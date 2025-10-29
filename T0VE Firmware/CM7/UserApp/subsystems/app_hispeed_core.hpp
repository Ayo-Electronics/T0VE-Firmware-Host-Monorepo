/*
 * app_hispeed_core.hpp
 *
 *  Created on: Oct 27, 2025
 *      Author: govis
 */

#pragma once

#include "app_neural_memory.hpp"

class Hispeed_Core {
public:
	//constructor takes in references to our
	Hispeed_Core() {}

	//set up all the execution and monitoring threads
	void init();

	//IMPLEMENTATION NOTE--MAKE SURE WE COPY INPUTS/OUTPUTS AROUND HISPEED LOOP!
	//THIS IS NOT DONE ON THE LOW SPEED SIDE!

private:
	//own a memory helper for access to shared and block memory
	Neural_Memory neural_mem;

};
