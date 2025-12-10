/*
 * Hispeed subsystem
 *
 *
 */

#include "app_threading.hpp"
#include "app_state_machine_library.hpp"
#include "app_scheduler.hpp"
#include "app_hispeed_analog.hpp"
#include "app_sync_if.hpp"
#include "app_hal_pwm.hpp"
#include "app_hal_dram.hpp"
#include "app_threading.hpp"
#include "app_hal_hsem.hpp"
#include "app_shared_memory.h"

class Hispeed_Subsystem {
public:
	//================================ TYPEDEFS ==================================

	/*
	 * Hispeed_Channel_Hardware_t
	 * Pass this to the constructor as a neat wrapper for all hardware related to a particular channel
	 */
	struct Hispeed_Channel_Hardware_t {
		HiSpeed_SPI::SPI_Hardware_Channel& _spi_channel_hw;
		GPIO_Alternate::GPIO_Alternate_Hardware_Pin _cs_dac_pin;
		GPIO_Alternate::GPIO_Alternate_Hardware_Pin _cs_adc_pin;
		GPIO::GPIO_Hardware_Pin _soa_en_pin;
		GPIO::GPIO_Hardware_Pin _tia_en_pin;
	};

	//============================================================================

	//###### Constructors ######
	//standard constructor takes
	//	\--> 4 hardware channels
	//	\--> DRAM instance hardware (owns the DRAM)
	//	\--> Power monitor instance (for instantaneous power monitoring in high speed state execution)
	//	\--> Sync interface instance (to signal SYNC status)
	Hispeed_Subsystem(	Hispeed_Channel_Hardware_t ch0,
						Hispeed_Channel_Hardware_t ch1,
						Hispeed_Channel_Hardware_t ch2,
						Hispeed_Channel_Hardware_t ch3	);

	//delete the copy constructor and assignment operator to prevent accidental copies/writes
	Hispeed_Subsystem(const Hispeed_Subsystem& other) = delete;
	void operator=(const Hispeed_Subsystem& other) = delete;

	//initialization routine
	void init();

	//and the last thing we need is just accessors to our subsystem variables
	LINK_FUNC(command_hispeed_arm_fire_request);
	SUBSCRIBE_FUNC(status_hispeed_armed);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_ready);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_pwr);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_timeout);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_cancelled);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_complete);
	LINK_FUNC_RC(command_hispeed_SOA_enable);
	LINK_FUNC_RC(command_hispeed_TIA_enable);
	LINK_FUNC_RC(command_hispeed_SOA_DAC_drive);
	SUBSCRIBE_FUNC(status_hispeed_TIA_ADC_readback);
	LINK_FUNC(status_onboard_immediate_pgood);
	LINK_FUNC(status_onboard_debounced_pgood);
	SUBSCRIBE_FUNC(command_attach_mem);
	LINK_FUNC(status_mem_attached);

