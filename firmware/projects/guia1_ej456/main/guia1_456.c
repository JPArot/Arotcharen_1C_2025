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
 * @author Jean Pierre Arotcharen (jean.arotcharen@ingenieria.uner.edu.ar)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
 #include <led.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
/*==================[macros and definitions]=================================*/

int8_t  convertToBcdArray (uint32_t data, uint8_t digits, uint8_t * bcd_number)
{

    // Con este for separo en centena decena unidad
	for(uint_t i=0; i<digits; i++){
		bcd_number[digits-1-i]= data%10; 
		data=data/10;
	}
        /*bcd_number[0]= data%10;
		 bcd_number[1]=(data/10)%10;
		 bcd_number[2]=(data/100)%10;
*/ 
   return 0;
}


/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/

/*==================[external functions definition]==========================*/
void app_main(void){
	uint32_t number= 123;
	uint8_t digitos= 3;
	uint8_t arreglo [3];
	convertToBcdArray(number,digitos,&arreglo);

	printf("El primer digito vale %d \n" , arreglo[0]);
	printf("El segundo digito vale %d \n" , arreglo[1]);
	printf("El tercer digito vale %d \n" , arreglo[2]);
	
}
/*==================[end of file]============================================*/