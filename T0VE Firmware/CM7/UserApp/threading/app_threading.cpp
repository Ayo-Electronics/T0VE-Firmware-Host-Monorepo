/*
 * app_threading.cpp
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#include "app_threading.hpp"
#include "app_hal_tick.hpp"
//==================================================== ATOMIC VARAIBLE CLASS =======================================================
//nothing here since it's templatized - has to live in the header file

//==================================================== MUTEX CLASS ======================================================

void Mutex::LOCK() {
	//spin until available, claim when available
	while(!AVAILABLE(true));
}

//release the mutex
void Mutex::UNLOCK() {
	mutex_locked = false;
}

//check to see if the mutex is free
bool Mutex::AVAILABLE(bool claim) {
	//if we don't wanna claim the mutex, just read it
	if(!claim) return !mutex_locked;

	//if we do wanna claim it, read/clear it atomically
	//if the the mutex is locked, then the comparison will be true
	//otherwise the comparison will be false, and true will be written to the mutex
	return !mutex_locked.cmp_eq_write(true);
}


//==================================================== THREAD SIGNALING CLASS ======================================================
//constructor; initialize the signal to non-signaled
Thread_Signal::Thread_Signal(): signal_var(false) {}

bool Thread_Signal::wait(bool clear_if_asserted, uint32_t timeout_ms) {
    uint32_t start_time = Tick::get_ms();
    //there's two flavors of busy-wait here--one where we need to clear the flag and one where we don't

    //if we want to clear the flag immediately after it becomes true (i.e. atomically)
    //we wait while the variable is false, i.e. signal_var == false
    //but if it's not false, we'd like to set it to false (and return true)
    //this can be accomplished with the `cmp_eq_write()` function in the `Atomic_Var` class
    if(clear_if_asserted) {
        while(signal_var.cmp_eq_write(false)) {
            if(Tick::get_ms() - start_time > timeout_ms) return false; //check for timeout
        }
    }
    
    //if we don't wanna clear the flag during the busy-wait, just atomically read the signal flag
    else while(!signal_var) {
        if(Tick::get_ms() - start_time > timeout_ms) return false; //check for timeout
    }

    //if we made it here, we waited long enough and the signal was asserted
    return true;
}

bool Thread_Signal::available(bool clear_if_asserted) {
    //if we want to clear the flag immediately after it becomes true (i.e. atomically)
    //we can use the `cmp_eq_write()` function to do this
    if(clear_if_asserted) return !signal_var.cmp_eq_write(false);

    //if we don't want to clear the flag immediately, just atomically read the signal flag
    return (bool)signal_var;
}

void Thread_Signal::signal() { signal_var = true; }
void Thread_Signal::clear() { signal_var = false; }   
