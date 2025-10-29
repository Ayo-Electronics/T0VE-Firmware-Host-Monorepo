/*
 * app_hal_timer.cpp
 *
 *  Created on: Jun 30, 2025
 *      Author: govis
 */

#include <app_hal_pwm.hpp>

//================================= STATIC MEMBER VARIABLES ================================
PWM::PWM_Hardware_Channel PWM::SYNCOUT_TIMER = {
    .timer_handle = &htim8,
    .timer_channel = TIM_CHANNEL_1,
	.is_complementary_channel = true,
	.timer_clk_hz = 240e6, //clocking the timer peripheral this fast
    .timer_init_function = MX_TIM8_Init,
    .timer_deinit_function = {}, //don't have the deinit function
    ._TIM_SR = &TIM8->SR
};

PWM::PWM_Hardware_Channel PWM::SYNCIN_TIMER = {
    .timer_handle = &htim1,
    .timer_channel = TIM_CHANNEL_2,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM1_Init,
    .timer_deinit_function = {},
    ._TIM_SR = &TIM1->SR
};

PWM::PWM_Hardware_Channel PWM::CS_ADC_CH0_CHANNEL = {
    .timer_handle = &htim3,
    .timer_channel = TIM_CHANNEL_2,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM3_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM3->SR
};

PWM::PWM_Hardware_Channel PWM::CS_ADC_CH1_CHANNEL = {
    .timer_handle = &htim3,
    .timer_channel = TIM_CHANNEL_4,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM3_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM3->SR
};

PWM::PWM_Hardware_Channel PWM::CS_ADC_CH2_CHANNEL = {
    .timer_handle = &htim3,
    .timer_channel = TIM_CHANNEL_3,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM3_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM3->SR
};

PWM::PWM_Hardware_Channel PWM::CS_ADC_CH3_CHANNEL = {
    .timer_handle = &htim3,
    .timer_channel = TIM_CHANNEL_1,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM3_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM3->SR
};

PWM::PWM_Hardware_Channel PWM::CS_DAC_CH0_CHANNEL = {
    .timer_handle = &htim2,
    .timer_channel = TIM_CHANNEL_4,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM2_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM2->SR
};

PWM::PWM_Hardware_Channel PWM::CS_DAC_CH1_CHANNEL = {
    .timer_handle = &htim2,
    .timer_channel = TIM_CHANNEL_2,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM2_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM2->SR
};

PWM::PWM_Hardware_Channel PWM::CS_DAC_CH2_CHANNEL = {
    .timer_handle = &htim2,
    .timer_channel = TIM_CHANNEL_1,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM2_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM2->SR
};

PWM::PWM_Hardware_Channel PWM::CS_DAC_CH3_CHANNEL = {
    .timer_handle = &htim2,
    .timer_channel = TIM_CHANNEL_3,
    .is_complementary_channel = false,
    .timer_clk_hz = 240e6,
    .timer_init_function = MX_TIM2_Init,
    .timer_deinit_function = {},
	._TIM_SR = &TIM2->SR
};

//================================= CONSTRUCTORS ================================
//constructors just take a reference to the hardware channel and saves it to the class
PWM::PWM(PWM_Hardware_Channel& _hardware) :
		hardware(_hardware), TIM_SR(hardware._TIM_SR)
{}

//================================= INSTANCE METHODS ================================
//initialize the timer by calling the appropriate HAL function
void PWM::init() {
	hardware.timer_init_function();
}  

//deinitialize the timer by calling the appropriate HAL function
void PWM::deinit() {
	hardware.timer_deinit_function();
}

//function to enable the timer
void PWM::enable() {
	//reset the count of the particular channel
    if(hardware.is_complementary_channel) HAL_TIMEx_PWMN_Start(hardware.timer_handle, hardware.timer_channel);
    else HAL_TIM_PWM_Start(hardware.timer_handle, hardware.timer_channel);
}

//function to disable the timer
void PWM::disable() {
	if(hardware.is_complementary_channel) HAL_TIMEx_PWMN_Stop(hardware.timer_handle, hardware.timer_channel);
	else HAL_TIM_PWM_Stop(hardware.timer_handle, hardware.timer_channel);
}

//function to reset the counter
void PWM::reset_count(uint32_t cntval) {
	__HAL_TIM_SET_COUNTER(hardware.timer_handle, cntval);
}