private:
	//##### HISPEED THREAD FUNCTION #####
	//before we actually arm + fire, check to see if our peripheral is ready
	void do_prearm_check();		//clears any pending timeouts, sets up another one
	void do_prearm_fail();		//called on timeout; posts a timeout status update
	static const uint32_t PREARM_TIMEOUT_MS = 5000;	//let the core take 5 seconds to be ready again

	//in the low-speed core, this state basically prepares all the peripherals/memory for arming + firing
	//and communicates with the high-speed core via shared thread signals
	void do_arm_fire_setup();	//sets up peripherals, detaches the files, moves the inputs, locks memory, clears the signal listener, asserts fire signal
	void do_arm_fire_run();		//polls the completion flags, asserts signal if done
	void do_arm_fire_exit();	//grabs the exit codes, deasserts fire signal, unlocks memory, moves the outputs, attaches the files, restores peripherals
	static const uint32_t FIRING_TIMEOUT_MS = 15000; //large networks may take a while to execute

	//and all the semaphores for inter-core signaling
	Hard_Semaphore HISPEED_ARM_FIRE_READY = 	{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_READY)};
	Hard_Semaphore HISPEED_ARM_FIRE_SUCCESS = 	{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_SUCCESS)};
	Hard_Semaphore HISPEED_ARM_FIRE_ERR_READY = {static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_ARM_FIRE_ERR_READY)};
	Hard_Semaphore LOSPEED_DO_ARM_FIRE = 		{static_cast<Hard_Semaphore::HSem_Channel>(Sem_Mapping::SEM_DO_ARM_FIRE)};

	//and signal-listener pairs to indicate when we're done with our arm-fire sequence or if we timed out/lost power
	Scheduler arm_fire_timeout_task;	//sets the timeout flag if it gets triggered
	PERSISTENT((Thread_Signal), arm_fire_timeout);
	Thread_Signal_Listener arm_fire_timeout_listener = arm_fire_timeout.listen();
	PERSISTENT((Thread_Signal), arm_fire_done);
	Thread_Signal_Listener arm_fire_done_listener = arm_fire_done.listen();
	PERSISTENT((Thread_Signal), arm_fire_brownout);
	Thread_Signal_Listener arm_fire_brownout_listener = arm_fire_brownout.listen();
	PERSISTENT((Thread_Signal), arm_fire_cancelled);
	Thread_Signal_Listener arm_fire_cancelled_listener = arm_fire_cancelled.listen();

	//##### UTILITY CONFIGURATION FUNCTIONS #####
	//some functions to run when power becomes good/bad
	//namely setting up I/O pins, resetting state variables
	//for `activate` function, set up some variables relevant to continuous status reporting of DAC/ADC state + schedules "hispeed pilot" task
	void activate(); //run upon entry to "active state"
	void deactivate(); //run upon entry to "inactive state"

	//##### THREAD FUNCTIONS #####
	//run this function to execute our state machine and run "asynchronous" commands
	Scheduler check_state_update_task;
	void check_state_update_run_esm();

	//this function will sample the highspeed ADC/DAC
	//calling this "pilot" like a pilot light for a furnace--"gets us ready" to "fire"
	Scheduler hispeed_pilot_task;
	PERSISTENT((Thread_Signal), pilot_signal);	//scheduler asserts this signal
	Thread_Signal_Listener pilot_signal_listener = pilot_signal.listen();
	static const uint32_t PILOT_TASK_PERIOD_MS = 100; //run pilot task at 10Hz.
	void do_hispeed_pilot();

	//some other deferred update functions, regarding the SOA/TIA enable GPIOs
	void do_soa_gpio_control(std::array<uint16_t, 4> dac_vals);
	void do_tia_gpio_control();

	//##### HISPEED HARDWARE ########
	//a little structure to organize our hardware by channel
	//own instances of the hardware within the struct
	//construct the struct with a hardware channel structure -- simplifies the class's constructor
	struct Hispeed_Channel_t {
		Hispeed_Analog device_pair;
		GPIO soa_en;
		GPIO tia_en;
		Hispeed_Channel_t(Hispeed_Channel_Hardware_t hw_channel):
			device_pair(hw_channel._spi_channel_hw, hw_channel._cs_dac_pin, hw_channel._cs_adc_pin),
			soa_en(hw_channel._soa_en_pin),
			tia_en(hw_channel._tia_en_pin)
		{}

		//======= and a couple helper functions =======
		void init() {
			device_pair.init();
			soa_en.init();
			tia_en.init();
		}

		void activate() {
			soa_en.clear();
			tia_en.clear();
			device_pair.activate();
		}

		void deactivate() {
			soa_en.clear();
			tia_en.clear();
			device_pair.deactivate();
		}

		void prepare() {
			device_pair.arm();
			//moving timer control to second core
		}

		void restore() {
			//letting second core release the timers
			device_pair.disarm();
		}
	};

	Hispeed_Channel_t CHANNEL_0;
	Hispeed_Channel_t CHANNEL_1;
	Hispeed_Channel_t CHANNEL_2;
	Hispeed_Channel_t CHANNEL_3;

	//##### HISPEED SYSTEM STATE MACHINE #####
	//own a couple simple states for the hispeed system
	//hispeed system runs a very basic state machine to manage subsystem control depending on power state
	ESM_State hispeed_state_INACTIVE; 	//will transition to ACTIVE if power good
	ESM_State hispeed_state_ACTIVE; 	//will transition to INACTIVE if !power good, transition to PREARM if request
	ESM_State hispeed_state_PREARM;		//will move to ARM_FIRE if our core is ready, back to ACTIVE if we timeout
	ESM_State hispeed_state_ARM_FIRE;	//will move to ACTIVE if we're complete or timed out
	Extended_State_Machine hispeed_esm; //the actual extended state machine wrapper

	//and some functions to check for state transitions
	bool hispeed_trans_INACTIVE_to_ACTIVE() { return status_onboard_debounced_pgood.read(); }	//check our subscription variable to see if power is good
	bool hispeed_trans_ACTIVE_to_INACTIVE() { return !status_onboard_debounced_pgood.read(); }	//check our subscription variable to see if power is bad
	bool hispeed_trans_ACTIVE_to_PREARM() 	{ return command_hispeed_arm_fire_request.read(); }	//check the subscription variable to see if we want to arm/fire
	bool hispeed_trans_PREARM_to_ARM_FIRE()	{ return 	HISPEED_ARM_FIRE_READY.READ() &&
														!status_mem_attached.read(); }			//if the second core is ready and memory is detached, move onto ARM state
	bool hispeed_trans_PREARM_to_ACTIVE()	{ return arm_fire_timeout_listener.check(); }		//if we timed out, move to the active state
	bool hispeed_trans_ARM_FIRE_to_ACTIVE() { return 	arm_fire_done_listener.check() ||		//move back to active when we're done or we timeout, leave errors signaled
														arm_fire_timeout_listener.check(false) 	||
														arm_fire_brownout_listener.check(false)	||
														arm_fire_cancelled_listener.check(false);	}
	//NOTE: Always return to active state after armed, even in case of power fail
	//		power monitor state variable will bring us into inactive state in case of actual power fail
	//		If I returned to inactive state, will likely return right back to active state, then back to inactive state due to delays in state updating

	//and some state transitions between everything
	//transitions bound to states in constructor
	ESM_Transition hispeed_trans_FROM_INACTIVE[1] = {	{&hispeed_state_ACTIVE, {BIND_CALLBACK(this, hispeed_trans_INACTIVE_to_ACTIVE)}		}	};
	ESM_Transition hispeed_trans_FROM_ACTIVE[2] =   {	{&hispeed_state_INACTIVE, {BIND_CALLBACK(this, hispeed_trans_ACTIVE_to_INACTIVE)}	},
														{&hispeed_state_ARM_FIRE, {BIND_CALLBACK(this, hispeed_trans_ACTIVE_to_PREARM)}	}	};
	ESM_Transition hispeed_trans_FROM_PREARM[2] =	{	{&hispeed_state_ACTIVE, {BIND_CALLBACK(this, hispeed_trans_PREARM_to_ACTIVE)}		},
														{&hispeed_state_ARM_FIRE, {BIND_CALLBACK(this, hispeed_trans_PREARM_to_ARM_FIRE)}	}	};
	ESM_Transition hispeed_trans_FROM_ARM_FIRE[1] = {	{&hispeed_state_ACTIVE, {BIND_CALLBACK(this, hispeed_trans_ARM_FIRE_to_ACTIVE)}		}	};

	//###### STATE VARIABLES #######
	//have R/C-command ARM_FIRE_REQUEST --> requests to arms and runs the highspeed system
	//have status ARMED --> reports whether the system is armed or not
	//have R/C-status ARM_ERR_READY --> reports whether the highspeed system was in the loop when a node wasn't ready
	//have R/C-status ARM_ERR_SYNC_TIMEOUT --> reports whether the highspeed system timed out waiting for a sync signal
	//have R/C-status ARM_ERR_PWR --> reports whether a power issue caused the execution to terminate early
	//have R/C-status ARM_FIRE_COMPLETE --> reports whether the hispeed system has finished execution
	//have R/C-command bool[4] SOA_ENABLE --> drives GPIO pins to enable SOA photomos	(gets reset in deactivation)
	//have R/C-command bool[4] TIA_ENABLE --> drives GPIO pins to enable TIA photomos	(gets reset in deactivation)
	//have R/C-command uint16_t[4] SOA_DAC_DRIVE --> writes these values out to the DAC (gets reset in deactivation)
	//have status uint16_t[4] TIA_ADC_READBACK --> reports the values read by the ADC
	//have R/C-command to load SDRAM with test sequence
	Sub_Var_RC<bool> 	command_hispeed_arm_fire_request;
	PERSISTENT((Pub_Var<bool>), status_hispeed_armed);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_err_ready);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_err_pwr);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_err_timeout);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_err_cancelled);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_complete);
	Sub_Var_RC<std::array<bool, 4>> 	command_hispeed_SOA_enable;
	Sub_Var_RC<std::array<bool, 4>> 	command_hispeed_TIA_enable;
	Sub_Var_RC<std::array<uint16_t, 4>> command_hispeed_SOA_DAC_drive;
	PERSISTENT((Pub_Var<std::array<uint16_t, 4>>), status_hispeed_TIA_ADC_readback);

	//and let's subscribe to the state of the onboard power monitor
	//to get whether the power rails are good or not (so we can activate/deactivate the subsystem)
	Sub_Var<bool> status_onboard_immediate_pgood;
	Sub_Var<bool> status_onboard_debounced_pgood;

	//also have some state variable interfaces to the memory manager
	//lets us attach/detach the block memory, and read attachment status
	Sub_Var<bool> status_mem_attached;
	PERSISTENT((Pub_Var<bool>), command_attach_mem, true);
};
