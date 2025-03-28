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
 * @author Arotcharen Jean Pierre (jean.Arotcharen@ingenieria.uner.edu.ar)
 *
 */

 #include <stdio.h>
 #include <stdint.h>
 #include <led.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 
 /*==================[Macros y Definiciones]=================================*/
 #define ON 1
 #define OFF 0
 #define TOGGLE 2
 #define CONFIG_BLINK_PERIOD 100  // Período base para el parpadeo
 
 /*==================[Definición de Estructura]===============================*/
 // Definimos una estructura para manejar los LEDs
 struct leds {
	 uint8_t mode;      // Indica el modo de operación: ON, OFF, TOGGLE
	 uint8_t n_led;     // Número del LED a controlar (1, 2 o 3)
	 uint8_t n_ciclos;  // Cantidad de ciclos de encendido/apagado en modo TOGGLE
	 uint16_t periodo;  // Tiempo de cada ciclo en modo TOGGLE
 } my_leds; 
 
 /*==================[Función para Controlar el LED]==========================*/
 // Esta función recibe un puntero a la estructura `leds` y ejecuta la acción correspondiente
 void controlarLED(struct leds *led) {
	 switch (led->mode) {
		 case ON:
			 // Encender el LED correspondiente
			 if (led->n_led == 1) {
				 LedOn(LED_1);
			 } else if (led->n_led == 2) {
				 LedOn(LED_2);
			 } else if (led->n_led == 3) {
				 LedOn(LED_3);
			 } else {
				 printf("Número de LED no válido: %d\n", led->n_led);
			 }
			 break;
 
		 case OFF:
			 // Apagar el LED correspondiente
			 if (led->n_led == 1) {
				 LedOff(LED_1);
			 } else if (led->n_led == 2) {  // Corregido: antes verificaba `n_led == 1`
				 LedOff(LED_2);
			 } else if (led->n_led == 3) {  // Corregido: antes verificaba `n_led == 1`
				 LedOff(LED_3);
			 } else {
				 printf("Número de LED no válido: %d\n", led->n_led);
			 }
			 break;
 
		 case TOGGLE:
			 // Alternar el estado del LED según la cantidad de ciclos especificada
			 for (int i = 0; i < led->n_ciclos; i++) {
				 switch (led->n_led) {
					 case 1:
						 LedToggle(LED_1);
						 break;
					 case 2:
						 LedToggle(LED_2);
						 break;
					 case 3:
						 LedToggle(LED_3);
						 break;
					 default:
						 printf("Número de LED no válido: %d\n", led->n_led);
						 break;  // Se eliminó el `return;` innecesario
				 }
				 vTaskDelay(led->periodo / portTICK_PERIOD_MS); // Espera entre toggles
			 }
			 break;
 
		 default:
			 printf("Modo de LED no válido: %d\n", led->mode);
			 break;
	 }
 }
 
 /*==================[Función Principal]======================================*/
 void app_main(void) {
	 LedsInit();  // Inicializa los LEDs antes de usarlos
 
	 // Configuración de la estructura para el control de LEDs
	 my_leds.n_ciclos = 10;  // Cantidad de veces que se alternará el LED en TOGGLE
	 my_leds.periodo = 500;  // Período en milisegundos entre cada toggle
	 my_leds.n_led = 1;      // LED que será controlado
	 my_leds.mode = TOGGLE;  // Modo de operación
 
	 controlarLED(&my_leds);  // Llamada a la función para ejecutar la acción
 }
 