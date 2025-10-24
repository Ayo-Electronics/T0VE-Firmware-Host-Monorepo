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

#include <atomic>

#include "app_proctypes.hpp"
#include "app_hal_tick.hpp"	//for threading timeouts
#include "app_debug_if.hpp"

//========================================================== ATOMIC VARIABLE CLASS ===============================================================

//atomic variable that wraps std::atomic<T>
//use for primitive types that operate lock-free
//otherwise can run into some subtle deadlock situations that are gnarly to debug
//i.e. for types that require locking, can be manipulating data in "app" context with lock acquired
//if in interrupt context this data is manipulated, a deadlock can happen upon acquisition of the type
template <typename Vartype>
class Atomic_Var {
	//sanity check that the particular type is lock free and trivially copyable
    static_assert(std::atomic<Vartype>::is_always_lock_free, "Atomic type is not lock-free; use a different scheme");
    static_assert(std::is_trivially_copyable_v<Vartype>, "Atomic type must be trivially copyable");

private:
    //wrap around std::atomic
    std::atomic<Vartype> atomic_var{};

public:
    Atomic_Var() = default;
    Atomic_Var(Vartype init) : atomic_var(init) {}

    //read and writes forward to the std::atomic class
    void write(const Vartype& v) noexcept {
        atomic_var.store(v, std::memory_order_release);
    }

    Vartype read() const noexcept {
        return atomic_var.load(std::memory_order_acquire);
    }

    //general purpose `with` class to perform read-modify-write operations
    //uses the compare-and-swap loop pattern for lock-free variables
    //the intent here is to pass *capturing lambdas that take a `Vartype` reference as an argument, i.e. for a uin32_t atomic variable `uvar`,
	//your atomic pattern should be something like:
	//uint32_t DEMO_MASK = 0xDEADBEEF
	//uvar.with([&](uint32_t& _uvar) {
	//	_uvar &= DEMO_MASK;
	//});
	//note how I capture everything by *reference* and take a *reference* to the atomic variable
	//this is insanely cool because there's no heap allocation in this pattern and very minimal overhead
	//	\--> pretty much just some variable closure on the stack and a function call
    template <class Func>
    Vartype with(Func&& f) noexcept(noexcept(std::declval<Func&>()(std::declval<Vartype&>()))) {
    	//initialize the `old` variable by reading the atomic
        Vartype old = atomic_var.load(std::memory_order_relaxed);

        //compare-and-swap loop
        for (;;) {
        	//apply the lambda function we passed in
            Vartype desired = old;                 // start from what you saw
            std::forward<Func>(f)(desired);        // mutate a local copy

            //try to swap the old value with our new one we calculated
            //if the old value isn't the one we computed with, rerun the calculations using the new old value
            //(`old` is updated via reference in `compare_exchange_weak`)
            if (atomic_var.compare_exchange_weak(old, desired,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                return desired;
            }
        }
    }

    // Prefer the built-ins when available for add/sub
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U fetch_add(U arg, std::memory_order order = std::memory_order_acq_rel) noexcept {
        return atomic_var.fetch_add(arg, order);
    }
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U fetch_sub(U arg, std::memory_order order = std::memory_order_acq_rel) noexcept {
        return atomic_var.fetch_sub(arg, order);
    }

    // Optional operator helpers (match std::atomic semantics)
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U operator++() noexcept { return fetch_add(1) + 1; }      // prefix: returns new
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U operator++(int) noexcept { return fetch_add(1); }       // postfix: returns old
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U operator--() noexcept { return fetch_sub(1) - 1; }
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U operator--(int) noexcept { return fetch_sub(1); }

    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U operator+=(U v) noexcept { return fetch_add(v) + v; }   // return new
    template <class U = Vartype, std::enable_if_t<std::is_integral_v<U>, int> = 0>
    U operator-=(U v) noexcept { return fetch_sub(v) - v; }

    // Disable copying of the wrapper; allow assignment from value via write()
    Atomic_Var(const Atomic_Var&) = delete;
    Atomic_Var& operator=(const Atomic_Var&) = delete;

    // Avoid implicit reads
    // explicit operator Vartype() const = delete;
};

//==================================================== MUTEX CLASS ======================================================
//for non-RTOS applications, wrap around an atomic bool
//be careful using the mutex in ISR context
//	\--> only TRY_LOCK is acceptable for ISR context; make sure failure path is okay
//signals to defer ISR completion or data sharing to thread context
//std::mutex class isn't supported with reduced C-library that ships with STM32

class Mutex {
public:
	//just use the emtpy default constructor
	Mutex() {}

