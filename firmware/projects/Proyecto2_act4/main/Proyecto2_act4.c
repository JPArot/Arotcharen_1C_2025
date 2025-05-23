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
 * @author Jean Pierre Arotcharen(jean.arotcharen@ingenieria.uner.edu.ar)
 *
 */



/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "analog_io_mcu.h"
#include "uart_mcu.h"
#include "timer_mcu.h"
#include "led.h"
#include "switch.h"
#include "gpio_mcu.h"
#include "lcditse0803.h"
#include <hc_sr04.h>
#include "delay_mcu.h"

#define FRECUENCIA_MUESTREO_HZ 500                        // Frecuencia de muestreo: 500 Hz
#define TIEMPO_MUESTREO_US     (1000000 / FRECUENCIA_MUESTREO_HZ) // Periodo en microsegundos: 2 ms

/*==================[global variables]====================================*/

uint16_t valor_analogico = 0;   // Variable global para almacenar el valor leído del potenciómetro

TaskHandle_t LeerYEnviar_task_handle = NULL;

/*==================[internal data definition]===============================*/

/**
 * @brief Tarea que lee la señal analógica del potenciómetro (CH1) y la envía por UART.
 * acordate lo que decia del formato ">brightness:VALOR\r\n"
 */
void LeerYEnviar_task(void *pvParameter) {
    char buffer[32];  // Buffer de texto para preparar la línea a enviar

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // timer

        AnalogInputReadSingle(CH1, &valor_analogico);  // Lectura de ADC en CH1

        sprintf(buffer, ">brightness:%d\r\n", valor_analogico);  // Formato del dato como string
        UartSendString(UART_PC, buffer);  // Envío por UART
    }
}

/**
 * @brief Función llamada desde ISR del timer para notificar la tarea de lectura y envío.
 */
void FuncTimerMuestreo(void *param) {
    vTaskNotifyGiveFromISR(LeerYEnviar_task_handle, pdFALSE);
}

/*==================[main function]=======================================*/

/**
 * @brief Función principal que inicializa periféricos, tareas y timers.
 */
void app_main(void) {

    // Inicialización del UART para transmitir a la PC (Serial Plotter)
    serial_config_t uart_config = {
        .port = UART_PC,
        .baud_rate = 9600,
        .func_p = NULL,
        .param_p = NULL
    };
    UartInit(&uart_config);

    // Configuración del canal analógico CH1 par potenciómetro
    analog_input_config_t adc_config = {
        .input = CH1,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL
    };
    AnalogInputInit(&adc_config);

    // Configuración del timer para generar interrupciones cada 2 ms (500 Hz)
    timer_config_t timer_muestreo = {
        .timer = TIMER_A,
        .period = TIEMPO_MUESTREO_US,
        .func_p = FuncTimerMuestreo,
        .param_p = NULL
    };
    TimerInit(&timer_muestreo);

    // Creación de la tarea para leer el ADC y enviar los datos por UART
    xTaskCreate(&LeerYEnviar_task, "LeerYEnviar", 2048, NULL, 5, &LeerYEnviar_task_handle);

    // Inicio del timer
    TimerStart(timer_muestreo.timer);
}

