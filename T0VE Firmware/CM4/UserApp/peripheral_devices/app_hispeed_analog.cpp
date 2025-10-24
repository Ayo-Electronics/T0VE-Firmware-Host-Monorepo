/*
 * app_hispeed_analog.cpp
 *
 *  Created on: Aug 25, 2025
 *      Author: govis
 */

#include "app_hispeed_analog.hpp"
#include "app_hal_tick.hpp" //for us delay


//create our hardware from the initialization structures passed in
Hispeed_Analog::Hispeed_Analog(HiSpeed_SPI::SPI_Hardware_Channel& _bus,
                               GPIO_Alternate::GPIO_Alternate_Hardware_Pin _cs_dac,
                               GPIO_Alternate::GPIO_Alternate_Hardware_Pin _cs_adc)
    : bus(_bus), cs_dac(_cs_dac), cs_adc(_cs_adc)
{}

//nothing really going on in `init()` defer most to `activate()`
void Hispeed_Analog::init() {}

//call this when analog rails go up
void Hispeed_Analog::activate() {
    //initialize the I/O pins--put them into GPIO mode by default
	//drive them high after initialization
	cs_dac.init();
	cs_adc.init();

	//make sure the chip select lines are in disarmed mode upon activation
	disarm();

	//init the SPI bus
	bus.init();

	//write out zeros to the DAC to ensure safe state
	//this will stall the calling thread until write completes
	write(0);
}

//call this when analog rails go down
void Hispeed_Analog::deactivate() {
	//skip this section if the bus is deinitialized
	//assessing by checking whether the peripheral is enabled
	if(bus.is_init()) {
		//do one final write to put the DACs into zero-scale for a little better power-on behavior
		//stall until write completes
		write(0);

		//ensure the bus is idle and RX FIFO drained before shutdown
		bus.purge();

		//then de-init the SPI bus
		bus.deinit();
	}

	//and tri-state the chip-select lines
	cs_dac.deinit();
	cs_adc.deinit();
}

void Hispeed_Analog::arm() {
    //just need to put the chip-select I/Os in alternate mode
	//for timer control
	cs_dac.configure_mode_alternate();
	cs_adc.configure_mode_alternate();
}

void Hispeed_Analog::disarm() {
    //put the chip-select I/Os back in normal GPIO mode
	cs_dac.configure_mode_gpio();
	cs_adc.configure_mode_gpio();

	//and set them high
	cs_dac.set();
	cs_adc.set();
}

//==================================== LOW SPEED TRANSFERRING ==================================

uint16_t Hispeed_Analog::read() {
    //stage an ADC conversion -- strobe the ADC chip select pin
	cs_adc.clear();
	Tick::delay_us(5); //setup time, very conservative
	cs_adc.set();
	Tick::delay_us(5); //hold time, very conservative

	//and now pull data outta the ADC -- activate only the ADC chip select
	cs_adc.clear();
	uint16_t adc_val = bus.transfer(0);
	cs_adc.set();

	//return the ADC value
	return adc_val;
}

void Hispeed_Analog::write(uint16_t dac_val) {
    //simply drive the DAC chip select low, shift out data, then drive it high again
	cs_dac.clear();
	bus.transfer(dac_val);
	cs_dac.set();
}


uint16_t Hispeed_Analog::transfer(uint16_t dac_val) {
    write(dac_val);
    return read();
}

