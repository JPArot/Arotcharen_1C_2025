/*! @mainpage Template
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	PIN_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 12/09/2023 | Document creation		                         |
 *
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
/*==================[macros and definitions]=================================*/
#define CONFIG_BLINK_PERIOD 1000 // periodo de parpadeo en ms
/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/

/*==================[external functions definition]==========================*/
void app_main(void){
	uint8_t teclas; // variable que almacena teclas presionadas
	LedsInit(); // inicializa leds
	SwitchesInit(); // inicializa teclas
    while(1)    {
    	teclas  = SwitchesRead(); 
    	switch(teclas){
    		case SWITCH_1:
    			LedToggle(LED_1);
    		break;
    		case SWITCH_2:
    			LedToggle(LED_2);
    		break;
			case (SWITCH_1 | SWITCH_2):
			LedToggle(LED_3);
			break;
			default:
			LedsOffAll();
			break;
    	}
	    
		vTaskDelay(CONFIG_BLINK_PERIOD / portTICK_PERIOD_MS); // espera 1 s (1000 ms / 10 ms por tick = 100 ticks)
	}
}
/*==================[end of file]============================================*/