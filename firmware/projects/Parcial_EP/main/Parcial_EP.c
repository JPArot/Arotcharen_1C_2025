/*! @mainpage Template
 *
 * @section Este programa debe detectar el riesgo de nevada y de radiacion excesiva
 * y enviar por UART con un formato determinado
 * 
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
#include <gpio_mcu.h>
#include "uart_mcu.h"
#include "analog_io_mcu.h"
#include "uart_mcu.h"
#include "timer_mcu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "string.h"
#include "switch.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h" 
#include "driver/adc.h" 
#include "driver/gpio.h" 
#include "nvs_flash.h"
#include "ble_mcu.h"
#include "pwm_mcu.h"
#include "gpio_mcu.h"
#include "analog_io_mcu.h"
#include "timer_mcu.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
#include "gpio_mcu.h"
#include "lcditse0803.h"
#include <hc_sr04.h>
#include "delay_mcu.h"
#include "timer_mcu.h"


#define GPIO

/*==================[macros and definitions]=================================*/
/** @def AD_SAMPLE_PERIOD
 * @brief Establece un periodo de medicion, expresado en milisegundos
 * se mide cada 1 segundos
 */
#define AD_SAMPLE_PERIOD 1000 // el sensor de humedad y temperatura cada 1 segundo

/** @def SHOW_PERIOD
 * @brief Establece un periodo con el que se muestran los datos, expresado en milisegundos
 * se muestran cada 10 segundos
 */
#define SHOWH_PERIOD 10000	  // se muestra por el puerto serie el estado de las variables cada 1s

/** @def RADIACION_PERIOD
 * @brief Establece un periodo con el que se sensa la radiacion cada 5 segundos
 */
#define RADIACION_PERIOD 5000

/** @def Humety
 * variable donde se almacena el valor actual de la humedad
*/
uint32_t Humety;

/** @def Temperature
 * variable donde se almacena el valor actual de la temperatura
*/
uint32_t Temperature;

/** @def Temperature
 * variable donde se almacena el valor actual de la temperatura
*/
uint32_t Radiation;


/** @def nevada
 * variable tipo BOOL donde se almacena la nevada
*/
bool nevada=false;	//

/** @def radioactivo
 * variable tipo BOOL donde se almacena la radioactividdad
*/
bool radioactividad=false;	//

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/


/** @fn gpioConf_t
 * @brief Estructura que representa un puerto GPIO
 * @param pin numero de pin del GPIO
 * @param dir direccion del GPIO; '0' entrada ;  '1' salida
 */
typedef struct
{
	gpio_t pin; /*!< GPIO pin number */
	io_t dir;	/*!< GPIO direction '0' IN;  '1' OUT*/
} gpioConf_t;

/** @fn Control
 * @brief Esta funcion controla el dispositivo
 * @param pvParameter
 * @param pin vector de estructuras del tipo gpioConf_t
 */

/*!
 * @fn MedirTemperatura
 * @brief Sensado analogico de temperatura y conversion a grados
 */
void MedirTemperatura()
{
	uint16_t valor;

	AnalogInputRead(CH1, &valor);

	Temperature = (valor * 100) / 3.3;
	
}
/*!
 * @fn MedirHumedad
 * @brief Sensado analogico de humedad y conversion a porcentaje
 */
void MedirHumedad()
{
	uint16_t valor;

	AnalogInputRead(CH2, &valor);

	Humety = (valor / 3.3)*100;
}


/*!
 * @fn MedirTemperatura
 * @brief Sensado analogico de Radiacion y conversion a mili Radios
 */
void MedirRadiacion()
{
	uint16_t valor;

	AnalogInputRead(CH3, &valor);

	Radiation = (valor * 100) / 3300;
}
static void Control_nevada(){
   bool nevadaaux=true;
	if(Humety>85 && Temperature<2){
        UartSendString(UART_PC, (char*)UartItoa(Humety, 10));
		UartSendString(UART_PC, " Porciento \r\n");
		UartSendString(UART_PC, (char*)UartItoa(Temperature, 10));
		UartSendString(UART_PC, " °C\r\n");
		UartSendString(UART_PC, "RIESGO DE NEVADA.");  
		nevada=nevadaaux;
	}
	else
	{
		UartSendString(UART_PC, (char*)UartItoa(Humety, 10));
		UartSendString(UART_PC, " Porciento \r\n");
		UartSendString(UART_PC, (char*)UartItoa(Temperature, 10));
		UartSendString(UART_PC, " °C\r\n");
	}
}
static void Control_Radiacion(){
bool radiacionaux;
if(Radiation>40){
UartSendString(UART_PC, (char*)UartItoa(Radiation, 10));
UartSendString(UART_PC, " mR/h \r\n");
UartSendString(UART_PC, "RADIACION ELEVADA.");
radioactividad=radiacionaux;
}
else{
UartSendString(UART_PC, (char*)UartItoa(Radiation, 10));
UartSendString(UART_PC, " mR/h \r\n");
}
}

void Controlsalir(void *param){
	while (1) {
        // 1. Verifica nevada
		Control_nevada();
		Control_Radiacion();
        if (nevada==true) {
                LedOn(LED_3); LedOff(LED_2); LedOff(LED_1);

            } else if (radioactividad == true) {
                LedOff(LED_1); LedOn(LED_2); LedOff(LED_3);
            } else {
                LedOn(LED_1); LedOff(LED_2); LedOff(LED_3);
            }
 
}

}