/*! @mainpage Longboard Eléctrico con ESP32-C6 + ESP-EDU
 *
 * @section genDesc Descripción General
 *
 * Este firmware permite controlar un longboard eléctrico utilizando un ESP32-C6.
 * Las funcionalidades implementadas incluyen:
 *
 * - Control de motor por PWM (mediante L293D)
 * - Lectura de velocidad usando sensor óptico (TCRT5000)
 * - Medición del voltaje de batería mediante ADC
 * - Comunicación BLE para recepción de comandos y envío de estado
 * - Control por comandos: avanzar, frenar, mantener velocidad
 * - Indicadores LED para depuración visual
 *
 * <b>Comandos disponibles vía BLE:</b>
 * - `'A'` → avanzar/mantener velocidad
 * - `'F'` → frenar
 *
 * @section hardConn Conexión de Hardware
 *
 * |    Componente         |   ESP32-C6 (ESP-EDU) GPIO |
 * |:---------------------:|:-------------------------:|
 * | Sensor óptico (TCRT5000) salida digital | GPIO_6 (SENSOR_GPIO)     |
 * | Motor (a través de L293D)               | GPIO conectados vía driver L293 |
 * | Lectura de batería (ADC)                | CH0 (canal ADC 0)        |
 * | LED 1 (actividad BLE)                   | LED_1                    |
 * | LED 2 (comando 'A')                     | LED_2                    |
 * | LED 3 (mantener velocidad)              | LED_3                    |
 *
 * @section changelog Historial de cambios
 *
 * |   Fecha     | Descripción                                     |
 * |:-----------:|:-----------------------------------------------|
 * | 12/09/2023  | Creación del documento base                    |
 * | 17/06/2025  | Integración BLE, sensor óptico y control motor |
 *
 * @author Jean Pierre Arotcharen (jean.arotcharen@ingenieria.uner.edu.ar)
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_bt.h"

#include "ble_mcu.h"
#include "pwm_mcu.h"
#include "gpio_mcu.h"
#include "analog_io_mcu.h"
#include "timer_mcu.h"
#include "l293.h" 

// === Definiciones generales ===
#define SENSOR_GPIO GPIO_6        /*!< GPIO del sensor óptico */
#define BAT_ADC_CH CH0            /*!< Canal ADC para batería */
#define SENSOR_SLOTS 20           /*!< Cantidad de ranuras en el disco */
#define WHEEL_RADIUS 0.03f        /*!< Radio de la rueda en metros */
#define BAT_DIV_FACTOR 2.0f       /*!< Factor de división resistiva */
#define BAT_LOW_PERCENT 10.0f     /*!< Umbral de batería baja (en %) */


// === Variables de estado ===
static int duty_cycle = 0;
static bool mantener_velocidad = false;
static bool mantener_presionado = false;
static bool bateria_baja = false;
static bool cmd_frenar = false;

// === Variables de velocidad ===
static uint32_t pulse_interval_us = 0;
static uint32_t last_pulse_time = 0;
static int64_t last_touch_time = 0;

#include "led.h" //Para control

/**
 * @brief Callback llamado al recibir un comando BLE.
 * 
 * Interpreta los comandos:
 * - 'A': avanzar o mantener velocidad
 * - 'F': frenar progresivamente
 */
void read_data(uint8_t *data, uint8_t length)
{
    //Prendo los LED para indicar actividad
      LedOn(LED_1);
    //  Protección para punteros nulos o mensajes vacíos
    if (data == NULL || length == 0)
    return;
               
   
    
    switch (data[0]) {
        case 'A':
        //Prendo Led para ver que funciona
        LedOn(LED_2);
            mantener_velocidad = true;
            break;
        case 'F':
            cmd_frenar = true;
            break;
        default:
            
            break;
    }
}


/**
 * @brief ISR del sensor óptico.
 * 
 * Se activa en flanco descendente y mide el tiempo entre ranuras.
 */
void IRAM_ATTR sensor_isr(void *arg) {
    uint32_t now = TimerRead(TIMER_A);
    if (now > last_pulse_time) {
        pulse_interval_us = now - last_pulse_time;
        last_pulse_time = now;
    }
}


