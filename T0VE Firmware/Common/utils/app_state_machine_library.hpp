#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"

/*
 * ESM system - "Extended State Machine" base class
 *
 * Self-written library to build what I'm calling "extended state machines"
 * MOTIVATION:
 * 	State machines can get us pretty far when describing the desired behavior of systems.
 * 	However, they can get bloated real fast--think about writing a simple system that latches an LED on when with a debounced pushbutton.
 * 	What I'm hoping to do here is provide a state-machine style interface, i.e.
 * 		- states identified as nodes of a graph
 * 		- transitions between states identified as directed edges of a graph
 * 		- transitions executing
 * 	In addition to this, I'll be adding functionality to enable the execution of "subprograms" and periodic functions more easily
 * 	This will be accomplished by:
 * 		- `impl_on_state_entry` function hook
 * 			- allows for one-time execution functions
 * 			- allows for one-time configuration for future periodic functions
 * 		- `impl_state_execution` function hook
 * 			- allows for execution of periodic functions
 * 			- allows for the execution of looping functions
 * 			- allows for the handling of "dispatched events" i.e. servicing a flag set during an ISR
 * 			- allows for stalling program execution if desired
 * 		- `impl_on_state_exit` function hook
 * 			- allows for deinitialization of heap variables
 * 			- allows for descheduling of recurrent tasks
 * 
 * Usage Details:
 * 		1) Create callback functions/non-capturing lambda functions to accomplish:
 * 			- `impl_on_state_entry()`
 * 			- `impl_state_execution()`
 *			- `impl_on_state_exit()`
 *			- any additional logic useful for checking whether a particular state transition is suitable
 * 		2) Instantiate ALL your ESM_State classes; construct by passing the callback functions
 * 		3) BELOW THESE, Create a std::array<ESM_Transition, n> for each state
 * 			- construct each state transition condition in the array initialization
 * 			- pass the next state and the condition for the state transition in the constructor
 * 		4) BELOW THESE, call `attach_state_transitions()` on each of the states
 * 			- pass the appropriate state transition list instantiated above
 *		5) BELOW THESE, instantiate an Extended_State_Machine
 * 			- pass in the entry point into the state machine as the only constructor argument
 *		
 *		6) IN THE MAIN APPLICATION LOOP:
 *			- call `RUN_ESM()`
 *			- all state machine logic and execution will be handled for you
 */

//forward declaring some classes up front:
class ESM_State; //state class
class ESM_Transition; //transition class (for transitions between states)
class Extended_State_Machine; //container class that runs the state machine

//================================================= CLASS FOR INDIVIDUAL STATE IN STATE MACHINE =================================================

/*
 * This is the class for a single state in a state machine
 * States should inherit from this class
 */
class ESM_State {
public:
	/*
	 * We should only allow execution privileges to the Extended_State_Machine class
	 * This is to prevent the user from invoking any special functions of the state directly
	 */
	friend class Extended_State_Machine;

	//constructor takes callback function arguments
	ESM_State(	Callback_Function<> _impl_state_on_entry,
				Callback_Function<> _impl_state_execution,
				Callback_Function<> _impl_state_on_exit		);

	//provide a hook to attach a list of state transitions from this current state
	//idea is to create all the states
	//then create a std::array<ESM_Transition> xxx_TRANSITIONS = { ..., ..., ..., ...};
	//with each state transition explicitly initialized via constructor 
	//then the entire list of transitions can be passed into the following function
	//arbitrary number of state transitions can be attached here
	void attach_state_transitions(std::span<ESM_Transition, std::dynamic_extent> _transition_list);

protected:
	/*
	 * Provide a function hook that "runs" the particular state's "extended functionality"
	 * NOTE: in a canonical state machine, you can't "execute" a state; this is "extended" functionality I'm adding to make certain behaviors easier to implement
	 * This function does the following:
	 * 	- Checks if we've just entered this state (`just_entered_state` == true); if so, run `impl_on_state_entry`, sets `just_entered_state` = false
	 *  - Runs the `impl_state_execution` function
	 *  - Checks a linked-list of state transistions
	 * 		- Default our next state to execute to be a pointer to itself
	 * 		- If we detect a valid state transition case... 
	 * 			- update the `next_state` pointer to point to the next state
	 * - checks if we're leaving this state (sees if the next state is going to be this state; compare next state pointer to this)
	 * 		\--> sets `just_entered_state` flag to `true`
	 * - returns the next state we're executing as a pointer
	 */
	ESM_State* EXECUTE_STATE();

	/*
	 * Also provide a hook that "resets" the state
	 * Useful for when we reset our state machines
	 */
	void RESET_STATE();

private:
	//flag that says whether we just entered a particular state
	//controls the execution of `impl_on_state_entry()`
	bool just_entered_state = true;

	//own callback functions to execute state entry/exit/loop functions
	//I was initially thinking about virtualizing the state machine class, but
	//its more annoying to create brand new classes
	//and a virtualization system will just require dynamic dispatch/vtabling anyway, so still requires call overhead
	Callback_Function<> impl_state_on_entry;
	Callback_Function<> impl_state_execution;
	Callback_Function<>	impl_state_on_exit;

	//own a view into a list of state transitions from the particular state
	//we'll check each of these transitions every loop iteration to see if we can move to one of these following states
	std::span<ESM_Transition, std::dynamic_extent> state_transitions;
};

//================================================= CLASS FOR INDIVIDUAL STATE TRANSITION IN STATE MACHINE =================================================
class ESM_Transition {
public:	
	//all we need to do to create a state transition is to pass in the next state we'd like to transition to
	//and a callback function that will assess whether we should transition to this state
	ESM_Transition(ESM_State* _next_state, Callback_Function<bool> _assess_report_state_transition);

	//override the call operator to run the transition check
	//if a state transition is desired, return the next state pointer
	//if no state transition is desired, return nullptr
	ESM_State* operator()() const;

private:
	ESM_State* const next_state;
	Callback_Function<bool> const assess_report_state_transition;
};

//================================================= CONTAINER THAT RUNS THE STATE MACHINE =================================================

class Extended_State_Machine {
public:
	//constructor
	//pass the entry point of the state machine system
	Extended_State_Machine(ESM_State* entry_state);

	//call this function in the application loop to run the extended state machine
	//execution and state transitions will happen automatically
	void RUN_ESM();

	//call this function in the application loop to reset the extended state machine
	//state machine will return back to its entry state as if it just started executing
	void RESET_ESM();
private:
	//hold a pointer to the state that is currently executing
	//this will be initialized to the entry state of the state machine
	ESM_State* current_state;

	//also remember the entry state for when we reset the state machines
	ESM_State* entry_state;
};
