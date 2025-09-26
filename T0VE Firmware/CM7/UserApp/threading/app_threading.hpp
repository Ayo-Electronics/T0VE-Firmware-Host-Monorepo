/*
 * app_threading.hpp
 *	some helpers for multi-threading
 *	NOTES:
 *		- with this specific file, I hope to start using a general API that works for the different types of embedded systems I work on
 *		- The goal is that this API is suitable for all types of multithreaded applications. This includes:
 *			- same-priority user-coordinated scheduling (i.e. using my `Scheduler` class in utils)
 *			- multi-priority interrupt-based scheduling (i.e. different threads run on different timer ISR channels; use the NVIC for priority management)
 *			- full blown RTOS system
 *		- The specific implementation you see in this file is currently for non-RTOS based systems
 *
 *  Created on: Aug 15, 2025
 *      Author: govis
 */

#pragma once

#include <app_proctypes.hpp>

//========================================================== ATOMIC VARIABLE CLASS ===============================================================

//a primitive version of an atomic variable
//use this with systems that aren't running an RTOS
//using a proper mutexed variable would be more suitable for RTOS based applications
template <typename Vartype>
class Atomic_Var {
private:
	Vartype var;
public:
	//make sure the type is trivially copyable
	//this is a requirement for the atomic variable to work properly
	static_assert(std::is_trivially_copyable_v<Vartype>, "Vartype must be trivially copyable");

	//default constructor
	Atomic_Var() : var() {}
	//constructor with initialization
	Atomic_Var(Vartype var_init): var(var_init) {}

	//implement read/write methods
	//as well as overload assignment/cast operator

	void write(const Vartype& new_var) {
		//for this simple implementation, just disable all interrupts, write, then re-enable interrupts
		__disable_irq(); //single instruction, fast
		var = new_var;
		__DSB();  // Data synchronization barrier
		__enable_irq();
	}
	void operator=(const Vartype& new_var) { write(new_var); }

	Vartype read() const {
		//for this simple implementation, just disable all interrupts, read, then re-enable interrupts
		__disable_irq(); //single instruction, fast
		Vartype tmp = var;
		__enable_irq();

		return tmp;
	}
	operator Vartype() const { return read(); }

	//unguarded read for performance
	//if it's guaranteed that only a single process is responsible for updating the atomic variable
	//that same process should be able to atomically read the variable without any guards
	Vartype UNGUARDED_READ() const { return var; }

	//an extra function that's handy for me
	//useful for comparing if the atomic variable is equal to the passed item
	//if it is, no write will be performed, will return true
	//else, write will be performed, will return false
	bool cmp_eq_write(const Vartype& new_var) {
		__disable_irq();
		if(new_var == var) {
			__enable_irq(); //MAKE SURE TO RE-ENABLE INTERRUPTS!
			return true; //comparison is equal, don't perform write
		}
		var = new_var; //comparison is unequal, perform write
		__DSB();  // Data synchronization barrier
		__enable_irq();

		return false;
	}

	//generic `with()` function that lets us perform any kinda atomic operations with the variable
	//the intent here is to pass *capturing lambdas that take a `Vartype` reference as an argument, i.e. for a uin32_t atomic variable `uvar`,
	//your atomic pattern should be something like:
	//uint32_t DEMO_MASK = 0xDEADBEEF
	//uvar.with([&](uint32_t& _uvar) {
	//	_uvar &= DEMO_MASK;
	//});
	//note how I capture everything by *reference* and take a *reference* to the atomic variable
	//this is insanely cool because there's no heap allocation in this pattern and very minimal overhead
	//	\--> pretty much just some variable closure on the stack and a function call
	template <typename Func>
	inline void with(Func&& f) noexcept(noexcept(std::declval<Func&>()(std::declval<Vartype&>()))) {
		__disable_irq();
		f(var);
		__enable_irq();
	}

	//delete the copy constructor and assignment operator
	Atomic_Var(const Atomic_Var& other) = delete;
	void operator=(const Atomic_Var& other) = delete;
};

//==================================================== MUTEX CLASS ======================================================

class Mutex {
public:
	//claim the mutex
	//NOTE: spins in a busy loop while waiting!!!
	void LOCK();

	//release the mutex
	void UNLOCK();

	//perform an operation atomically, in lambda function
	//lambda function should take no arguments
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
	bool check_WITH(Func&& f) noexcept(noexcept(std::declval<Func&>()())) {
		//check if the mutex is available; claim if so
		//fail immediately/return false if not
		if(!AVAILABLE(true)) return false;

		//run the function, then unlock the mutex, then return success
		f();
		UNLOCK();
		return true;
	}

	//check to see if the mutex is free
	//can claim the mutex if it's free
	bool AVAILABLE(bool claim = false);

private:
	//just have a boolean flag that works atomically
	Atomic_Var<bool> mutex_locked;
};

//==================================================== THREAD SIGNALING CLASS ======================================================

//a simple class that allows us to signal between threads
//use this for systems that aren't running an RTOS
//for RTOS based systems, use the RTOS's native signaling system (will be wrapped in this API)
class Thread_Signal {
public:
	//constructor
	Thread_Signal();

	//exposing three functions that allows us general functionality
	void signal(); //called by the asserting thread
	bool wait(bool clear_if_asserted = true, uint32_t timeout_ms = UINT32_MAX); 	//stalls the thread until the signal is asserted -- USE VERY CAREFULLY FOR NON-RTOS SYSTEMS!
																					//returns false if we exited the wait state due to timeout
	bool available(bool clear_if_asserted = true); //checks the state of the signal variable, can clear the signal if it's asserted
	void clear(); //clears the signal variable if asserted

	//delete copy constructor and assignment operator--calls to these would almost certainly be accidental
	void operator=(const Thread_Signal& other) = delete;
	Thread_Signal(const Thread_Signal& other) = delete;
private:
	Atomic_Var<bool> signal_var;
};
