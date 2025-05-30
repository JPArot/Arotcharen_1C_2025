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

/**
 * @file main.c
 * @brief Firmware para longboard eléctrico con ESP32-C6 y BLE
 * 
 * Características:
 * - Comunicación BLE modular
 * - Control de motor con PWM
 * - Sensor óptico para medir velocidad
 * - ADC para medir voltaje de batería
 * - Doble toque para mantener velocidad constante
 * - Presión mantenida para acelerar progresivamente
 * - Frenado automático si la batería cae por debajo del 10%
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "ble_mcu.h"  // BLE modular

#include <string.h>
#include <stdio.h>

// === Configuración de hardware para ESP32-C6 ===
#define PWM_GPIO        7    // PWM del motor
#define SENSOR_GPIO     6    // Sensor óptico
#define BAT_ADC_CH      ADC1_CHANNEL_0  // ADC en GPIO0

#define PWM_CHANNEL     LEDC_CHANNEL_0
#define PWM_TIMER       LEDC_TIMER_0
#define PWM_FREQ_HZ     1000
#define PWM_RES         LEDC_TIMER_8_BIT

#define SENSOR_SLOTS    20     // Cantidad de ranuras por vuelta
#define WHEEL_RADIUS    0.03f  // Radio de rueda en metros
#define BAT_DIV_FACTOR  2.0f   // Factor por divisor resistivo
#define BAT_LOW_PERCENT 10.0f  // Umbral de batería crítica (%)

// === Variables globales ===
volatile int64_t last_pulse_time = 0;
volatile int64_t pulse_interval_us = 1000000;
static int duty_cycle = 0;
static bool bateria_baja = false;

// === Flags de control por BLE ===
static int64_t last_touch_time = 0;
static bool mantener_velocidad = false;
static bool mantener_presionado = false;
volatile bool cmd_frenar = false;

// === Callback de recepción de comandos BLE ===
void read_data(uint8_t *data, uint8_t length) {
    int64_t now = esp_timer_get_time();  // tiempo actual en µs

    if (data[0] == 'A') {  // Comando de avance
        if (now - last_touch_time < 500000) {  // doble toque (<500 ms)
            mantener_velocidad = true;
        } else {
            mantener_presionado = true;
        }
        last_touch_time = now;
    }
    else if (data[0] == 'R') {  // Soltar botón
        mantener_presionado = false;
    }
    else if (data[0] == 'F') {  // Frenar
        cmd_frenar = true;
    }
}

// === Interrupción del sensor óptico ===
void IRAM_ATTR sensor_isr(void *arg) {
    int64_t now = esp_timer_get_time();
    pulse_interval_us = now - last_pulse_time;
    last_pulse_time = now;
}

// === Inicialización del PWM para el motor ===
void init_motor_pwm() {
    ledc_timer_config_t timer = {
        .duty_resolution = PWM_RES,
        .freq_hz = PWM_FREQ_HZ,
        .speed_mode = LEDC_LOW_SPEED_MODE,  
        .timer_num = PWM_TIMER
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .channel = PWM_CHANNEL,
        .duty = 0,
        .gpio_num = PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,  
        .hpoint = 0,
        .timer_sel = PWM_TIMER
    };
    ledc_channel_config(&channel);
}


// === Inicialización del sensor óptico ===
void init_sensor() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << SENSOR_GPIO),
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(SENSOR_GPIO, sensor_isr, NULL);
}

// === Inicialización del ADC para batería ===
void init_adc() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BAT_ADC_CH, ADC_ATTEN_DB_11);
}

// === Función para leer voltaje de batería ===
float leerVoltajeBateria() {
    int val = adc1_get_raw(BAT_ADC_CH);
    return (val / 4095.0f) * 3.3f * BAT_DIV_FACTOR;
}

// === Calcula velocidad en m/s usando los pulsos del sensor óptico ===
float calcularVelocidad() {
    if (pulse_interval_us <= 0) return 0;
    float revs_per_sec = 1000000.0f / (pulse_interval_us * SENSOR_SLOTS);
    return 2 * 3.1416f * WHEEL_RADIUS * revs_per_sec;
}

// === Frena progresivamente ===
void frenar() {
    // Validar rango del duty actual (por si hay errores previos)
    if (duty_cycle > 255) duty_cycle = 255;
    if (duty_cycle < 0) duty_cycle = 0;

    for (int i = duty_cycle; i >= 0; i -= 10) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, i);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    duty_cycle = 0;
    mantener_velocidad = false;
    mantener_presionado = false;
}

// === Tarea: interpreta comandos y ejecuta control de motor ===
void tarea_comandos(void *param) {
    while (1) {
        if (cmd_frenar) {
            frenar();
            cmd_frenar = false;
        }

        if (mantener_presionado && !bateria_baja) {
            if (duty_cycle < 255) {
                duty_cycle += 10;
                if (duty_cycle > 255) duty_cycle = 255;
                ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, duty_cycle);
                ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);
            }
        }

        if (!mantener_presionado && !mantener_velocidad && !bateria_baja) {
            // Mantener duty constante si no hay comando activo
            ledc_set_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL, duty_cycle);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, PWM_CHANNEL);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


// === Tarea: monitorea batería y envía datos BLE ===
void tarea_monitoreo(void *param) {
    while (1) {
        float volt = leerVoltajeBateria();
        float bat_pct = volt * 100 / 4.2f;

        // Protección por batería baja
        if (bat_pct < BAT_LOW_PERCENT && !bateria_baja) {
            frenar();
            bateria_baja = true;
        } else if (bat_pct >= BAT_LOW_PERCENT) {
            bateria_baja = false;
        }

        float vel = calcularVelocidad();
        char msg[32];
        snprintf(msg, sizeof(msg), "VEL:%.2f;BAT:%.0f", vel, bat_pct);
        BleSendString(msg);  // Notificación BLE

        vTaskDelay(pdMS_TO_TICKS(1000));  // cada 1 segundo
    }
}


void app_main(void) {
    nvs_flash_init();
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    init_motor_pwm();
    init_sensor();
    init_adc();

    // Configuración BLE con nombre y callback
    ble_config_t cfg = {
        .device_name = "ESP_LONGBRD",
        //.on_rx = read_data
    };
    BleInit(&cfg);
    nimble_port_init();

    // Tareas
    xTaskCreate(tarea_comandos, "Comandos", 2048, NULL, 2, NULL);
    xTaskCreate(tarea_monitoreo, "Monitoreo", 2048, NULL, 1, NULL);
}


