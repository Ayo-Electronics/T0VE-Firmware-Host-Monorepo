/*
 * app_hal_timer.hpp
 *
 *  Created on: Jun 30, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp" //for callback function


#include "tim.h" //HAL Timer Stuff
#include "app_registers.hpp" //to read timer status register

class PWM {
public:

	//================================ TYPEDEFS ================================
	struct PWM_Hardware_Channel {
		TIM_HandleTypeDef* const timer_handle;
		const uint32_t timer_channel;
		const bool is_complementary_channel;
		const float timer_clk_hz;
		const Callback_Function<> timer_init_function;
		const Callback_Function<> timer_deinit_function;
		volatile uint32_t* _TIM_SR; //timer status register address
	};

	//the most basic timer just for generating a square-wave synchronization signal
	static PWM_Hardware_Channel SYNCOUT_TIMER;

	//these two channels are used to trigger SPI writes and reads, respectively
	//these operate in one-shot mode, triggered by SYNC_IN
	//these also forward the trigger signal to the timers associated with the CS controls of the ADC/DACs
	//they don't generate outputs, but can still be controlled using the same interface
	static PWM_Hardware_Channel SYNCIN_TIMER;

	//these ones also operate in one-shot mode and will control the CS lines of the ADC/DAC
	static PWM_Hardware_Channel CS_ADC_CH0_CHANNEL;
	static PWM_Hardware_Channel CS_ADC_CH1_CHANNEL;
	static PWM_Hardware_Channel CS_ADC_CH2_CHANNEL;
	static PWM_Hardware_Channel CS_ADC_CH3_CHANNEL;
	static PWM_Hardware_Channel CS_DAC_CH0_CHANNEL;
	static PWM_Hardware_Channel CS_DAC_CH1_CHANNEL;
	static PWM_Hardware_Channel CS_DAC_CH2_CHANNEL;
	static PWM_Hardware_Channel CS_DAC_CH3_CHANNEL;

	//================================= INSTANCE METHODS ================================

	void init();
	void deinit();
	void enable();
	void disable();
	void reset_count(uint32_t cntval = 0);
	void set_duty(float duty);
	void set_assert_time(float assert_time_s); //useful for ADC/DAC control/select lines
	void set_frequency(float freq_hz);
	void set_period(float period_s); //set the period of the timer, high level function
	
	#pragma GCC push_options
	#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

	//heavily optimized functions that read whether a certain timer has been triggered
	__attribute__((always_inline)) inline uint32_t get_triggered() { return (uint32_t)TIM_SR & TIM_SR_TIF; }
	__attribute__((always_inline)) inline void reset_triggered() { TIM_SR = ~TIM_SR_TIF; }

	#pragma GCC pop_options

	// Get timer handle for direct register access
	TIM_HandleTypeDef* get_timer_handle() { return hardware.timer_handle; }

	//================================= CONSTRUCTORS ================================
	PWM(PWM_Hardware_Channel& _hardware); //Cconstructure just takes a reference to the hardware channel
	PWM(PWM const& other) = delete;	//delete the copy constructor to prevent any issues with hardware channel writing conflicts
	PWM& operator=(PWM const& other) = delete; //delete the copy assignment operator to prevent any issues with hardware channel writing conflicts

private:
	PWM_Hardware_Channel& hardware;
	Register<uint32_t> TIM_SR;
};