	//claim the mutex
	//NOTE: spins in a busy loop while waiting!!!
	void LOCK();

	//release the mutex
	void UNLOCK();

	//try to claim the mutex; return false if not available
	bool TRY_LOCK();

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

private:
	//for non-RTOS builds, use an atomic flag as a mutex type
	std::atomic_flag mutex_claimed = ATOMIC_FLAG_INIT;
};

//==================================================== THREAD SIGNALING CLASS ======================================================

//forward declarations for both classes to work
class Thread_Signal;
class Thread_Signal_Listener;

//signaling side of the signal-listener pair
//signal() function is ISR and Thread safe
class Thread_Signal {
public:
	//intentionally leak with these factory functions
	//only way to guarantee program lifetime
	static Thread_Signal& mk() { return *new Thread_Signal(); }

	//exposing three functions that allows us general functionality
	//sets a signal flag and increments the atomic epoch
	void signal();		//called by the asserting thread

	//function that spawns a listener to this particular signal
	//can be an arbitrary number of these
	Thread_Signal_Listener listen();

	//and get the epoch to check for any signals that have happened since the last refresh for listeners
	//not typically useful for downstream users, but can expose with no harm
	uint32_t get_epoch() const;

	//delete copy constructor and assignment operator--calls to these would almost certainly be accidental
	void operator=(const Thread_Signal& other) = delete;
	Thread_Signal(const Thread_Signal& other) = delete;
	~Thread_Signal() = delete;
private:
	//private constructor - make using factory function
	Thread_Signal();

	//private wait function that's only gonna be accessible by listeners
	//WILL DIFFER BETWEEN RTOS AND NON-RTOS BUILDS
	friend class Thread_Signal_Listener;
	struct Wait_Return_t {
		bool no_timeout;
		uint32_t exit_epoch;
	};
	Wait_Return_t wait(uint32_t starting_epoch, uint32_t timeout_ms = 0);

	//epoch will be present for RTOS and Non-RTOS versions
	//used for polling-based checking approaches
	Atomic_Var<uint32_t> epoch;

	//and we'll have an actual signal variable that's used for
	//RTOS-based non-polling waits
	//will always clear the signal flag upon wait
	//TODO: fill in using correct RTOS API
};

//listener side of the signaling-listener pair
//all functions are ISR/Thread safe except wait() --> only thread safe
class Thread_Signal_Listener {
public:
	//instantiate a listener by passing the thread signal we're listening to
	Thread_Signal_Listener(Thread_Signal* signal);
	//and default construct with a nullptr
	Thread_Signal_Listener(): signal_to_monitor(nullptr) {}

	//the intended use case of the listener is to monitor whether the thread signal has been asserted since
	//listener object construction or the last time we "checked" (using this word colloquially, no reference to the function below).
	//there may be instances where we want to say "yeah I acknowledge the event flag may have been set before,
	//...but get me up to speed with what it is now". After a `refresh` call, any successive calls
	//to `check()` and `wait()` are guaranteed to return false and stall, unless a signal has happened between
	//the call to `refresh` and the particular waiting function
	void refresh();

	//non-blocking "check" function that checks to see if the signal has been asserted since last refresh
	//or object construction (does this via non-blocking epoch check)
	bool check(bool do_refresh = true);	//set refresh to false if we want to stay asserted

