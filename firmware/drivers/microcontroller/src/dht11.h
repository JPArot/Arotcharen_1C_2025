#ifndef _DHT11_H_
#define _DHT11_H_

/*! @file dht11.h
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

#include "gpio_mcu.h"
#include "stdbool.h"

/*==================[macros]=================================================*/

/*==================[typedef]================================================*/

/*==================[external data declaration]==============================*/

/*==================[external functions declaration]=========================*/
/**
 * @brief DHT11 initialization.
 * 
 * @param gpio GPIO number for data pin
 * @return nothing 
 */
void dht11Init( gpio_t gpio );


/**
 * @brief Read temperature and humidity
 * @param phum pointer to store humidity value
 * @param ptemp pointer to store temperature value
 * @return true if read was successful, false otherwise
 */
_Bool dht11Read( float *phum, float *ptemp );

#endif /* _DHT11_H_ */

 /** @} doxygen end group definition */
 /** @} doxygen end group definition */
 /** @} doxygen end group definition */