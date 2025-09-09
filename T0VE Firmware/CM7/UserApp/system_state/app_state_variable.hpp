#pragma once

#include "app_utils.hpp" //for callback function
#include "app_types.hpp" //for types
#include "app_threading.hpp" //for atomic variables

/*
 * app_state_variable.hpp
 *
 * Implement a publish/subscribe model of information sharing across multiple threads
 * The process that owns the state variable is the only process that should be able to "publish", i.e. write to it
 * An arbitrary number of processes, however, can "subscribe" to the state variable via a `subscribe()` method.
 * this returns a copy of a subscription class that can
 * 		\--> access the current state of the variable atomically
 * 		\--> check to see if the variable has been updated
 * 		\--> stall until the variable has been updated (USE SPARINGLY ON NON RTOS SYSTEMS!)
 *
 * 	This class is designed to have reasonably low overhead, but implements quality of life + safety features that do sap performance
 * 	for the fastest inter-thread communication without these quality of life features, directly use `Atomic_Var`
 */

//forward declaring `State_Variable` class to give it access to the `signal()` method
template<typename Vartype> class State_Variable;

//============================================= STATE VARIABLE SUBSCRIPTION CLASS ============================================

template<typename Vartype>
class SV_Subscription {
protected:
	//reference the state variable - will not expose writes to the state variable!
	Atomic_Var<Vartype>* sv_ptr;
	inline void repoint(Atomic_Var<Vartype>* _sv_ptr) { sv_ptr = _sv_ptr; } //useful for destructor

	//a thread signal mechanism that gets asserted by the master state variable
	Thread_Signal signal_state_change;

	//and some linked-list type utilities for notify chaining
	SV_Subscription<Vartype>* next_sub = nullptr;
	SV_Subscription<Vartype>* prev_sub = nullptr;
	inline SV_Subscription<Vartype>* next() { return next_sub; } //useful for iterating down the list

	//and a private function that allows the master state variable to signal that state has changed
	//forwards to the thread signal
	inline void signal() {signal_state_change.signal();}

	//declaring the state variable class a friend to give it access to private methods
	friend class State_Variable<Vartype>;

	//################## UTILITY LIST MANAGEMENT FUNCTIONS ##################

	void add_this_to_llist(SV_Subscription<Vartype>* after_this_one) {
		//add the subscriber to the linked list after the one that was passed in
		//### UPDATING DOWN THE LIST ###
		this->next_sub = after_this_one->next_sub; //whatever was after the previous one is now after us
		after_this_one->next_sub = this; //and the next one after the previous one is now us

		//### UPDATING UP THE LIST ###
		this->prev_sub = after_this_one; //the one we're after is before us
		if(this->next_sub) this->next_sub->prev_sub = this; //the one before the one after us is us
	}

	void remove_this_from_llist() {
		//remove ourself from the linked list
		//updating the subscribers upstream and downstream of us to point to each other
		if(prev_sub && prev_sub->next_sub == this) {
			prev_sub->next_sub = next_sub;
		}
		if(next_sub && next_sub->prev_sub == this) {
			next_sub->prev_sub = prev_sub;
		}
	}

public: //and these are functions any subscribers have access to

	//read the state variable
	//if it isn't initialized yet, just return a default version of the state variable
	Vartype read() const { return sv_ptr ? sv_ptr->read() : Vartype(); }

	//override the cast operator
	//typically synonymous to reading the particular state variable
	//just forward the read call
	inline operator Vartype() const { return read(); }

	//check if there's an update to the state variable
	inline bool available(bool clear = true) { return signal_state_change.available(clear); }

	//wait for state change of the variable
	//USE SPARINGLY IN NON-RTOS APPLICATIONS - WAITS IN A BUSY LOOP!
	inline void wait_state_change(bool clear = true, uint32_t timeout_ms = UINT32_MAX) { signal_state_change.wait(); }

	//################## CONSTRUCTORS AND DESTRUCTORS ##################

	//Constructor
	//non-trivial, since we have to manage the linked-list behavior
	//also make sure to save the reference to the state variable
	SV_Subscription(Atomic_Var<Vartype>* _sv_ptr, SV_Subscription<Vartype>* after_this_one): sv_ptr(_sv_ptr)
	{
		if(!after_this_one) return; //if we're passed a nullptr, don't even try to initialize it--will break the chain

		//add the subscriber to the linked list after the one that was passed in
		add_this_to_llist(after_this_one);
	}

	//trivial constructor; useful for instances that aren't plugged into a state variable immediately
	//WON'T add ourselves to the linked list because degenerate
	SV_Subscription(): sv_ptr(nullptr), next_sub(nullptr), prev_sub(nullptr) {}