	//and a blocking function that waits until the signal is asserted
	//for RTOS based systems, this actually uses RTOS event flags to suspend the currently executing thread
	//	\--> with a little additional logic that uses the epoch system to bypass the wait if we have an update already pending
	//for non-RTOS based systems, this is just a busy loop that waits for the state to change via epoch
	//set the `refresh` flag if we want to update the local copy of the epoch after the state changes
	bool wait(uint32_t timeout_ms = 0, bool do_refresh = true);	//returns true if signaled, false if timeout
private:
	//maintain a reference to the original thread signal we're monitoring
	Thread_Signal* signal_to_monitor;

	//for some of the signal checking, maintain a local copy of the epoch
	//it's guaranteed that writes to this local epoch will be mutually exclusive
	//to reads of this local epoch; don't need to make this atomic
	//(though aligned uint32_t on ARM processors typically is)
	uint32_t local_epoch;
};

//=========================================== PUB/SUB SYSTEM VARIABLES =========================================

/*
 * multi-writer, multiple reader shared variable
 * typically used for publish-subscribe data sharing models across threads/ISRs
 * 	\--> there's a bit of extra bloat involved with this data sharing setup, so don't use this for critical ISRs
 * 	\--> if possible, prefer a model where ISR pushes data to a non-atomic variable, sets a thread signal
 * 		\--> thread waiting on signal reads the data, does whatever with it, and re-arms the interrupt
 *
 *	Read policy:
 *		- try to read the shared variable
 *		- if the epoch changed between the start and end of a read, an update happened
 *		- retry the read until the epoch doesn't change
 *		- reads are retried upon interrupting writes
 *
 *	Write policy:
 *		- try to acquire the writing mutex; abort the write if mutex acquisition fails (another process is updating the variable)
 *		- with the mutex, copy the new data into a temporary buffer
 *		- after data copy is completed, point the reads to pull from the active buffer
 *		- writes take priority over read!
 *
 * 	NOTE: 	this class is design to capture the most recent version of a shared variable!
 * 			it does not strictly ensure all state variable changes are captured!
 */
//used for publish-subscribe data sharing models across threads
//and for ISR --> thread data sharing if absolutely required
//	\--> there's a bit of extra bloat involved with this sharing setup, so don't use this for critical ISRs
//if possible, prefer a model where ISR pushes data to a non-atomic variable, sets a thread signal
//	\--> thread waiting on signal reads the data, does whatever with it, and re-arms the interrupt
//
//NOTE: this class just contains the most recent version of the variable!
//		it does not strictly ensure all variable changes are captured!

//forward declaring classes
template<typename Vartype> class Pub_Var;
template<typename Vartype> class Sub_Var;
template<typename Vartype> class Sub_Var_RC;

template<typename Vartype>
class Pub_Var {
	//copying is a core part of the shared variable; ensure this
	static_assert(std::is_trivially_copyable_v<Vartype>, "Atomic type must be trivially copyable");
public:
	//intentionally leak with these factory functions
	//only way to guarantee program lifetime
	static Pub_Var<Vartype>& mk() { return *new Pub_Var(); }
	static Pub_Var<Vartype>& mk(const Vartype& init_val) { return *new Pub_Var(init_val); }

	//write method is non-stalling
	inline bool publish(const Vartype& v) noexcept {
		//we'll return whether we could acquire the write mutex (i.e. if a write isn't in progress)
		//returns whether the mutex could be acquired successfully
		return write_mutex.TRY_WITH([&](){
			//check if the variable we're writing is already the same
			//break early without signaling change
			if(v == READ_PORT()) return;

			//the variable we wanna write is different, look at our writing index
			//and write into that spot of our double buffer
			WRITE_PORT() = v;

			//signal a state change (also updates read/write indices by incrementing epoch)
			write_signal.signal();
		});
	}

	//read method CAN stall if write is ongoing
	inline Vartype read() const noexcept {
		//a valid read is one that starts and ends at the same epochs
		//try to read until we get a valid result
		while(true) {
			auto start_epoch = write_signal.get_epoch();
			auto var = READ_PORT(); //COPY into the temp, don't reference
			auto end_epoch = write_signal.get_epoch();

			//if no changes in epoch occurred during read, read is valid
			if(start_epoch == end_epoch) return var;
		}
	}

