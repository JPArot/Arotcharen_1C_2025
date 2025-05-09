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
/**
 * @file main.c
 * @brief Proyecto: Medidor de distancia por ultrasonido con interrupciones.
 * 
 * Este proyecto utiliza un sensor HC-SR04, una pantalla LCD ITSE0803 y 3 LEDs para mostrar la distancia medida.
 * Utiliza interrupciones para el control de teclas y timers para el control de medición y visualización.
 * 
 * - SWITCH_1 (TEC1): Inicia/detiene la medición.
 * - SWITCH_2 (TEC2): Mantiene la última medición (HOLD).
 * - REFRESCO_MED: Timer que dispara medición cada 1s.
 * - REFRESCO_LCD: Timer que actualiza LCD y LEDs cada 100ms.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
#include "gpio_mcu.h"
#include "lcditse0803.h"
#include "hc_sr04.h"
#include "timer_mcu.h"

/*==================[Macros]=================================================*/
/** @brief Periodo de medición en microsegundos */
#define REFRESCO_MED 1000000

/** @brief Periodo de actualización de LCD y LEDs en microsegundos */
#define REFRESCO_LCD 100000

/*==================[Variables globales]=====================================*/
static bool start = false;       /**< Flag que indica si la medición está activa */
static bool hold = false;        /**< Flag para mantener la última medición */
static int distancia = 0;        /**< Variable global para almacenar la distancia medida */

static TaskHandle_t Medir_task_handle = NULL;     /**< Manejador de la tarea de medición */
static TaskHandle_t Mostrar_task_handle = NULL;   /**< Manejador de la tarea de visualización */

/*==================[Tareas]=================================================*/

/**
 * @brief Tarea dedicada a realizar mediciones con el sensor HC-SR04.
 * 
 * La tarea espera ser notificada mediante un timer y actualiza la variable global `distancia`.
 * Solo mide si `start` está activo.
 */
static void TareaMedir(void *pvParameter)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (start)
        {
            distancia = HcSr04ReadDistanceInCentimeters();
        }
    }
}

/**
 * @brief Tarea encargada de mostrar la distancia en el LCD y controlar los LEDs.
 * 
 * Se ejecuta mediante notificación por timer. Aplica lógica de encendido de LEDs según rangos.
 * No actualiza el LCD si `hold` está activo.
 */
static void TareaMostrar(void *pvParameter)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (start)
        {
            if (!hold)
            {
                LcdItsE0803Write(distancia);
            }

            if (distancia < 10)
            {
                LedsOffAll();
            }
            else if (distancia < 20)
            {
                LedOn(LED_1);
                LedOff(LED_2);
                LedOff(LED_3);
            }
            else if (distancia < 30)
            {
                LedOn(LED_1);
                LedOn(LED_2);
                LedOff(LED_3);
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
    }
}

/*==================[Callbacks de Timer]=====================================*/

/**
 * @fn void FuncTimerMedir(void *param)
 * @brief Envia una notificación a la tarea de medición.
 */
static void FuncTimerMedir(void *param)
{
    vTaskNotifyGiveFromISR(Medir_task_handle, pdFALSE);
}

/**
 * @fn void FuncTimerMostrar(void *param)
 * @brief Envia una notificación a la tarea de visualización.
 */
static void FuncTimerMostrar(void *param)
{
    vTaskNotifyGiveFromISR(Mostrar_task_handle, pdFALSE);
}

/*==================[Callbacks de teclas]====================================*/

/**
 * @fn void ToggleMedicion(void)
 * @brief Alterna el estado de la medición.
 */
static void ToggleMedicion(void)
{
    start = !start;

    if (!start)
    {
        hold = false;  // Resetea HOLD al detener medición
    }
}

/**
 * @fn void ToggleHold(void)
 * @brief Alterna el estado de "HOLD" si la medición está activa.
 */
static void ToggleHold(void)
{
    if (start)
    {
        hold = !hold;
    }
}

/*==================[Función principal]======================================*/

/**
 * @fn void app_main(void)
 * @brief Función principal. Configura perifericos, timers, teclas y crea tareas.
 */
void app_main(void)
{
    // Inicialización de periféricos
    LedsInit();
    HcSr04Init(GPIO_3, GPIO_2);
    SwitchesInit();
    LcdItsE0803Init();

    // Configuración e inicialización de timers
    timer_config_t timer_medir = {
        .timer = TIMER_A,
        .period = REFRESCO_MED,
        .func_p = FuncTimerMedir,
        .param_p = NULL
    };
    TimerInit(&timer_medir);

    timer_config_t timer_mostrar = {
        .timer = TIMER_B,
        .period = REFRESCO_LCD,
        .func_p = FuncTimerMostrar,
        .param_p = NULL
    };
    TimerInit(&timer_mostrar);

    // Activación de interrupciones por teclas
    SwitchActivInt(SWITCH_1, ToggleMedicion, NULL);
    SwitchActivInt(SWITCH_2, ToggleHold, NULL);

    // Creación de tareas
    xTaskCreate(&TareaMedir, "medir", 512, NULL, 5, &Medir_task_handle);
    xTaskCreate(&TareaMostrar, "mostrar", 512, NULL, 5, &Mostrar_task_handle);

    // Inicio de timers
    TimerStart(timer_medir.timer);
    TimerStart(timer_mostrar.timer);
}

/*==================[end of file]============================================*/