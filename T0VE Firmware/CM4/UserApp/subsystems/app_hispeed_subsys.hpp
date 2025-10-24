/*
 * Hispeed subsystem
 *
 *
 */

#include "app_threading.hpp"
#include "app_state_machine_library.hpp"
#include "app_scheduler.hpp"
#include "app_hispeed_analog.hpp"
#include "app_power_monitor.hpp"
#include "app_hal_dram.hpp"
#include "app_sync_if.hpp"
#include "app_hal_pwm.hpp"
#include "app_threading.hpp"

/*
 * TODO LIST:
 *  - figure out how to time out in high-speed loop (DWT CYCLE COUNTER)
 *  - figure out how to link SDRAM to comms subsystem to load memory
 */

class Hispeed_Subsystem {
public:
	//================================ TYPEDEFS ==================================

	/*
	 * ADC_Destination_t
	 *  \--> want to pack information about where to route an ADC conversion result
	 *  \--> We need to encode:
	 *  		- Which block address to dump it in
	 *  		- Which index to dump it in
	 *  		- If we just wanna throw the ADC value away
	 *
	 *  I'm proposing a structure that looks like:
	 *  	 [0..27] --> block index
	 *  	[28..29] --> sub index
	 *  	 	[30] --> throwaway flag	 (`1` if throwaway)
	 *  	 	[31] --> block valid flag (`1` if valid)
	 *
	 *	I'd likely implement this as a struct with methods rather than a union to speed up access
	 *		\--> methods will just be bit shifts
	 *
	 *  For simple fully-connected feed-forward networks, we can run without a sequence control counter
	 *  Essentially, we'd run the system until we hit an invalid block, where we'd disarm and stop
	 *  I'd like to be able to quickly check if a block is valid or not, likely going to be just checking if the destination is 0
	 */
	struct ADC_Destination_t {
		//some consts just to make reading/writing more consistent
		static constexpr uint32_t BLOCK_INDEX_MASK = 0x0FFF'FFFF;
		static constexpr size_t BLOCK_INDEX_SHIFT = 0;
		static constexpr uint32_t SUB_INDEX_MASK = 0x03;
		static constexpr size_t SUB_INDEX_SHIFT = 28;
		static constexpr uint32_t THROWAWAY_MASK = 0x01;
		static constexpr size_t THROWAWAY_SHIFT = 30;
		static constexpr uint32_t BLOCK_VALID_MASK = 0x01;
		static constexpr size_t BLOCK_VALID_SHFIT = 31;

		//just the data, store as uint32_t
		uint32_t dest_data;

		//construct with either data or fields, default construct to 0
		ADC_Destination_t(): dest_data(0) {}
		ADC_Destination_t(uint32_t _dest_data): dest_data(_dest_data) {}
		ADC_Destination_t(uint32_t block_index, uint32_t sub_index, uint32_t throwaway):
			dest_data( 	((block_index & BLOCK_INDEX_MASK) << BLOCK_INDEX_SHIFT) |
						((sub_index & SUB_INDEX_MASK) << SUB_INDEX_SHIFT) 		|
						((throwaway & THROWAWAY_MASK) << THROWAWAY_SHIFT)		|
						( BLOCK_VALID_MASK << BLOCK_VALID_SHFIT)					)
		{}

		//methods to operate on the data field
		__attribute__((always_inline)) inline uint32_t block_index() 	{ return ((dest_data >> BLOCK_INDEX_SHIFT) & BLOCK_INDEX_MASK); 	}
		__attribute__((always_inline)) inline uint32_t sub_index() 		{ return ((dest_data >> SUB_INDEX_SHIFT) & SUB_INDEX_MASK); 		}
		__attribute__((always_inline)) inline bool throwaway()			{ return ( dest_data & (THROWAWAY_MASK << THROWAWAY_SHIFT));		}
		__attribute__((always_inline)) inline bool valid()				{ return ( dest_data & (BLOCK_VALID_MASK << BLOCK_VALID_SHFIT));	}
	};

	/*
	 * Hispeed_Block_t
	 * Contains information relevant to execution of a block
	 * 	\--> `param_vals` contains the values to write to the DACs
	 * 	\--> `readback_destinations` contains information on where to store the ADC readings
	 * 	Using C-style arrays to minimize the std::array overhead (very slight performance bump)
	 */
	struct Hispeed_Block_t {
		uint16_t param_vals[4];
		ADC_Destination_t readback_destinations[4];

