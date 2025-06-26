/*! @mainpage Alimentador automático de mascotas
 *
 * @section 
 *
 * Este firmware permite controlar un dispositivo para suministrar alimento y agua a una mascota.
 * Se utiliza: 
 * - sensor HC SR-04 
 * - una balanza analógica 
 * Tambien el ssistema debe ser capaz de enviar información a una PC a través de UART
 * cuanto alimento y agua tiene disponible la mascota 
 * en los respectivos platos. Para esto se deben transmitir por el puerto serie a la PC mensajes cada 10 segundos,
 * con un formato predefinido.
 * Y por ultimo se debe utilizar las teclas 1  para iniciar y detener el suministro de agua
 * y la tecla 2 para hacer lo propio con el suministro de alimento
 *
  * @section  Conexión de Hardware prototipo de longboard 
 *
 * |    Componente         |   ESP32-C6 (ESP-EDU) GPIO |
 * |:---------------------:|:-------------------------:|
 * | Electrovalvula (agua)        | GPIO_3                             |
 * | Motor comida (L293D)         | GPIO_5                             |
 * | Hc sr-04 TRIG                | GPIO_7                             |
 * | Hc sr-04 ECHO                | GPIO_8                             |
 * | Tecla agua                   | GPIO_9                             |
 * | Tecla comida                 | GPIO_10                            |
 * | Balanza                      | CH2                                |
 * | UART consola                 | UART_PC                            |
 * | LED 3 (mantener velocidad)   | LED_3                              | 
 *
 * @author Arotcharen Jean Pierre (Jean.Arotcharen@ingenieria.uner.edu.ar)
 *
 */



#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_mcu.h"
#include "uart_mcu.h"
#include "delay_mcu.h"
#include "analog_io_mcu.h"
#include "hc_sr04.h"
#define ELECTROVALVULA GPIO_3
#define MOTOR_COMIDA   GPIO_5
#define TRIG_GPIO      GPIO_7
#define ECHO_GPIO      GPIO_8
#define TECLA_AGUA     GPIO_9
#define TECLA_COMIDA   GPIO_10
#define UART_CONSOLA   UART_PC

//Hago unas banderas de control para saber si el agua y la comida estan habilitadas
static bool agua_habilitada = false;
static bool comida_habilitada = false;

float agua_ml = 0;
float comida_g = 0;

/**
 * @brief Callback llamado al recibir un comando BLE.
 * 
 * Interpreta los comandos:
 * - 'A': avanzar o mantener velocidad
 * - 'F': frenar progresivamente
 *  Se enciende un LED para para control e indicar actividad.
 */
void InitHardware(void) {
    GPIOInit(ELECTROVALVULA, GPIO_OUTPUT);
    GPIOInit(MOTOR_COMIDA, GPIO_OUTPUT);
    GPIOInit(TECLA_AGUA, GPIO_INPUT);
    GPIOInit(TECLA_COMIDA, GPIO_INPUT);
    HcSr04Init(TRIG_GPIO, ECHO_GPIO);

    analog_input_config_t adc_cfg = {
        .input = CH2,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL,
        .sample_frec = 0
    };
    AnalogInputInit(&adc_cfg);

    serial_config_t uart_cfg = {
        .port = UART_CONSOLA,
        .baud_rate = 115200,
        .func_p = NULL,
        .param_p = NULL
    };
    UartInit(&uart_cfg);
}

/**
 * @brief tarea que monitorea las teclas de agua y comida.
 * Detecta el estado de las teclas y activa/desactiva el suministro de agua y comida.
 * y envía mensajes por UART al usuario.
 * utilizo las banderas de control agua_habilitada y comida_habilitada
 */
