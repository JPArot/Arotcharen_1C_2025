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
/**
 * @file main.c
 * @brief Medidor de distancia por ultrasonido usando sensor HC-SR04, LEDs y LCD.
 *
 * Proyecto desarrollado para la Actividad 1 de la materia de Electrónica Programable.
 * Mide la distancia utilizando un sensor ultrasónico y la representa con:
 * - Un display LCD para mostrar el valor numérico.
 * - LEDs para indicar el rango de distancia.
 * Se controla mediante dos teclas:
 * - TEC1: activa/desactiva la medición.
 * - TEC2: congela el valor medido (modo HOLD).
 */

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

#define PERIODO_MEDICION_MS   1000
#define PERIODO_TECLAS_MS     100

static bool medicion_activa = false;
static bool hold_activo = false;

/**
 * @brief Tarea que realiza mediciones de distancia con el sensor HC-SR04,
 * actualiza el display LCD y enciende LEDs según el rango correspondiente.
 *
 * - Si la distancia es menor a 10 cm: se apagan todos los LEDs.
 * - 10–20 cm: se enciende LED_1.
 * - 20–30 cm: se encienden LED_1 y LED_2.
 * - Más de 30 cm: se encienden LED_1, LED_2 y LED_3.
 *
 * Si el modo HOLD está activo, se mantienen los valores actuales en el display.
 *
 * @param pvParam No utilizado.
 */
void TareaMedicion(void *pvParam)
{
    uint16_t distancia = 0;

    while (1)
    {
        if (medicion_activa)
        {
            distancia = HcSr04ReadDistanceInCentimeters();

            if (!hold_activo)
            {
                LcdItsE0803Write(distancia);
            }

            LedsOffAll();

            if (distancia >= 10 && distancia < 20)
            {
                LedOn(LED_1);
            }
            else if (distancia >= 20 && distancia < 30)
            {
                LedOn(LED_1);
                LedOn(LED_2);
            }
            else if (distancia >= 30)
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

        vTaskDelay(pdMS_TO_TICKS(PERIODO_MEDICION_MS));
    }
}

/**
 * @brief Tarea que gestiona la lectura de teclas:
 * - TEC1: activa o desactiva la medición.
 * - TEC2: activa o desactiva el modo HOLD (solo si la medición está activa).
 *
 * @param pvParam No utilizado.
 */
void TareaTeclas(void *pvParam)
{
    while (1)
    {
        uint8_t tecla = SwitchesRead();

        switch (tecla)
        {
        case SWITCH_1:
            medicion_activa = !medicion_activa;
            if (!medicion_activa)
            {
                hold_activo = false;
            }
            break;

        case SWITCH_2:
            if (medicion_activa)
            {
                hold_activo = !hold_activo;
            }
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(PERIODO_TECLAS_MS));
    }
}

/**
 * @brief Función principal de la aplicación. Inicializa periféricos y crea las tareas.
 */
void app_main(void)
{
    // Inicialización de periféricos
    HcSr04Init(GPIO_3, GPIO_2);
    LedsInit();
    LcdItsE0803Init();
    SwitchesInit();

    // Creación de tareas
    xTaskCreate(&TareaMedicion, "Medicion", 512, NULL, 5, NULL);
    xTaskCreate(&TareaTeclas, "Teclas", 512, NULL, 5, NULL);
}


/*==================[end of file]============================================*/
