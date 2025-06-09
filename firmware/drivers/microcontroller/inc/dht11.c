/*! @file dht11.c
/** \brief Driver for reading humiduty and temperature with DHT11.
 *
 * @note Temperature range: 0 / +50°C ±2°C
 * 
 * @note Humidity range: 20-90%RH ±5%RH
 *
 * @author Juani Cerrudo

 *
 * @note Hardware connections:
 *
 * |   	DHT11		|   ESP-EDU 	|
 * |:--------------:|:--------------|
 * | 	VCC     	|	3V3     	|
 * | 	DATA	 	| 	GPIO_#		|
 * | 	GND		 	| 	GND 		|
 *
 * @section changelog
 *
 * |   Date	    | Description                                    						|
 * |:----------:|:----------------------------------------------------------------------|
 * | 20/05/2025 | Document creation
 */
/*==================[inclusions]=============================================*/
#include "dht11.h"
#include "gpio_mcu.h"
#include "delay_mcu.h"

/*==================[macros and definitions]=================================*/
#define DHT11_TIMEOUT_MAX			(1000)
#define DHT11_LEN_dht11_ticks_array	(82)
#define DHT11_LEN_dht11_byte		(5)

/*==================[internal data declaration]==============================*/
static uint32_t 	dht11_ticks_array[DHT11_LEN_dht11_ticks_array];
static uint8_t 		dht11_byte[DHT11_LEN_dht11_byte];

gpio_t dht11Pin;

enum dht11_state_e {
	dht11_state_start,
	dht11_state_low,
	dht11_state_high,
	dht11_state_timeout,
	dht11_state_error,
	dht11_state_end,
};


/*==================[internal functions declaration]=========================*/
static void dht11_GPIO_Low(void){
	GPIOInit(dht11Pin, GPIO_OUTPUT);
	GPIOOff(dht11Pin);
}

static void dht11_GPIO_High(void){
   GPIOInit(dht11Pin, GPIO_INPUT);
}

static _Bool dht11_GPIO_Read(void) {
	return GPIORead( dht11Pin );
}

uint32_t dht11_timeout;
uint32_t dht11_timeout_max;
static void dht11_TimeOutReset(uint32_t max) {
	if(0 < max) {
		dht11_timeout_max = max;
	} else {
		dht11_timeout_max = DHT11_TIMEOUT_MAX;
	}
	dht11_timeout = dht11_timeout_max;
}

static _Bool dht11_TimeOutCheck(void) {
	if(0 < dht11_timeout) {
		dht11_timeout--;
		return true;
	}
	dht11_TimeOutReset(0);
	return false;
}

static _Bool dht11_StartRead(void) {
	uint8_t  state          = dht11_state_start;
	_Bool   flag_loop_end	= false;
	_Bool   flag_timeout   = false;
	_Bool   flag_error	   = false;
	uint32_t n_tick         = 0;
	uint32_t n_bit          = 0;

   (void) flag_timeout;   // Use a variable to not produce compiler Warnings
   (void) flag_error;     // Use a variable to not produce compiler Warnings
   
	dht11_GPIO_Low();
	DelayMs(20);
	dht11_GPIO_High();

	while(false == flag_loop_end) {
		switch(state) {
		case dht11_state_start:

			dht11_TimeOutReset(DHT11_TIMEOUT_MAX);
			while(dht11_state_start == state) {
				if(dht11_GPIO_Read() == false) {
					state = dht11_state_low;
				}
				if(!dht11_TimeOutCheck()) {
					state = dht11_state_timeout;
				}
			}
			break;

		case dht11_state_low:

			dht11_TimeOutReset(0);
			while(dht11_state_low == state) {
				n_tick++;
				if(dht11_GPIO_Read() == true) {
					dht11_ticks_array[n_bit] = n_tick;
					n_bit++;
					n_tick = 0;
					state = dht11_state_high;
				}

				if(!dht11_TimeOutCheck()) {
					state = dht11_state_timeout;
				}
			}
			break;

		case dht11_state_high:

			dht11_TimeOutReset(0);
			while(dht11_state_high == state) {
				n_tick++;
				if(dht11_GPIO_Read() == false) {
					dht11_ticks_array[n_bit] = n_tick;
					n_bit++;
					n_tick = 0;
					state = dht11_state_low;
				}

				if(!dht11_TimeOutCheck()) {
					state = dht11_state_timeout;
				}
			}
			break;

		case dht11_state_timeout:
			flag_timeout = true;
			state = dht11_state_end;
			break;

		case dht11_state_end:
			flag_loop_end = true;
			break;

		default:
		case dht11_state_error:
			flag_error = true;
			state = dht11_state_end;
			break;
		}


		if(DHT11_LEN_dht11_ticks_array <= n_bit) {
			state = dht11_state_end;
		}
	}

	if(82 == n_bit) {
		return true;
	}

	return false;
}

static _Bool dht11_ProcessData(void) {
	int i, i_i, i_f, j;
	uint32_t valf, valt;

	valf = dht11_ticks_array[0];
	valt = dht11_ticks_array[1];
	for(i = 2; i < 81; i++) {
		if((valf <= dht11_ticks_array[i]) || (valt <= dht11_ticks_array[i])) {
			return false;
		}
	}

	i_i = 2;
	for(j = 0; j < DHT11_LEN_dht11_byte; j++) {
		dht11_byte[j] = 0x00;
		i_f = i_i + 8 * 2 - 1;
		for(i = i_i; i < i_f; i = i + 2) {
			valf = dht11_ticks_array[i];
			valt = dht11_ticks_array[i+1];

			if(valt < valf) {
				dht11_byte[j] = (dht11_byte[j] << 1);
			} else {
				dht11_byte[j] = (dht11_byte[j] << 1) | 0x01;
			}
		}
		i_i = i_f + 1;
	}

	uint8_t crc;
	crc = dht11_byte[0] + dht11_byte[1] + dht11_byte[2] + dht11_byte[3];
	if(crc != dht11_byte[4]) {
		return false;
	}

	return true;
}
/*==================[internal data definition]===============================*/

/*==================[external data definition]===============================*/

/*==================[internal functions definition]==========================*/

/*==================[external functions definition]==========================*/



void dht11Init( gpio_t gpio ){
   dht11Pin = gpio;
   dht11_GPIO_High();
}

_Bool dht11Read( float *phum, float *ptemp ){
	if(true == dht11_StartRead()) {
		if(true == dht11_ProcessData()) {
			*phum 	= ((float)dht11_byte[0]) + ((float)dht11_byte[1])/10;
			*ptemp 	= ((float)dht11_byte[2]) + ((float)dht11_byte[3])/10;
			return true;
		}
	}
	return false;
}