/**
 * @brief Inicializa el pin del sensor óptico y su interrupción.
 */
void init_sensor(void) {
    GPIOInit(SENSOR_GPIO, GPIO_INPUT);
    GPIOActivInt(SENSOR_GPIO, sensor_isr, false, NULL);
    GPIOInputFilter(SENSOR_GPIO);
}

/**
 * @brief Inicializa el ADC para la lectura de batería.
 */
void init_adc(void) {
    static analog_input_config_t battery_adc_config = {
        .input = BAT_ADC_CH,
        .mode = ADC_SINGLE,
        .func_p = NULL,
        .param_p = NULL,
        .sample_frec = 0
    };
    AnalogInputInit(&battery_adc_config);
}

/**
 * @brief Lee el voltaje de la batería en voltios.
 * @return Voltaje estimado de la batería (V)
 */
float leerVoltajeBateria(void) {
    uint16_t val_mv = 0;
    AnalogInputReadSingle(BAT_ADC_CH, &val_mv);
    return (val_mv / 1000.0f) * BAT_DIV_FACTOR;
}

/**
 * @brief Calcula la velocidad del longboard en m/s.
 * @return Velocidad lineal en m/s
 */
float calcularVelocidad() {
    if (pulse_interval_us <= 0)
        return 0;
    float rps = 1000000.0f / (pulse_interval_us * SENSOR_SLOTS);
    return 2 * M_PI * WHEEL_RADIUS * rps;
}

/**
 * @brief Frenado suave y progresivo del motor.
 * 
 * Reduce la velocidad de forma escalonada y resetea los flags de control.
 */
void frenar(void) {
    for (int i = duty_cycle; i >= 0; i -= 10) {
        L293SetSpeed(MOTOR_2, i);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    duty_cycle = 0;
   
    mantener_velocidad = false;
    L293SetSpeed(MOTOR_2, 0);
}

/**
 * @brief Tarea que ejecuta los comandos BLE sobre el motor.
 */
void tarea_comandos(void *param) {
    while (1) {
        if (cmd_frenar) {
            frenar();
            cmd_frenar = false;
        }

        if (mantener_velocidad) {
            LedOn(LED_3); //Prendo LED rojo para indicar que se mantiene velocidad
            mantener_velocidad=false;
            if (duty_cycle < 255) {
                duty_cycle += 10;
                if (duty_cycle > 255) duty_cycle = 255;
                L293SetSpeed(MOTOR_2, duty_cycle*100/255 );
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea que monitorea batería, velocidad y BLE.
 */
void tarea_monitoreo(void *param) {
    float velocidad = 0.0f;
    char msg[64];

    while (1) {
        if (BleStatus() != BLE_CONNECTED) {
            //frenar();
            bateria_baja = true;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        float voltaje_bat = leerVoltajeBateria();
        if (voltaje_bat < 6.6f) {
            bateria_baja = true;
            //frenar(); 
        } else {
            bateria_baja = false;
        }

        velocidad = calcularVelocidad();
        snprintf(msg, sizeof(msg), "Bat: %.2f V | Vel: %.2f km/h", voltaje_bat, velocidad);
        BleSendString(msg);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
void funcionTimer(){

}

/**
 * @brief Función principal del firmware.
 * 
 * Inicializa sensores, BLE, L293 y lanza tareas.
 */
void app_main(void) {
    timer_config_t timer_config = {
        .timer = TIMER_A,
        .period = 1000000,
        .func_p = funcionTimer,
        .param_p = NULL
    };
    TimerInit(&timer_config);
    TimerStart(TIMER_A);
    LedsInit();
    L293Init();       
    init_sensor();
    init_adc();

    ble_config_t cfg = {
        "ESP_LONGBRD",
        read_data
    };
    BleInit(&cfg);

    xTaskCreate(tarea_comandos, "Comandos", 2048, NULL, 2, NULL);
    xTaskCreate(tarea_monitoreo, "Monitoreo", 2048, NULL, 1, NULL);
}

