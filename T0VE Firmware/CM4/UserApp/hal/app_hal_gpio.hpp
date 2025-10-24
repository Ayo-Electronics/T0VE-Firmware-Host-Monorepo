#pragma once

#include <app_proctypes.hpp>
#include "app_registers.hpp"

class GPIO {
public:
	//================================ HARDWARE REFERENCES TO EACH GPIO CHANNEL ================================

	struct GPIO_Hardware_Pin {
		GPIO_TypeDef* _GPIO_PORT;
		uint32_t _GPIO_PIN;
		uint32_t _GPIO_MODE;
		uint32_t _GPIO_PULL;
		uint32_t _GPIO_SPEED;
	};

	//================================= INSTANCE METHODS ===================================
	void init(); //just calls HAL function that configures the GPIO pin in its default (cube) setting
	void deinit(); //just calls the HAL function that de-initializes the GPIO pin


	//heavily optimize these calls - unroll any loops, inline these functions
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

	//drive the GPIO pin high
	__attribute__((always_inline)) inline void set() noexcept { BSRR_REGISTER = SET_MASK; }

	//drive the GPIO pin low
	__attribute__((always_inline)) inline void clear() noexcept { BSRR_REGISTER = CLEAR_MASK; }

	//read the GPIO pin state
	__attribute__((always_inline)) uint32_t read() noexcept { return (IDR_REGISTER & READ_MASK); }

	#pragma GCC pop_options

	//========================= CONSTRUCTORS, DESTRUCTORS, OVERLOADS =========================
	GPIO(const GPIO_Hardware_Pin& _pin);

	//delete assignment operator and copy constructor
	//in order to prevent hardware conflicts
	GPIO(GPIO const& other) = delete;
	void operator=(GPIO const& other) = delete;


protected:
	static void init_clocking();
	static bool all_init; //flag that determines whether the common I/O hardware has been initialized

	const GPIO_Hardware_Pin pin; //reference to the hardware pin so we can use it for initialization

	Register<uint32_t> BSRR_REGISTER;
	Register<uint32_t> IDR_REGISTER;

	const uint32_t SET_MASK;
	const uint32_t CLEAR_MASK;
	const uint32_t READ_MASK;
};


//================================ CLASS THAT EXTENDS GPIO FUNCTIONALITY TO INCLUDE HARDWARE PWM CONTROL ======================================	
/* NOTE: this interface does not implement the alternate mode functionality! It only supports the
 * GPIO-relevant functionality in pins with alternate modes. Presently, this code supports a pin under either
 * normal GPIO control or a single alternate mode. These alternate modes are defined in the hardware structure,
 * and declared in the `pin_mapping` file.
 * To actually implement the alternate mode functionality, call `configure_mode_alternate()` in this class, and invoke
 * whatever relevant methods in other classes to actually do what you want
 *
 * By default, calling `init()` on this class will just put it in GPIO mode.
 * Downstream needs to call `configure_mode_alternate` to enable alternate mode functions
 * Also, I'm not guarding against any ill-formed calls to read/write/get for the parent GPIO class
 * This avoids the virtualization overhead but allows for potential corrupted writes when GPIO is controlled by peripherals
 */

class GPIO_Alternate : public GPIO {
public:
	//=================== ALTERNATE MODE STRUCTURES ==================
	struct GPIO_Alternate_Hardware_Pin {
		const GPIO_Hardware_Pin _GPIO_INFO;
		const uint32_t _ALTERNATE_MODE;
		const uint32_t _ALTERNATE_INDEX;
	};

	//================================== NEW METHODS ==================================
	void configure_mode_gpio();
	void configure_mode_alternate();

	//=========================== CONSTRUCTORS, DESTRUCTORS, OVERLOADS ===========================
	GPIO_Alternate(const GPIO_Alternate_Hardware_Pin& _pin);

	//delete assignment operator and copy constructor
	//in order to prevent hardware conflicts
	GPIO_Alternate(GPIO_Alternate const& other) = delete;
	//assignment operator deleted for the parent

private:
	const uint32_t ALTERNATE_INDEX; //extra variable that says which alternate function the pin is hooked up to
	const uint32_t ALTERNATE_MODE; //extra variable that says the output mode of the GPIO pin in alternate mode
};