		//factory function
		//direct copy construction of C-arrays can be kinda weird, so just making it step-wise
		static Hispeed_Block_t mk(std::array<uint16_t, 4> _vals, std::array<ADC_Destination_t, 4> _dest) {
			Hispeed_Block_t block;
			std::copy(_vals.begin(), _vals.end(), block.param_vals); //param vals decays into a pointer
			std::copy(_dest.begin(), _dest.end(), block.readback_destinations);
			return block;
		}

		//special factory function that makes a block that throws away ADC value
		static Hispeed_Block_t mk_throwaway(std::array<uint16_t, 4> _vals) {
			//explicitly call ADC_Destination_t constructor
			return mk(_vals, {	ADC_Destination_t(0, 0, 1),
								ADC_Destination_t(0, 1, 1),
								ADC_Destination_t(0, 2, 1),
								ADC_Destination_t(0, 3, 1)	});
		}

		//special factory function that makes a terminating block
		static Hispeed_Block_t mk_term() { return mk({0}, {0});	}
	};

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
		PWM::PWM_Hardware_Channel& _cs_dac_timer;
		PWM::PWM_Hardware_Channel& _cs_adc_timer;
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
						Hispeed_Channel_Hardware_t ch3,
						DRAM::DRAM_Hardware_Channel& _block_memory_hw,
						Power_Monitor& _onboard_power_monitor,
						Multicard_Info& _multicard_interface);

	//delete the copy constructor and assignment operator to prevent accidental copies/writes
	Hispeed_Subsystem(const Hispeed_Subsystem& other) = delete;
	void operator=(const Hispeed_Subsystem& other) = delete;

	//initialization routine
	void init();

	//and the last thing we need is just TODO accessors to our subsystem variables
	LINK_FUNC(command_hispeed_arm_fire_request);
	SUBSCRIBE_FUNC(status_hispeed_armed);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_ready);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_sync_timeout);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_err_pwr);
	SUBSCRIBE_FUNC_RC(status_hispeed_arm_flag_complete);
	LINK_FUNC_RC(command_hispeed_sdram_load_test_sequence);
	LINK_FUNC_RC(command_hispeed_SOA_enable);
	LINK_FUNC_RC(command_hispeed_TIA_enable);
	LINK_FUNC_RC(command_hispeed_SOA_DAC_drive);
	SUBSCRIBE_FUNC(status_hispeed_TIA_ADC_readback);