void TareaTeclas(void *param) {
    bool estado_agua = true;
    bool estado_comida = true;

//controlo el agua
    while (1) {
        if (!GPIORead(TECLA_AGUA) && estado_agua) {
            agua_habilitada = !agua_habilitada;
            UartSendString(UART_CONSOLA, agua_habilitada ? "[AGUA ACTIVADA]\r\n" : "[AGUA DESACTIVADA]\r\n");
            estado_agua = false; // cambio el estado de la bandera
        }
        if (GPIORead(TECLA_AGUA)) {
            estado_agua = true;
        }
//uso la misma logica para la comida
        if (!GPIORead(TECLA_COMIDA) && estado_comida) {
            comida_habilitada = !comida_habilitada;
            UartSendString(UART_CONSOLA, comida_habilitada ? "[COMIDA ACTIVADA]\r\n" : "[COMIDA DESACTIVADA]\r\n");
            estado_comida = false;
        }
        if (GPIORead(TECLA_COMIDA)) {
            estado_comida = true;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea que controla el suministro de agua.
 * Utiliza el sensor HC-SR04 para medir la distancia y calcular el volumen de agua
 * Si la distancia es menor a 30 cm activa la electrovalvula para suministrar agua.
 * Si la distancia es mayor o igual a 25 cm desactiva la electrovalvula
 */
void TareaAgua(void *param) {
    float distancia_cm;
    while (1) {
        if (agua_habilitada) {
            if (HcSr04ReadDistance(&distancia_cm)) {
                agua_ml = (30.0 - distancia_cm) * 100; 
                if (agua_ml < 500) {
                    GPIOOn(ELECTROVALVULA);
                } else if (agua_ml >= 2500) {
                    GPIOOff(ELECTROVALVULA);
                }
            }
        } else {
            GPIOOff(ELECTROVALVULA);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
/**
 * @brief Tarea que controla el suministro de coimida.
 * Usa un ADC para leer el valor de la balanza analógica
 * Calcula el peso de la comida en gramos y activa/desactiva el motor de comida
 * Si el peso es menor a 25g activa el motor de comida
 * Si el peso es mayor o igual a 250g desactiva el motor de comida
 */

void TareaComida(void *param) {
    uint16_t raw;
    while (1) {
        if (comida_habilitada) {
            AnalogInputReadSingle(CH2, &raw);
            float volt = (raw / 4095.0f) * 3.3f;
            comida_g = ((volt - 0.5f) / 2.8f) * 500.0f; // (3.3-0.5)=2.8 (que seria la variacion de voltaje maximo y minimo)
      //si la comida es menor a 25g, activo el motor de comida
            if (comida_g < 25) {
                GPIOOn(MOTOR_COMIDA);
		//si la comida es mayor a 250g, desactivo el motor de comida
            } else if (comida_g >= 250) {
                GPIOOff(MOTOR_COMIDA);
            }
        } else {
            GPIOOff(MOTOR_COMIDA);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/**
 * @brief Tarea que maneja los comandos recibidos por UART.
 * Permite activar/desactivar el suministro de agua y comida mediante los comandos w  y f 
 *  
 */
void TareaUART(void *param) {
    char comando;
    while (1) {
        if (UartReadByte(UART_CONSOLA, (uint8_t *)&comando)) {
            if (comando == 'w') {
                agua_habilitada = !agua_habilitada;
                UartSendString(UART_CONSOLA, agua_habilitada ? "[AGUA ACTIVADA]\r\n" : "[AGUA DESACTIVADA]\r\n");
            }
            if (comando == 'f') {
                comida_habilitada = !comida_habilitada;
                UartSendString(UART_CONSOLA, comida_habilitada ? "[COMIDA ACTIVADA]\r\n" : "[COMIDA DESACTIVADA]\r\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea que envía informes periódicos por UART.
 * Envía cada 10 segundos un mensaje con la cantidad de comida y agua disponible.
 */
void TareaInforme(void *param) {
    while (1) {
        char mensaje[100];
        snprintf(mensaje, sizeof(mensaje), "Teo tiene %.0f g de alimento y %.0f ml de agua\r\n", comida_g, agua_ml);
        UartSendString(UART_CONSOLA, mensaje);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void) {
    InitHardware();
    UartSendString(UART_CONSOLA, "\r\n[SISTEMA INICIADO]\r\nComandos UART: 'w'=agua, 'f'=comida\r\n");

    xTaskCreate(&TareaTeclas, "Teclas", 4096, NULL, 5, NULL);
    xTaskCreate(&TareaAgua, "Agua", 4096, NULL, 5, NULL);
    xTaskCreate(&TareaComida, "Comida", 4096, NULL, 5, NULL);
    xTaskCreate(&TareaUART, "UART", 4096, NULL, 2, NULL);
    xTaskCreate(&TareaInforme, "Informe", 4096, NULL, 1, NULL);
}








