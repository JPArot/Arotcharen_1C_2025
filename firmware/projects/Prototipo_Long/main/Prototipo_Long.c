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
 * @author Albano Peñalva (albano.penalva@uner.edu.ar)
 *
 */

/**
 * @file firmware_longboard_bt.c
 * @brief Firmware para control de un prototipo de longboard eléctrico vía Bluetooth (ESP32-C6)
 *
 * Funcionalidades:
 * - Comunicación BLE con app móvil (comandos de avance y frenado)
 * - Control de velocidad mediante PWM
 * - Lectura de velocidad desde sensor óptico con interrupciones
 * - Lectura de nivel de batería por ADC
 * - Envío periódico de velocidad y nivel de batería vía BLE
 * - Soporte para "doble toque" para mantener velocidad constante
 * - Soporte para mantener presionado y acelerar progresivamente
 * - Frenado automático cuando la batería baja del 10%
 */

#include <stdio.h>
#include <string.h>
#include "driver/ledc.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

#define TAG "LONGBOARD_BT"

// Parámetros del PWM para controlar la velocidad del motor
#define PWM_CHANNEL LEDC_CHANNEL_0
#define PWM_TIMER LEDC_TIMER_0
#define PWM_GPIO 25
#define PWM_FREQ_HZ 1000
#define PWM_RES LEDC_TIMER_8_BIT

// Parámetros para la lectura de batería por ADC
#define BAT_ADC_CH ADC1_CHANNEL_6 // GPIO34
#define BAT_DIV_FACTOR 2.0
#define BAT_LOW_PERCENT 10.0f // Umbral de batería en porcentaje (10%)

// Parámetros del sensor óptico para medir velocidad
#define SENSOR_GPIO 33
#define SENSOR_SLOTS 20
#define WHEEL_RADIUS 0.03f // en metros

static uint16_t conn_handle;
static uint16_t notify_handle;
static int duty_cycle = 0; // Valor PWM actual aplicado al motor

// Variables para calcular la velocidad a partir de los pulsos del sensor
volatile int64_t last_pulse_time = 0;
volatile int64_t pulse_interval_us = 1000000;

// Variables para lógica de doble toque y mantener velocidad
static int64_t last_touch_time = 0;
static bool mantener_velocidad = false;
static bool mantener_presionado = false;
static bool bateria_baja = false;

/**
 * @brief ISR del sensor óptico
 * Esta rutina se llama automáticamente cuando el sensor óptico detecta
 * un flanco descendente (paso de una ranura). Calcula el tiempo entre pulsos.
 */
static void IRAM_ATTR sensor_isr_handler(void *arg)
{
	int64_t now = esp_timer_get_time();
	pulse_interval_us = now - last_pulse_time;
	last_pulse_time = now;
}

/**
 * @brief Inicializa el pin GPIO del sensor óptico y lo configura para generar interrupciones en flanco de bajada.
 */
void sensor_init()
{
	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_NEGEDGE,
		.mode = GPIO_MODE_INPUT,
		.pin_bit_mask = (1ULL << SENSOR_GPIO),
		.pull_up_en = GPIO_PULLUP_ENABLE};
	gpio_config(&io_conf);
	gpio_install_isr_service(0);
	gpio_isr_handler_add(SENSOR_GPIO, sensor_isr_handler, NULL);
}

/**
 * @brief Inicializa el PWM para controlar el motor
 */
void motor_pwm_init()
{
	ledc_timer_config_t timer = {
		.duty_resolution = PWM_RES,
		.freq_hz = PWM_FREQ_HZ,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.timer_num = PWM_TIMER};
	ledc_timer_config(&timer);

	ledc_channel_config_t channel = {
		.channel = PWM_CHANNEL,
		.duty = 0,
		.gpio_num = PWM_GPIO,
		.speed_mode = LEDC_HIGH_SPEED_MODE,
		.hpoint = 0,
		.timer_sel = PWM_TIMER};
	ledc_channel_config(&channel);
}

/**
 * @brief Acelera progresivamente hasta alcanzar un valor PWM estable
 */
void acelerar()
{
	for (int i = duty_cycle; i <= 200; i += 10)
	{
		ledc_set_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL, i);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL);
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	duty_cycle = 200;
}

/**
 * @brief Frena progresivamente hasta detener el motor
 */
void frenarProgresivo()
{
	for (int i = duty_cycle; i >= 0; i -= 10)
	{
		ledc_set_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL, i);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL);
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	duty_cycle = 0;
	mantener_velocidad = false;
	mantener_presionado = false;
}

/**
 * @brief Aumenta gradualmente la velocidad mientras el botón esté presionado
 */