	//and expose subscription methods
	//one of these has "read-clear" access
	inline Sub_Var<Vartype> subscribe() 		{ return Sub_Var<Vartype>(*this, write_signal.listen()); }
	inline Sub_Var_RC<Vartype> subscribe_RC() 	{ return Sub_Var_RC<Vartype>(*this, write_signal.listen()); }

	//delete copy constructor, assignment operator, and destructor
	Pub_Var(const Pub_Var<Vartype>& other) = delete;
	void operator=(const Pub_Var<Vartype>& other) = delete;
	~Pub_Var() = delete;

private:
	//private constructors; Pub Vars always last for the lifetime of the program
	Pub_Var(): var_ping(Vartype()), var_pong(Vartype()) {}
	Pub_Var(const Vartype& init_val): var_ping(init_val), var_pong(init_val) {}

	//helper functions to determine which index to read/write from given the current signal epoch
	inline Vartype READ_PORT() const	{ return (write_signal.get_epoch()) & 0x1 ? var_ping : var_pong; } //read always copies
	inline Vartype& WRITE_PORT()		{ return (write_signal.get_epoch()) & 0x1 ? var_pong : var_ping; } //write will reference

	//our double-buffered write port with a ping/pong index flag
	//read should come from this index, writes should go to the negation of the index
	Vartype var_ping;
	Vartype var_pong;

	//a mutex for write accessing
	Mutex write_mutex;

	//and a thread signal to notify listeners when updates to the variables have happened
	//use the epoch field of the thread signal to determine our reading/writing index
	//use the LSB of the epoch
	//make sure this, like the Pub_Var itself, lasts the lifetime of the program
	PERSISTENT((Thread_Signal), write_signal);
};

template<typename Vartype>
class Sub_Var {
public:
	//constructor needs a variable to access and a signal to listen to
	//save the variable, but copy the listener
	Sub_Var(Pub_Var<Vartype>& _pub_var, const Thread_Signal_Listener& _signal_listener):
		pub_var(&_pub_var), signal_listener(_signal_listener)
	{}
	//default construct using our dummy pub_var
	Sub_Var(): pub_var(nullptr), signal_listener() {}

	//expose the thread signal methods and forward to signal listener
	inline void refresh() { signal_listener.refresh(); }
	inline bool check(bool do_refresh = true) { return signal_listener.check(do_refresh); }
	inline bool wait(uint32_t timeout_ms = 0, bool do_refresh = true) { return signal_listener.wait(timeout_ms, do_refresh); }

	//and forward the read method to the pub var
	inline Vartype read() { return pub_var->read(); }

protected:
	//save the parent pub_var and the thread signal listener
	Pub_Var<Vartype>* pub_var;
	Thread_Signal_Listener signal_listener;
};

//subscription with read-clear
template<typename Vartype>
class Sub_Var_RC : public Sub_Var<Vartype> {
public:
	//expose all public methods from the Sub_Var
	//and forward the base class constructor
	using Sub_Var<Vartype>::Sub_Var;

	//add an acknowledge/reset method that allows subscribers to
	//reset the state of a shared variable
	void acknowledge_reset(Vartype reset_val = Vartype()) const {
		this->pub_var->publish(reset_val);
	}
};

//========================= HELPER MACROS ===========================
//use these to quickly generate "link" and "subscribe" functions for any class with state variables
#define SUBSCRIBE_FUNC(name) 			\
    auto subscribe_##name() { 			\
        return this->name.subscribe(); 	\
    }

#define SUBSCRIBE_FUNC_RC(name) 			\
    auto subscribe_RC_##name() { 			\
        return this->name.subscribe_RC(); 	\
    }										\
	SUBSCRIBE_FUNC(name)	//also provide a non read-clear hook

#define LINK_FUNC(name) 					\
    void link_##name(const auto& sub) { 	\
        this->name = sub; 					\
    }

#define LINK_FUNC_RC(name) 					\
    void link_RC_##name(const auto& sub) { 	\
        this->name = sub; 					\
    }

