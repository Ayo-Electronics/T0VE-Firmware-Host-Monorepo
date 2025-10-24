/*
 * app_main.cpp
 *
 *  Created on: Sep 12, 2023
 *      Author: Ishaan
 */

#include "app_main.hpp"

#include "app_scheduler.hpp"

void app_init() {
	//nothing to initialize for now
}

void app_loop() {
	//just run the scheduler in the main loop--all threads will be running in their respective subsystems
	Scheduler::update();
}
