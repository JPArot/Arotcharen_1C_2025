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
#include "uart_mcu.h"

#define REFRESCO_MED 1000000   
#define REFRESCO_LCD 100000    

/*==================[internal data definition]===============================*/
bool on = false;          // Variable para activar/desactivar medición y visualización
bool hold = false;        // Variable para mantener última medición (pausar actualización)
int distancia = 0;        // Variable global para almacenar distancia medida (en cm)

/* Handles para las tareas FreeRTOS */
TaskHandle_t Medir_task_handle = NULL;
TaskHandle_t Mostrar_task_handle = NULL;
TaskHandle_t Display_task_handle = NULL;
TaskHandle_t MostrarUART_task_handle = NULL;

/*==================[functions declaration]===============================*/

/**
 * @brief Tarea que muestra la distancia por pantalla  y controla LEDs según distancia.
 *        Se activa cuando se conecta 
 */
void MostrarDistancia_task(void *pvParameter) {
    while (1) {
        // Espera a recibir notificación para ejecutar la actualización
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (on) {
            if (!hold) {
                LcdItsE0803Write(distancia);  // Actualiza LCD con distancia
            }

            // Control de LEDs según distancia medida
            if (distancia < 10) {
                LedsOffAll();
            } else if (distancia < 20) {
                LedOn(LED_1); LedOff(LED_2); LedOff(LED_3);
            } else if (distancia < 30) {
                LedOn(LED_1); LedOn(LED_2); LedOff(LED_3);
            } else {
                LedOn(LED_1); LedOn(LED_2); LedOn(LED_3);
            }
        } else {
            // Si está apagado, apaga LCD y LEDs
            LcdItsE0803Off();
            LedsOffAll();
        }
    }
}

/**
 * @brief Tarea que mide la distancia usando sensor ultrasónico.
 *        Se activa con notificación periódica del timer.
 */
void Medir_dist(void *pvParameter) {
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (on) {
            distancia = HcSr04ReadDistanceInCentimeters();  // Lee distancia en cm
        }
    }
}

/**
 * @brief Tarea que muestra la distancia por UART serial.
 *        Muestra la distancia actualizada.
 */
void MostrarUART_task(void *pvParameter) {
    char buffer[16];
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (on && !hold) {
          
            UartSendString(UART_PC, "Aguante Boca!\r\n");

            // Formatea la distancia con 3 dígitos, espacio y "cm" + salto de línea
            snprintf(buffer, sizeof(buffer), "%03d cm\r\n", distancia);
            UartSendString(UART_PC, buffer);
        }
    }
}


/**
 * @brief Función llamada desde ISR de timer para notificar la tarea de medición.
 */
void FuncTimerMedir(void *param) {
    vTaskNotifyGiveFromISR(Medir_task_handle, pdFALSE);
}

/**
 * @brief Función llamada desde ISR de timer para notificar la tarea de mostrar LCD y LEDs.
 */
void FuncTimerMostrar(void *param) {
    vTaskNotifyGiveFromISR(Mostrar_task_handle, pdFALSE);
}

/**
 * @brief Función llamada desde ISR de timer para notificar la tarea de mostrar UART.
 */
void FuncTimerMostrarUART(void *param) {
    vTaskNotifyGiveFromISR(MostrarUART_task_handle, pdFALSE);
}



/**
 * @brief Alterna la bandera 'on' para activar o desactivar medición y visualización.
 */
void Tstart() {
    on = !on;
}

/**
 * @brief Alterna la bandera 'hold' para pausar o continuar actualización de pantalla y UART.
 *        Solo funciona si 'on' está activo.
 */
void THold() {
    if (on) {
        hold = !hold;
    }
}


/**
 * @brief Lee un byte del UART y alterna según comando recibido:
 *        - 'O': toggle on/off
 *        - 'H': toggle hold
 */
void TSerieOnOff() {
    uint8_t pres;
    if (UartReadByte(UART_PC, &pres)) {
        switch (pres) {
            case 'O':
                on = !on;
                break;
            case 'H':
                hold = !hold;
                break;
            default:
                
                break;
        }
    }
}

/*==================[Función principal]===============================*/

void app_main(void) {

    // Inicialización de periféricos y hardware
    LedsInit();
    HcSr04Init(GPIO_3, GPIO_2);
    SwitchesInit();
    LcdItsE0803Init();

    // Configuración y arranque de UART con función callback para comandos
    serial_config_t my_uart = {
        .port = UART_PC,
        .baud_rate = 9600,
        .func_p = &TSerieOnOff,
        .param_p = NULL
    };
    UartInit(&my_uart);

    // Configuración y arranque de timers para medición, visualización y UART
    timer_config_t timer_de_medicion = {
        .timer = TIMER_A,
        .period = REFRESCO_MED,
        .func_p = FuncTimerMedir,
        .param_p = NULL
    };
    TimerInit(&timer_de_medicion);

    timer_config_t timer_de_mostrar = {
        .timer = TIMER_B,
        .period = REFRESCO_LCD,
        .func_p = FuncTimerMostrar,
        .param_p = NULL
    };
    TimerInit(&timer_de_mostrar);

    timer_config_t timer_de_mostrar_uart = {
        .timer = TIMER_C,
        .period = REFRESCO_LCD,
        .func_p = FuncTimerMostrarUART,
        .param_p = NULL
    };
    TimerInit(&timer_de_mostrar_uart);

    // Configuración de interrupciones externas para switches físicos
    SwitchActivInt(SWITCH_1, &Tstart, NULL);
    SwitchActivInt(SWITCH_2, &THold, NULL);

    // Creación de tareas FreeRTOS para medir, mostrar LCD y mostrar UART
    xTaskCreate(&Medir_dist, "Medir", 512, NULL, 5, &Medir_task_handle);
    xTaskCreate(&MostrarDistancia_task, "Mostrar", 512, NULL, 5, &Mostrar_task_handle);
    xTaskCreate(&MostrarUART_task, "MostrarUART", 512, NULL, 5, &MostrarUART_task_handle);

    // Inicio de timers
    TimerStart(timer_de_medicion.timer);
    TimerStart(timer_de_mostrar.timer);
    TimerStart(timer_de_mostrar_uart.timer);
}

/*==================[end of file]============================================*/