private:
	//###################### THE MAIN HIGH-SPEED EXECUTION FUNCTION ###################
	//this function is designed to run on entry into the ARMED_FIRE state
	//will return from this function having modified the appropriate state variables upon exit
	//state machine is configued to automatically return to the correct state
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,inline-functions")
	__attribute__((section(".ITCMRAM_Section"))) //placing this function in ITCM for performance
	void do_hispeed_arm_fire();
	#pragma GCC pop_options

	//a little enum class that reports how we exited the loop
	enum class Loop_Exit_Status_t {
		LOOP_EXIT_OK,
		LOOP_EXIT_ERR_SYNC_TIMEOUT,	//error waiting for sync signal
		LOOP_EXIT_ERR_POWER,		//error regarding onboard power supply
		LOOP_EXIT_ERR_READY,		//error regarding ALL_READY line
	};

	//have access to a list of blocks
	//using pointer notation here--will index through these using array notation
	//may try to figure out an elegant way to use `std::span<>` here if possible
	Hispeed_Block_t* block_sequence;

	//own a dummy block to dump throwaway ADC readings
	//owning a complete block results in fewer conditional code branches in the high-speed execution
	Hispeed_Block_t throwaway_block;

	//a function that loads a test sequence into the SDRAM
	void do_load_sdram_test_sequence();

	//timeout constants for the high-speed execution loop
	static constexpr float TIMEOUT_DURATION_MS = 5000;
	static constexpr uint32_t TIMEOUT_TICKS = (uint32_t)(CPU_FREQ_HZ * TIMEOUT_DURATION_MS / 1000);

	//and some constants for how long we should hold our chip select lines low
	static constexpr float CS_DAC_LOWTIME = 650e-9; //found somewhat empirically; some delay between writing to TXDR and getting SPI out the door
	static constexpr float CS_ADC_LOWTIME = 1650e-9;//maximum amount of acquisition time while still respecting conversion time
	static constexpr float SYNC_FREQUENCY = 500e3; 	//starting with 500kHz update frequency, will increase as timing validated
	static constexpr float SYNC_DUTY = 0.5;			//operating the SYNC timer at 50% duty cycle
	//#################################################################################

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
		PWM cs_dac_timer;
		PWM cs_adc_timer;
		Hispeed_Channel_t(Hispeed_Channel_Hardware_t hw_channel):
			device_pair(hw_channel._spi_channel_hw, hw_channel._cs_dac_pin, hw_channel._cs_adc_pin),
			soa_en(hw_channel._soa_en_pin),
			tia_en(hw_channel._tia_en_pin),
			cs_dac_timer(hw_channel._cs_dac_timer),
			cs_adc_timer(hw_channel._cs_adc_timer)
		{}

		//======= and a couple helper functions =======
		void init() {
			device_pair.init();
			soa_en.init();
			tia_en.init();
			cs_dac_timer.init();
			cs_adc_timer.init();
		}

		void configure_timing(float dac_cs_lowtime_s, float adc_cs_lowtime_s) {
			cs_adc_timer.set_period(100e-6);
			cs_adc_timer.set_assert_time(adc_cs_lowtime_s);
			cs_dac_timer.set_period(100e-6);
			cs_dac_timer.set_assert_time(dac_cs_lowtime_s);
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

		void arm() {
			device_pair.arm();
			cs_adc_timer.reset_count(0xFFFF);
			cs_dac_timer.reset_count(0xFFFF);
			cs_adc_timer.enable();
			cs_dac_timer.enable();
		}

		void disarm() {
			cs_adc_timer.disable();
			cs_dac_timer.disable();
			device_pair.disarm();
		}
	};

	Hispeed_Channel_t CHANNEL_0;
	Hispeed_Channel_t CHANNEL_1;
	Hispeed_Channel_t CHANNEL_2;
	Hispeed_Channel_t CHANNEL_3;

	//Reference a power monitor, own a DRAM subsystem, and reference a sync interface
	//lets us monitor the power status, access external memory, and control the sync signals respectively
	DRAM block_memory;
	Power_Monitor& onboard_power_monitor;
	Multicard_Info& multicard_interface;

	//##### HISPEED SYSTEM STATE MACHINE #####
	//own a couple simple states for the hispeed system
	//hispeed system runs a very basic state machine to manage subsystem control depending on power state
	ESM_State hispeed_state_INACTIVE; //will transition to ACTIVE if power good
	ESM_State hispeed_state_ACTIVE; //will transition to INACTIVE if !power good, transition to ARM if request
	ESM_State hispeed_state_ARM_FIRE;
	Extended_State_Machine hispeed_esm; //the actual extended state machine wrapper

	//and some functions to check for state transitions
	bool hispeed_trans_INACTIVE_to_ACTIVE() { return status_onboard_pgood.read(); }				//check our subscription variable to see if power is good
	bool hispeed_trans_ACTIVE_to_INACTIVE() { return !status_onboard_pgood.read(); }			//check our subscription variable to see if power is bad
	bool hispeed_trans_ACTIVE_to_ARM_FIRE() { return command_hispeed_arm_fire_request.check(); }//check the subscription variable to see if we got an arm/fire request
	bool hispeed_trans_ARM_FIRE_to_ACTIVE() { return true; }
	//NOTE: Always return to active state after armed, even in case of power fail
	//		power monitor state variable will bring us into inactive state in case of actual power fail
	//		If I returned to inactive state, will likely return right back to active state, then back to inactive state due to delays in state updating

	//and some state transitions between everything
	//transitions bound to states in constructor
	ESM_Transition hispeed_trans_FROM_INACTIVE[1] = {	{&hispeed_state_ACTIVE, {BIND_CALLBACK(this, hispeed_trans_INACTIVE_to_ACTIVE)}		}	};
	ESM_Transition hispeed_trans_FROM_ACTIVE[2] =   {	{&hispeed_state_INACTIVE, {BIND_CALLBACK(this, hispeed_trans_ACTIVE_to_INACTIVE)}	},
														{&hispeed_state_ARM_FIRE, {BIND_CALLBACK(this, hispeed_trans_ACTIVE_to_ARM_FIRE)}	}	};
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
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_err_sync_timeout);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_err_pwr);
	PERSISTENT((Pub_Var<bool>), status_hispeed_arm_flag_complete);
	Sub_Var_RC<bool> 	command_hispeed_sdram_load_test_sequence;
	Sub_Var_RC<std::array<bool, 4>> 	command_hispeed_SOA_enable;
	Sub_Var_RC<std::array<bool, 4>> 	command_hispeed_TIA_enable;
	Sub_Var_RC<std::array<uint16_t, 4>> command_hispeed_SOA_DAC_drive;
	PERSISTENT((Pub_Var<std::array<uint16_t, 4>>), status_hispeed_TIA_ADC_readback);

	//and let's subscribe to the state of the onboard power monitor
	//to get whether the power rails are good or not (so we can activate/deactivate the subsystem)
	Sub_Var<bool> status_onboard_pgood;
};
