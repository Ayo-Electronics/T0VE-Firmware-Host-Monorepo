#include "app_state_machine_library.hpp"

//================================================= MEMBER FUNCTIONS FOR ESM_STATE =================================================
//nothing exciting going on in the constructor
//just copying over the callback functions
ESM_State::ESM_State(	Callback_Function<> _impl_state_on_entry,
						Callback_Function<> _impl_state_execution,
						Callback_Function<> _impl_state_on_exit		):
	impl_state_on_entry(_impl_state_on_entry),
	impl_state_execution(_impl_state_execution),
	impl_state_on_exit(_impl_state_on_exit)

{}

//provide a hook to attach a list of state transitions from this current state
//idea is to create all the states
//then create a std::array<ESM_Transition> xxx_TRANSITIONS = { ..., ..., ..., ...};
//with each state transition explicitly initialized via constructor 
//then the entire list of transitions can be passed into the following function
//arbitrary number of state transitions can be attached here
void ESM_State::attach_state_transitions(std::span<ESM_Transition, std::dynamic_extent> _transition_list) {
    //just save the list of transitions
    state_transitions = _transition_list;
}

//provide a generic hook for "Extended" state execution
ESM_State* ESM_State::EXECUTE_STATE() {
    //check if we've just entered this state
    //if so, run the on_state_entry function
    //and set the just_entered_state flag to false
    if(just_entered_state) {
        impl_state_on_entry();
        just_entered_state = false;
    }

    //run the state execution function
    //this is where the "extended functionality" of the state is implemented
    //this is where you can call scheduler functions and service deferred ISR calls
    impl_state_execution();

    //after we're done executing the "body of the state", check the list of state transitions
    //if a state transition is desired, return the next state pointer
    //run the `on_exit` callback, and set the just_entered_state flag to false
    for(const auto& transition : state_transitions) {
        ESM_State* next_state = transition();
        if(next_state) {
        	impl_state_on_exit();
            just_entered_state = true;
            return next_state;
        }
    }

    //if no state transition is desired, we'll be sticking to our current state
    return this; 
}

//and reset function just resets the `just_entered_state` flag
//but DON'T call `on_state_exit()`--haven't actually transitioned out of the state
void ESM_State::RESET_STATE() {
	just_entered_state = true;
}


//================================================= MEMBER FUNCTIONS FOR ESM_TRANSITION =================================================
//just save the variables during the constructor
ESM_Transition::ESM_Transition(ESM_State* _next_state, Callback_Function<bool> _assess_report_state_transition)
	: next_state(_next_state), assess_report_state_transition(_assess_report_state_transition) {}

//override the call operator to run the transition check
//if a state transition is desired, return the next state pointer
//if no state transition is desired, return nullptr
ESM_State* ESM_Transition::operator()() const {
	return assess_report_state_transition() ? next_state : nullptr;
}

//================================================= CONTAINER THAT RUNS THE STATE MACHINE =================================================
Extended_State_Machine::Extended_State_Machine(ESM_State* _entry_state)
    : current_state(_entry_state), entry_state(_entry_state) {}

//call this function in the application loop to run the extended state machine
//execution and state transitions will happen automatically
void Extended_State_Machine::RUN_ESM() {
    //execute the current state
    //and update the state pointer if we transition states  
    current_state = current_state->EXECUTE_STATE();
}

//call this function to return an ESM back to its starting state
void Extended_State_Machine::RESET_ESM() {
	//reset the state that we're currently executing
	//and return back to our entry state
	current_state->RESET_STATE();
	current_state = entry_state;
}

