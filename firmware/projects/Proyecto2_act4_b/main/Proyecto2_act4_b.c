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
17,17,17,17,17,17,17,17,17,17,17,18,18,18,17,17,17,17,17,17,17,18,18,18,18,18,18,18,17,17,16,16,16,16,17,17,18,18,18,17,17,17,17,
18,18,19,21,22,24,25,26,27,28,29,31,32,33,34,34,35,37,38,37,34,29,24,19,15,14,15,16,17,17,17,16,15,14,13,13,13,13,13,13,13,12,12,
10,6,2,3,15,43,88,145,199,237,252,242,211,167,117,70,35,16,14,22,32,38,37,32,27,24,24,26,27,28,28,27,28,28,30,31,31,31,32,33,34,36,
38,39,40,41,42,43,45,47,49,51,53,55,57,60,62,65,68,71,75,79,83,87,92,97,101,106,111,116,121,125,129,133,136,138,139,140,140,139,137,
133,129,123,117,109,101,92,84,77,70,64,58,52,47,42,39,36,34,31,30,28,27,26,25,25,25,25,25,25,25,25,24,24,24,24,25,25,25,25,25,25,25,
24,24,24,24,24,24,24,24,23,23,22,22,21,21,21,20,20,20,20,20,19,19,18,18,18,19,19,19,19,18,17,17,18,18,18,18,18,18,18,18,17,17,17,17,
17,17,17

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


