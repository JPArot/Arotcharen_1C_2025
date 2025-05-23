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
 * @author Jean Pierre Arotcharen (Jean.arotcharen@ingenieria.uner.edu.ar)
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

/*==================[macros and definitions]=================================*/

#define FREC_MUESTREO_AD_US 20000     // 500 Hz -> 2ms
#define FREC_REPRO_DA_US    40000     // 250 Hz -> 4ms con esto visualizaba pausado
#define BUFFER_SIZE 231             // Tamaño de la señal ECG cargada

/*==================[global variables]======================================*/

TaskHandle_t ConversorAD_task_handle = NULL;   // leer CH1 y enviar UART
TaskHandle_t ConversorDA_task_handle = NULL;   // escribir señal ECG por DAC

/** 
 * @brief Vector con señal digital de ECG, que se transforma en señal analógica.
 */
const char ecg[BUFFER_SIZE] = {
    76, 77, 78, 77, 79, 86, 81, 76, 84, 93, 85, 80, 89, 95, 89, 85, 93, 98, 94, 88,
    98, 105, 96, 91, 99, 105, 101, 96, 102, 106, 101, 96, 100, 107, 101, 94, 100, 104,
    100, 91, 99, 103, 98, 91, 96, 105, 95, 88, 95, 100, 94, 85, 93, 99, 92, 84, 91, 96,
    87, 80, 83, 92, 86, 78, 84, 89, 79, 73, 81, 83, 78, 70, 80, 82, 79, 69, 80, 82, 81,
    70, 75, 81, 77, 74, 79, 83, 82, 72, 80, 87, 79, 76, 85, 95, 87, 81, 88, 93, 88, 84,
    87, 94, 86, 82, 85, 94, 85, 82, 85, 95, 86, 83, 92, 99, 91, 88, 94, 98, 95, 90, 97,
    105, 104, 94, 98, 114, 117, 124, 144, 180, 210, 236, 253, 227, 171, 99, 49, 34, 29,
    43, 69, 89, 89, 90, 98, 107, 104, 98, 104, 110, 102, 98, 103, 111, 101, 94, 103, 108,
    102, 95, 97, 106, 100, 92, 101, 103, 100, 94, 98, 103, 96, 90, 98, 103, 97, 90, 99,
    104, 95, 90, 99, 104, 100, 93, 100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105,
    99, 103, 108, 99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82, 88, 77, 69, 75, 79, 74,
    67, 71, 78, 72, 67, 73, 81, 77, 71, 75, 84, 79, 77, 77, 76, 76
};

/*==================[tasks declaration]=====================================*/

/**
 * @brief Tarea que simula un DAC escribe valores del vector ECG como señal analógica.
 */
void ConversorDA_task(void *pvParameter) {
    uint8_t indice = 0;

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Espera la interrupción del timer
        AnalogOutputWrite(ecg[indice]);          // Escritura al DAC
        indice++;

        if (indice >= BUFFER_SIZE) indice = 0;   // Reinicia al llegar al final
    }
}

/**
 * @brief Tarea que lee la señal analógica generada y la transmite por UART.
 * la voy a ver en el Serial Plotter.
 */
void ConversorAD_task(void *pvParameter) {
    uint16_t valor = 0;
    char buffer[32];

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // Espera la interrupción del timer
        AnalogInputReadSingle(CH1, &valor);        // Lectura desde CH1

        sprintf(buffer, ">brightness:%d\r\n", valor); 
        UartSendString(UART_PC, buffer);           // Envío por UART
    }
}


/**
 * @brief Timer de la lectura de señal analógica.
 */
void FuncTimerAD(void *param) {
    vTaskNotifyGiveFromISR(ConversorAD_task_handle, pdFALSE);
}

/**
 * @brief Timer de la escritura del siguiente valor ECG.
 */
void FuncTimerDA(void *param) {
    vTaskNotifyGiveFromISR(ConversorDA_task_handle, pdFALSE);
}

/*==================[main application entry point]==========================*/

void app_main(void) {

    // Configuración del UART
    serial_config_t uart_config = {
        .port = UART_PC,
        .baud_rate = 115200,
        .func_p = NULL,
        .param_p = NULL
    };
    UartInit(&uart_config);

    // Inicialización del conversor analógico digital (CH1)
    analog_input_config_t adc_config = {
        .input = CH1,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL
    };
    AnalogInputInit(&adc_config);

    // Inicialización del conversor digital-analógico (DAC)
    AnalogOutputInit();

    // Configuración del timer para adquisición (lectura)
    timer_config_t timerAD = {
        .timer = TIMER_A,
        .period = FREC_MUESTREO_AD_US,
        .func_p = FuncTimerAD,
        .param_p = NULL
    };
    TimerInit(&timerAD);

    // Configuración del timer para reproducción (salida ECG)
    timer_config_t timerDA = {
        .timer = TIMER_B,
        .period = FREC_REPRO_DA_US,
        .func_p = FuncTimerDA,
        .param_p = NULL
    };
    TimerInit(&timerDA);

    // Creo las treas
    xTaskCreate(&ConversorAD_task, "Tarea_Leer_ADC", 2048, NULL, 5, &ConversorAD_task_handle);
    xTaskCreate(&ConversorDA_task, "Tarea_Escribir_DAC", 2048, NULL, 5, &ConversorDA_task_handle);

    // Inicio los timers
    TimerStart(timerAD.timer);
    TimerStart(timerDA.timer);
}