void acelerarSostenido()
{
	if (duty_cycle < 255)
	{
		duty_cycle += 10;
		if (duty_cycle > 255)
			duty_cycle = 255;
		ledc_set_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL, duty_cycle);
		ledc_update_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL);
	}
}

/**
 * @brief Lee el voltaje real de la batería desde el ADC
 */
float leerVoltajeBateria()
{
	int val = adc1_get_raw(BAT_ADC_CH);
	return (val / 4095.0f) * 3.3f * BAT_DIV_FACTOR;
}

/**
 * @brief Calcula la velocidad lineal del longboard en m/s
 */
float calcularVelocidad()
{
	if (pulse_interval_us <= 0)
		return 0;
	float revs_per_sec = 1000000.0f / (pulse_interval_us * SENSOR_SLOTS);
	float v = 2 * 3.1416f * WHEEL_RADIUS * revs_per_sec;
	return v;
}

/**
 * @brief Envía por BLE la velocidad y el estado de la batería en formato de texto
 */
void enviarDatos()
{
	char msg[32];
	snprintf(msg, sizeof(msg), "VEL:%.2f;BAT:%.0f", calcularVelocidad(), leerVoltajeBateria() * 100 / 4.2);
	struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
	ble_gattc_notify_custom(conn_handle, notify_handle, om);
}

/**
 * @brief Maneja los comandos recibidos desde la app por BLE
 */
static int ble_cmd_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	char comando = ctxt->om->om_data[0];
	int64_t now = esp_timer_get_time();

	if (comando == 'A')
	{
		if (now - last_touch_time < 500000)
		{
			mantener_velocidad = true;
		}
		else
		{
			mantener_presionado = true;
		}
		last_touch_time = now;
	}
	else if (comando == 'R')
	{
		mantener_presionado = false;
	}
	else if (comando == 'F')
	{
		frenarProgresivo();
	}
	return 0;
}

/**
 * @brief Servicio BLE para recibir comandos y enviar notificaciones
 */
static const struct ble_gatt_svc_def gatt_svcs[] = {
	{.type = BLE_GATT_SVC_TYPE_PRIMARY,
	 .uuid = BLE_UUID16_DECLARE(0x180F),
	 .characteristics = (struct ble_gatt_chr_def[]){
		 {.uuid = BLE_UUID16_DECLARE(0x2A19),
		  .access_cb = ble_cmd_access,
		  .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
		  .val_handle = &notify_handle},
		 {0}}},
	{0}};

/**
 * @brief Función principal: inicializa todos los módulos y arranca la tarea principal BLE
 */
void app_main(void)
{
	// Inicializa el sistema de almacenamiento no volátil

	nvs_flash_init(); // Requerido por el stack BLE y otras configuraciones
	// Libera memoria del modo Bluetooth clásico ya que se usará solo BLE
	esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
	motor_pwm_init();										// Inicializa PWM del motor
	sensor_init();											// Configura el sensor óptico e ISR
	adc1_config_width(ADC_WIDTH_BIT_12);					// Resolución ADC de 12 bits
	adc1_config_channel_atten(BAT_ADC_CH, ADC_ATTEN_DB_11); // Atenuación para leer hasta ~3.6V

	// Configuración del BLE al sincronizar: nombre del dispositivo y servicios GATT
	ble_hs_cfg.sync_cb = []()
	{
		ble_svc_gap_device_name_set("ESP_LONGBRD");
		ble_svc_gatt_init();
		ble_gatts_count_cfg(gatt_svcs);
		ble_gatts_add_svcs(gatt_svcs);
	};

	nimble_port_init(); // Inicializa el stack BLE
	// Tarea principal BLE que se ejecuta cada 1 segundo
	nimble_port_freertos_init([](void *param){
        while (true) {
            float voltaje = leerVoltajeBateria();
            float porcentaje = voltaje * 100 / 4.2f;

            // Si la batería cae por debajo del 10%, se frena automáticamente
            if (porcentaje < BAT_LOW_PERCENT && !bateria_baja) {
                frenarProgresivo();
                bateria_baja = true;
            } else if (porcentaje >= BAT_LOW_PERCENT && bateria_baja){
                bateria_baja = false;
            }
            // Aceleración progresiva mientras el botón esté presionado
            if (mantener_presionado && !bateria_baja)
			    acelerarSostenido();

            // Si no está en modo mantener ni presionado, actualiza PWM
            if (!mantener_velocidad && !mantener_presionado && !bateria_baja) {
                ledc_set_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL, duty_cycle);
                ledc_update_duty(LEDC_HIGH_SPEED_MODE, PWM_CHANNEL);
            }

            // Envía velocidad y batería por BLE
            enviarDatos();
            vTaskDelay(pdMS_TO_TICKS(1000));
        } });
}
