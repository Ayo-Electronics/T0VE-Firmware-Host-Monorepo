/*
 * app_main.cpp
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#include "app_main.hpp"

//only thing using this processor is the `hispeed_core` subsystem
#include "app_hispeed_core.hpp"
#include "app_scheduler.hpp"

//TODO: instantiate hispeed core

void app_init() {
	//TODO: init hispeed core
}

void app_loop() {
	//just run the scheduler in the main loop--all threads will be running in their respective subsystems
	Scheduler::update();
}
