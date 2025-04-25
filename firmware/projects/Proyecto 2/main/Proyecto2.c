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
 * | 11/04/2025 | Document creation		                         |
 *
 * @author Jean Pierre Arotcharen (jean.arotcharen@ingenieria.uner.edu.ar)
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
#include "gpio_mcu.h"
#include "hc_sr04.h"
#include "lcditse0803.h"
#define CONFIG_BLINK_PERIOD_MEDICION 1000
#define CONFIG_BLINK_PERIOD_ESTADOTECLA 1000

/*==================[macros and definitions]=================================*/

bool controlMediciondeproximidadAux = false;
bool controldeHold = false;

/*==================[Tarea de medición de proximidad]========================*/

void MediciondeproximidadTask(void *pvparametro)
{
	uint16_t distanciaactual = 0;

	while (1)
	{
		if (controlMediciondeproximidadAux)
		{
			distanciaactual = HcSr04ReadDistanceInCentimeters();

			if (!controldeHold)
			{
				LcdItsE0803Write(distanciaactual);
			}

			LedsOffAll();

			if (distanciaactual < 10)
			{
				// No prender ningún LED
			}
			else if (distanciaactual < 20)
			{
				LedOn(LED_1);
			}
			else if (distanciaactual < 30)
			{
				LedOn(LED_1);
				LedOn(LED_2);
			}
			else
			{
				LedOn(LED_1);
				LedOn(LED_2);
				LedOn(LED_3);
			}
		}
		else
		{
			LcdItsE0803Off();
			LedsOffAll();
		}

		vTaskDelay(CONFIG_BLINK_PERIOD_MEDICION / portTICK_PERIOD_MS);
		//vTaskDelay(pdMS_TO_TICKS(200));  
	}
}

/*==================[Tarea de lectura de teclas]=============================*/

void EstadodeTeclaTask(void *parametro)
{
	uint8_t teclapresionada;

	while (1)
	{
		teclapresionada = SwitchesRead();

		switch (teclapresionada)
		{
		case SWITCH_1:
			controlMediciondeproximidadAux = !controlMediciondeproximidadAux;
			if (!controlMediciondeproximidadAux)
			{
				controldeHold = false;
			}
			break;

		case SWITCH_2:
			if (controlMediciondeproximidadAux)
			{
				controldeHold = !controldeHold;
			}
			break;
		}

		vTaskDelay(CONFIG_BLINK_PERIOD_ESTADOTECLA / portTICK_PERIOD_MS);
		//vTaskDelay(pdMS_TO_TICKS(150));  
	}
}

/*==================[Función principal]=====================================*/

void app_main(void)
{
	// Inicializaciones
	HcSr04Init(GPIO_3, GPIO_2);
	LedsInit();
	LcdItsE0803Init();

	// Activar interrupciones de teclas (si se usan en otra parte)
	//SwitchActivInt(SWITCH_1, NULL, NULL);
	//SwitchActivInt(SWITCH_2, NULL, NULL);

	// Crear tareas
	xTaskCreate(&MediciondeproximidadTask, "TareaMedicion", 512, NULL, 5, NULL);
	xTaskCreate(&EstadodeTeclaTask, "TareaTeclas", 512, NULL, 5, NULL);
}

/*==================[end of file]============================================*/
