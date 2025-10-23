/*
 * app_threading.cpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#include "app_threading.hpp"
#include "app_hal_tick.hpp"

//==================================================== MUTEX CLASS ======================================================

//claim the mutex if available
void Mutex::LOCK() {
	//spin while mutex is not available
	while (mutex_claimed.test_and_set(std::memory_order_acquire));
}

//release the mutex
void Mutex::UNLOCK() {
	mutex_claimed.clear(std::memory_order_release);
}

//check to see if the mutex is free, claim if it is
bool Mutex::TRY_LOCK() {
	//a neat trick to maintain locked state if mutex is locked
	//but also claim the mutex if unlocked
	//TODO: note about memory ordering
	return !mutex_claimed.test_and_set(std::memory_order_acquire);
}


//==================================================== THREAD SIGNALING CLASS ======================================================

//#### LEAD THREAD ####
//constructor just initializes atomic epoch to 0
Thread_Signal::Thread_Signal(): epoch(0) {}

//leading thread signals all listening threads
void Thread_Signal::signal() {
	//TODO: set the RTOS signal flag

	//and atomically increment the epoch
	//uses std::atomic increment overload;
	//will be a rare instance when increment epoch rolls over
	//likelihood is very low, and severity is also likey low, so ignoring this case for FMEA purposes
	epoch++;
}

//listen to this thread with a thread signal listener
Thread_Signal_Listener Thread_Signal::listen() { return Thread_Signal_Listener(this); }

//get the current epoch (read using overloaded atomic guards)
uint32_t Thread_Signal::get_epoch() const { return epoch.read(); }

//wait for the next signal from the lead thread
Thread_Signal::Wait_Return_t Thread_Signal::wait(uint32_t starting_epoch, uint32_t timeout) {
	//output temporary initialization
	Wait_Return_t wait_return = {};
	wait_return.no_timeout = true;				//default successful wait
	auto& exit_epoch = wait_return.exit_epoch;	//put our working exit epoch directly into our struct

	//early exit if the epochs are already different
	exit_epoch = get_epoch();
	if(starting_epoch != exit_epoch) return wait_return;

	//TODO: RTOS wait on event flags implementation

	//non-RTOS implementation just blocks, checks for timeout
	uint32_t starting_ms = Tick::get_ms();

	//wait for the epoch to differ from the one the caller started with
	while(exit_epoch == starting_epoch) {
		//check the current epoch if it was updated by a different process (i.e. interrupt)
		exit_epoch = get_epoch();

		//if we have a non-zero timeout, check if we've timed out
		if(	(timeout > 0) &&
			((Tick::get_ms() - starting_ms) >= timeout))
		{
			wait_return.no_timeout = false;
			return wait_return;
		}
	}

	//epoch has incremented, i.e. thread has signaled (and we haven't timed out)
	return wait_return;
}

//#### LISTENER ####
Thread_Signal_Listener::Thread_Signal_Listener(Thread_Signal* signal): signal_to_monitor(signal)
{
	//refresh the current epoch upon construction
	refresh();
}

//refresh just pulls the current epoch from the parent
//has an affect of "getting the listener up to speed"
void Thread_Signal_Listener::refresh() {
	if(signal_to_monitor) {
		local_epoch = signal_to_monitor->get_epoch();
	}
}

//non blocking function that checks if state has been updated since last refresh
bool Thread_Signal_Listener::check(bool do_refresh) {
	//early exit if our parent signal isn't valid
	if(signal_to_monitor == nullptr) return false;

	//check if our epochs differ --> implies a change has happened
	uint32_t current_epoch = signal_to_monitor->get_epoch();
	bool update_happened = local_epoch != current_epoch;

	//update the local epoch if we want
	if(do_refresh) local_epoch = current_epoch;

	//and finally return whether an update happened
	return update_happened;
}

//blocking wait function that checks for state change
bool Thread_Signal_Listener::wait(uint32_t timeout_ms, bool do_refresh) {
	//early exit if our parent signal we're monitoring isn't valid
	if(signal_to_monitor == nullptr) return false;

	//forward to the parent thread signal's wait function
	auto return_info = signal_to_monitor->wait(local_epoch, timeout_ms);

	//update from our return struct the epoch we exited our wait with
	//a little extra, but ensures we do our best we don't miss *any* signal event
	if(do_refresh) local_epoch = return_info.exit_epoch;

	//and return if we timed out during our wait
	return return_info.no_timeout;
}