//function to set the duty cycle of the timer
void PWM::set_duty(float duty) {
    //just constrain the duty cycle to 0-1, don't throw errors or anything funky
	duty = clip(duty, 0.0f, 1.0f);

	//get the auto-reload register value (what the period count of the timer is set to)
	uint32_t period = __HAL_TIM_GET_AUTORELOAD(hardware.timer_handle) + 1;  // ARR

	//compute the `pulse` value according to the auto-reload register value
	uint32_t pulse = (uint32_t)(duty * period);
	__HAL_TIM_SET_COMPARE(hardware.timer_handle, hardware.timer_channel, pulse);  // CCRx
}

//function to set the assert time of the PWM signal
void PWM::set_assert_time(float assert_time_s) {
	//compute the time unit of the `COMPARE` register from the TIM frequency and the prescaler
    // The timer's time unit is (PSC+1) / timer_clk_hz
    float time_unit = (hardware.timer_handle->Instance->PSC + 1.0f) / hardware.timer_clk_hz;

    //compute the how many time units are required to hit the required assert time
    //write this value into the `COMPARE` register
    uint32_t assert_time_units = (uint32_t)(assert_time_s / time_unit);
    __HAL_TIM_SET_COMPARE(hardware.timer_handle, hardware.timer_channel, assert_time_units);
}

//function to set the frequency of the PWM signal
void PWM::set_frequency(float freq_hz) {

	// note whether the timer is currently enabled
	bool was_enabled = (hardware.timer_handle->Instance->CR1 & TIM_CR1_CEN) != 0;

    //compute the required total clock division factor required
    float clock_div_factor = (hardware.timer_clk_hz / freq_hz);

    //and compute the optimal prescaler value given the clock division factor
    //since we'd like to maximimze the resolution of the timer, we'll use the lowest prescaler value possible
    //from this we compute the prescaler register value, which is 1 less than the CEIL() of the optimal prescaler value
    //NOTE: there's a slight difference between CEIL() - 1 and FLOOR(), specifically when you're at an integer value
    float opt_prescaler = clock_div_factor/65536.0f;
    uint32_t prescaler_regval = (uint32_t)(clip(std::ceil(opt_prescaler), 1.0f, 65536.0f) - 1.0f);

    //given this prescaler value, we can compute the auto-reload register value to achieve the desired frequency
    float opt_arr = (hardware.timer_clk_hz / (prescaler_regval + 1.0f)) / freq_hz;
    uint32_t arr_regval = (uint32_t)(clip(std::round(opt_arr), 1.0f, 65536.0f) - 1.0f); //don't care about the CEIL() call here, we're just rounding to the nearest integer

    //now we need to correct our compare value to account for our change in frequency
    //let's read our old duty-cycle register value
    //and compute the new compare value to maintain the same duty cycle
    uint32_t old_cmp = __HAL_TIM_GET_COMPARE(hardware.timer_handle, hardware.timer_channel);
    uint32_t old_period = __HAL_TIM_GET_AUTORELOAD(hardware.timer_handle) + 1;
    float new_cmp = old_cmp * ((arr_regval + 1.0f) / old_period);
    uint32_t new_cmp_regval = (uint32_t)(clip(std::round(new_cmp), 0.0f, 65535.0f));
    
    //disable the timer and update the registers with what we computed above
    //set the prescaler and auto-reload register values
    disable();
	__HAL_TIM_SET_PRESCALER(hardware.timer_handle, prescaler_regval);
	__HAL_TIM_SET_AUTORELOAD(hardware.timer_handle, arr_regval);
	__HAL_TIM_SET_COMPARE(hardware.timer_handle, hardware.timer_channel, new_cmp_regval);
	__HAL_TIM_SET_COUNTER(hardware.timer_handle, 0);

    // Force update event to immediately apply the new prescaler and auto-reload values
    //NOTE: this can generate downstream events to trigger, so we may have to do something like
    //  SET_BIT(TIMx->CR1, TIM_CR1_URS), i.e. changing the update request source
    //For now, I'm just letting it be and generating an update event. 
    hardware.timer_handle->Instance->EGR |= TIM_EGR_UG;

    //and if the timer was enabled when we called this function, re-enable the timer
    if(was_enabled) enable();
}

//function to set the period of the PWM signal
void PWM::set_period(float period_s) {
	//compute the frequency from the period
	float freq_hz = 1.0f / period_s;

	//and call the set_frequency function to set the frequency and update the registers
	set_frequency(freq_hz);
}