	//deliberate copy constructor and assignment operator
	//since we've deleted our thread signal copy constructor, we need to explicitly initialize that
	//okay to do this since our state variable signal thread
	SV_Subscription(const SV_Subscription<Vartype>& other):
		sv_ptr(other.sv_ptr),
		signal_state_change(),
		next_sub(other.next_sub),
		prev_sub(other.prev_sub)
	{
		//add the subscriber to the linked list after our previous element
		add_this_to_llist(prev_sub);
	}

	//copy assignment operator
	void operator=(const SV_Subscription<Vartype>& other) {
		if(this == &other) return; //self assignment

		//remove ourselves from the linked list first
		remove_this_from_llist();

		//assigning non-trivial variables as necessary
		sv_ptr = other.sv_ptr;
		next_sub = other.next_sub;
		prev_sub = other.prev_sub;

		//and add ourselves to the linked list using the new pointers
		//add ourselves after the previous item
		add_this_to_llist(prev_sub);
	}

	//Destructor
	//non-trivial since we have to manage linked list behavior
	~SV_Subscription() {
		remove_this_from_llist();
	}

};

//============================================= STATE VARIABLE SUBSCRIPTION CLASS with READ-CLEAR ACCESS ============================================
//pass this flavored version of a subscription for any threads you want to be able to read-clear a particular state variable
//NOTE: if ANY `SV_Subscription_RC` reads the variable

template<typename Vartype>
class SV_Subscription_RC : public SV_Subscription<Vartype> {
public:
	//inherit constructors from base class
	using SV_Subscription<Vartype>::SV_Subscription;

	//just adding `acknowledge` function
	//kinda a fast way to just clear/reset the state variable
	//useful for flags whose state will either be asserted/deasserted
	void acknowledge_reset(Vartype reset_val = Vartype()) const {
		if(this->sv_ptr) this->sv_ptr->write(reset_val);
	}
};

//============================================= STATE VARIABLE CLASS ============================================

template<typename Vartype>
class State_Variable {
private:
	//threading functions
	Atomic_Var<Vartype> state_var;

	//provide a notification stream for any subscribers
	//since we want to avoid heap allocation, a linked-list style system is best
	//these are wrappers of Thread Signal basically
	SV_Subscription<Vartype> sub_notify_hook; //dummy hook for notification system

public:
	//construct the write port with a reference to the state variable, the mutex, and the state change signal
	//initialize the read and write ports accordingly
	State_Variable(const Vartype& initial_value = Vartype()):
		state_var(initial_value),
		sub_notify_hook(&state_var, nullptr)
	{}

	//delete the copy constructor and assignment operator
	//to avoid any weird issues cropping up from accidentally copying a state variable
	//DELETE the destructor to avoid any dangling reference issues for instantiated subscribers
	void operator=(const State_Variable<Vartype>& other) = delete;
	State_Variable(const State_Variable<Vartype>& other) = delete;

	//non-trivial destructor
	//goes through the linked list and invalidates the pointer to the state var
	~State_Variable() {
		//start at the dummy hook, and go downstream
		SV_Subscription<Vartype>* sub_reset = sub_notify_hook.next();
		while(sub_reset) {
			sub_reset->repoint(nullptr); //invalidate data the subscriber is watching
			sub_reset = sub_reset->next();
		}
	}

	//just overload the assignment and cast operator
	//these will only be used by the calling thread
	//as such, allow for unguarded read for the state variable
	void operator=(const Vartype& update_state) {
		bool state_equal_no_update = state_var.cmp_eq_write(update_state);
		if(state_equal_no_update) return; //no need to signal downstream threads for same state

		//now we need to signal downstream threads
		//start at the dummy hook, and go downstream
		SV_Subscription<Vartype>* sub_notify = sub_notify_hook.next();
		while(sub_notify) {
			sub_notify->signal();
			sub_notify = sub_notify->next();
		}
	}
	operator Vartype() const { return state_var.UNGUARDED_READ(); }

	//also have a method of "subscribing" to the state variable
	//returns a notification system that can be used for basic state variable listening
	//add the new subscriber to the front of our linked list
	SV_Subscription<Vartype> subscribe() { return SV_Subscription<Vartype>(&state_var, &sub_notify_hook); }

	//adding a variant of the subscription method that allows for read-clear access
	//basically a specialization of the SV_Subscription class that has a modified read function
	SV_Subscription_RC<Vartype> subscribe_RC() { return SV_Subscription_RC<Vartype>(&state_var, &sub_notify_hook); }
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
