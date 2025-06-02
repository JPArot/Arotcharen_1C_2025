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
 * @brief Firmware para longboard eléctrico (ESP32-C6 + ESP-EDU)
 *
 * Funcionalidades:
 * - Control de motor por PWM con driver PWM_MCU
 * - Lectura de sensor óptico para medir velocidad
 * - Lectura de batería por ADC
 * - Comunicación BLE usando BleInit y read_data
 * - Modo mantener velocidad con doble toque
 * - Aceleración progresiva con presión sostenida
 * - Frenado automático si batería < 10%
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "ble_mcu.h"
#include "pwm_mcu.h"
#include "gpio_mcu.h"

#define MOTOR_GPIO      GPIO_7        // GPIO para PWM del motor
#define SENSOR_GPIO     GPIO_6        // Sensor óptico
#define BAT_ADC_CH      ADC1_CHANNEL_0 // GPIO0 en ESP-EDU
#define PWM_OUT         PWM_0         // Salida PWM usada
#define PWM_FREQ_HZ     1000          // Frecuencia del PWM

#define SENSOR_SLOTS    20            // Ranuras por vuelta
#define WHEEL_RADIUS    0.03f         // Radio de rueda en metros
#define BAT_DIV_FACTOR  2.0f          // Divisor resistivo
#define BAT_LOW_PERCENT 10.0f         // Umbral de batería baja

// === Variables de estado ===
static int duty_cycle = 0;
static bool mantener_velocidad = false;
static bool mantener_presionado = false;
static bool bateria_baja = false;
static bool cmd_frenar = false;

volatile int64_t last_pulse_time = 0;
volatile int64_t pulse_interval_us = 1000000;
static int64_t last_touch_time = 0;

/**
 * @brief Callback de recepción de comandos vía BLE
 *
 * Esta función se llama automáticamente cada vez que se recibe un mensaje desde la app móvil
 * a través del canal Bluetooth Low Energy (BLE).
 *
 * Interpreta el primer byte del mensaje (`data[0]`) como un comando de control, que puede ser:
 *
 * - 'A' (Avanzar): 
 *     - Si es un segundo toque dentro de 500 milisegundos -> activa mantener velocidad constante
 *     - Si no, interpreta que el botón está siendo presionado -> activa aceleración progresiva
 *
 * - 'R' (Release): 
 *     - El usuario soltó el botón de avanzar -> se desactiva la aceleración 
 *
 * - 'F' (Frenar): 
 *     - Se activa la bandera `cmd_frenar` para ejecutar el frenado progresivo en la tarea principal
 *
 * @param data   Puntero a los datos recibidos vía BLE en formato ASCII
 * @param length Longitud del array recibido (no usado en este caso)
 */
void read_data(uint8_t *data, uint8_t length) {
    int64_t now = esp_timer_get_time();  // Momento actual en microsegundos

    if (data[0] == 'A') {  // Comando "Avanzar"
        if (now - last_touch_time < 500000) {  // Si es un doble toque (<500 ms)
            mantener_velocidad = true;         // Activar modo de velocidad constante
        } else {
            mantener_presionado = true;        // Interpretar como botón sostenido (aceleración)
        }
        last_touch_time = now;  // Guardar tiempo del toque actual
    } else if (data[0] == 'R') {  // Comando "Release" → soltar botón
        mantener_presionado = false;
    } else if (data[0] == 'F') {  // Comando "Frenar"
        cmd_frenar = true;       // Se activará el frenado en la tarea de comandos
    }
}

/**
 * @brief ISR del sensor óptico
 *
 * Esta rutina de interrupción se llama automáticamente cuando el sensor óptico
 * detecta el paso de una ranura (flanco de bajada en el pin configurado).
 * Su propósito es medir el intervalo de tiempo entre dos eventos consecutivos,
 * que corresponde al tiempo entre dos ranuras pasando por el sensor.
 * Este valor luego se usa para calcular la velocidad lineal del longboard.
 */
void IRAM_ATTR sensor_isr(void *arg) {
    int64_t now = esp_timer_get_time();            // Obtiene el tiempo actual en microsegundos
    pulse_interval_us = now - last_pulse_time;     // Calcula el tiempo entre pulsos
    last_pulse_time = now;                         // Actualiza el tiempo del último pulso
}


/**
 * @brief Inicializa el PWM del motor
 *
 * Esta función configura la salida PWM para controlar la velocidad del motor
 * Uso las funciones del driver ESP-EDU
 * Acciones realizadas:
 * - Inicializa el PWM en el pin definido por `MOTOR_GPIO` y la frecuencia `PWM_FREQ_HZ`
 * - Configura el duty cycle inicial en 0% (motor detenido)
 */
void init_pwm() {
    PWMInit(PWM_OUT, MOTOR_GPIO, PWM_FREQ_HZ);  // Configura PWM en el pin del motor
    PWMSetDutyCycle(PWM_OUT, 0);                // Comienza con duty del 0% (motor apagado)
}


/**
 * @brief Inicializa el pin del sensor óptico y su interrupción
 *
 * Esta función configura el pin conectado al sensor óptico como entrada
 * digital con interrupción por flanco descendente (cuando detecta una ranura).
 *
 * Acciones realizadas:
 * - Configura el pin `SENSOR_GPIO` como entrada con resistencia pull-up.
 * - Habilita interrupción por flanco negativo (caída de señal).
 * - Instala el servicio de ISR (interrupciones) y asocia la función `sensor_isr()`
 *   como rutina que se ejecutará cada vez que el sensor detecte una ranura.
 *
 * Notas:
 * - El sensor óptico genera una señal baja al pasar una ranura por delante,
 *   lo que se detecta mediante la interrupción por flanco negativo.
 * - Esta interrupción se usa para calcular el tiempo entre pulsos y así medir velocidad.
 */
void init_sensor() {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,          // Detecta flanco de bajada (0 lógico)
        .mode = GPIO_MODE_INPUT,                 // Configura como entrada
        .pin_bit_mask = (1ULL << SENSOR_GPIO),   // Apunta al pin definido como sensor
        .pull_up_en = GPIO_PULLUP_ENABLE         // Habilita resistencia pull-up interna
    };
    gpio_config(&io_conf);                       // Aplica la configuración al pin

    gpio_install_isr_service(0);                 // Instala servicio general de interrupciones (una sola vez)
    gpio_isr_handler_add(SENSOR_GPIO, sensor_isr, NULL);  // Asocia el ISR a este pin
}


/**
 * @brief Inicializa el ADC del ESP32 para leer el voltaje de batería.
 *
 * Esta función configura el canal ADC que se usará para medir el voltaje,
 * ajustando tanto la resolución como la atenuación del pin analógico.
 *
 * Acciones realizadas:
 * - Establece una resolución de 12 bits para el ADC (0–4095).
 * - Configura una atenuación de 11 dB para que pueda leer voltajes más altos
 *   (hasta ~3.6 V) sin dañarse ni saturarse.
 * - Esta configuración es necesaria para leer voltajes como el de una batería
 * - El canal que se usa está definido por `BAT_ADC_CH`, por ejemplo: `ADC1_CHANNEL_0`.
 */
void init_adc() {
    adc1_config_width(ADC_WIDTH_BIT_12);               // Resolución de 12 bits → valores de 0 a 4095
    adc1_config_channel_atten(BAT_ADC_CH, ADC_ATTEN_DB_11);  // Atenuación de 11 dB → hasta ~3.6 V de entrada
}


/**
 * @brief Lee el voltaje de la batería a través del ADC.
 *
 * Esta función convierte la lectura cruda del ADC en un valor de voltaje real (en voltios),
 * teniendo en cuenta que:
 * - El ADC devuelve un valor de 0 a 4095 (resolución de 12 bits).
 * - El voltaje de referencia se considera 3.3 V.
 * - Hay un divisor resistivo conectado al pin ADC para no superar el máximo permitido por el ESP32.
 *
 * El resultado se escala por un factor de corrección (BAT_DIV_FACTOR) para obtener el valor
 * real de la batería antes del divisor.
 *
 * @return Voltaje estimado de la batería en voltios.
 */
float leerVoltajeBateria() {
    // Lee el valor crudo del ADC (entre 0 y 4095)
    int val = adc1_get_raw(BAT_ADC_CH);

    // Convierte ese valor a voltios reales:
    // - (val / 4095.0f): convierte el valor crudo en una fracción del máximo posible.
    // - * 3.3f: lo escala al rango de voltaje del ADC (típicamente 3.3 V).
    // - * BAT_DIV_FACTOR: corrige por el divisor resistivo (por ejemplo, si dividimos el voltaje por 2).
    return (val / 4095.0f) * 3.3f * BAT_DIV_FACTOR;
}


/**
 * @brief Calcula la velocidad lineal del longboard en metros por segundo (m/s)
 *
 * Esta función se basa en las interrupciones generadas por el sensor óptico.
 * Cada interrupción indica que una "ranura" de la rueda pasó frente al sensor.
 * Si sabemos cuántas ranuras tiene la rueda (`SENSOR_SLOTS`) y medimos el tiempo
 * entre dos interrupciones (`pulse_interval_us`), podemos estimar la cantidad de
 * vueltas por segundo (revoluciones por segundo, o RPS), y a partir de eso,
 * calcular la velocidad lineal
 * @return Velocidad del longboard en metros por segundo.
 */
float calcularVelocidad() {
    // Evita divisiones por cero o valores inválidos
    if (pulse_interval_us <= 0) return 0;

    // Calcula las revoluciones por segundo (RPS)
    // pulse_interval_us: tiempo entre dos ranuras, en microsegundos
    // SENSOR_SLOTS: cantidad de ranuras por vuelta
    float rps = 1000000.0f / (pulse_interval_us * SENSOR_SLOTS);
    return 2 * M_PI * WHEEL_RADIUS * rps;
}


/**
 * @brief Frena el motor de manera progresiva reduciendo el PWM.
 *
 * Esta función reduce lentamente el valor de `duty_cycle` (ciclo de trabajo del PWM),
 * simulando un frenado suave del motor. No corta de golpe la alimentación, sino que
 * lo hace en pasos de -10 hasta llegar a 0. Luego desactiva los modos de aceleración
 * - Disminuye el duty actual hacia 0 en intervalos regulares
 * - Aplica ese duty (en porcentaje) usando `PWMSetDutyCycle()`
 * - Espera 50 ms entre cada cambio para lograr suavidad
 * - Resetea las banderas de control BLE (`mantener_presionado`, `mantener_velocidad`)
 */
void frenar() {
    // Bucle que reduce el valor del duty actual a 0, en pasos de -10
    for (int i = duty_cycle; i >= 0; i -= 10) {
        // Convierte duty de escala [0–255] a porcentaje [0–100]
        PWMSetDutyCycle(PWM_OUT, i * 100 / 255); //PWMSetDutyCycle() espera un valor en porcentaje entre 0 y 100.
                                                 //duty_cycle está manejado internamente en una escala de 0 a 255 (resolución de 8 bits).

        // Espera 50 milisegundos para aplicar suavemente el cambio
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Reinicia duty a 0 (el motor se detiene por completo)
    duty_cycle = 0;

    // Desactiva modos de control por BLE
    mantener_presionado = false;
    mantener_velocidad = false;
}

/**
 * @brief Tarea FreeRTOS encargada de interpretar los comandos BLE y controlar el motor
 *
 * Esta función corre en bucle infinito y se ejecuta cada 100 ms
 * Toma decisiones según los comandos recibidos desde la app móvil por BLE
 *
 * - Si se recibió un comando de frenado (`cmd_frenar`), ejecuta frenado progresivo
 * - Si el botón está presionado (`mantener_presionado`), acelera progresivamente
 * - Si no hay comandos activos y no hay batería baja, mantiene el PWM actual
 */
void tarea_comandos(void *param) {
    while (1) {

        // 1. Si se recibió un comando de frenado desde BLE
        if (cmd_frenar) {
            frenar();             // Frena el motor de forma progresiva
            cmd_frenar = false;   // Resetea la bandera para no frenar de nuevo
        }

        // 2. Si se mantiene presionado el botón desde la app y no hay batería baja
        if (mantener_presionado && !bateria_baja) {
            if (duty_cycle < 255) {
                duty_cycle += 10;                            // Aumenta el duty cycle
                if (duty_cycle > 255) duty_cycle = 255;      // No pasa de 255
                PWMSetDutyCycle(PWM_OUT, duty_cycle * 100 / 255);  // Aplica duty en %
            }
        }

        // 3. Si no se mantiene presionado, ni doble toque, ni hay batería baja
        if (!mantener_presionado && !mantener_velocidad && !bateria_baja) {
            // Reaplica el último duty actual, para mantener velocidad estable
            PWMSetDutyCycle(PWM_OUT, duty_cycle * 100 / 255);
        }

        // Espera 100 milisegundos antes de volver a revisar el estado
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


/**
 * @brief Tarea periodica que monitorea la bateria, velocidad y envia datos por BLE
 *
 * Esta tarea corre constantemente cada 1 segundo
 * - Verifica si la conexión Bluetooth está activa
 *     - Si está desconectado, frena automáticamente el motor y simula estado de batería baja.
 * - Lee el voltaje de la batería con el ADC.
 *     - Si es menor al 10%, frena el motor automáticamente.
 * - Calcula la velocidad usando el sensor óptico.
 * - Envía la velocidad y batería como string vía BLE si está conectado.
 */
void tarea_monitoreo(void *param) {
    while (1) {

        // 1. Verifica si BLE está conectado
        if (BleStatus() != BLE_CONNECTED) {
            frenar();             // Frena por seguridad
            bateria_baja = true;  // Simula condición de protección
            vTaskDelay(pdMS_TO_TICKS(1000));  // Espera antes de volver a chequear
            continue;             // No ejecuta el resto del loop
        }

        // 2. Lectura y verificación de batería
        float volt = leerVoltajeBateria();          // Lee voltaje en V
        float pct  = volt * 100 / 4.2f;              // Convierte a porcentaje (considerando 4.2V como 100%)

        if (pct < BAT_LOW_PERCENT && !bateria_baja) {
            frenar();              // Frena el motor si la batería está baja
            bateria_baja = true;  // Marca estado de protección activa
        } else if (pct >= BAT_LOW_PERCENT) {
            bateria_baja = false; // Se recuperó
        }

        // 3. Calcula velocidad
        float vel = calcularVelocidad();  // En m/s

        // 4. Envía datos por BLE como string
        char msg[32];
        snprintf(msg, sizeof(msg), "VEL:%.2f;BAT:%.0f", vel, pct);
        BleSendString(msg);  // Notificación BLE

        // 5. Espera 1 segundo antes del próximo ciclo
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}



void app_main(void) {

/**
 * 
 * La parte del esp_err_t y lo que sigue es crítica para el funcionamiento del stack BLE
 * El almacenamiento NVS es usado por BLE para guardar configuraciones internas,
 * bonding de dispositivos, direcciones MAC, etc.
 * Este bloque se asegura de que:
 * - Si la NVS está en buen estado, se inicializa normalmente.
 * - Si la NVS está llena o con una versión vieja (por ejemplo tras flasheos múltiples),
 *   se borra y se vuelve a inicializar automáticamente
 *
 * Si este bloque no está presente y hay un problema en NVS,
 * el BLE puede no arrancar o fallar silenciosamente
 */
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();      // Borra el contenido corrupto o viejo
    nvs_flash_init();       // Intenta inicializar de nuevo
}

    // Libera recursos de Bluetooth clásico que no se usan (solo BLE)
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    // Inicializaciones
    init_pwm();
    init_sensor();
    init_adc();

    // BLE como en ejemplo funcional
    ble_config_t cfg = {
        "ESP_LONGBRD",  // Nombre del dispositivo BLE
        read_data       // Callback de recepción
    };
    BleInit(&cfg);

    xTaskCreate(tarea_comandos, "Comandos", 2048, NULL, 2, NULL);
    xTaskCreate(tarea_monitoreo, "Monitoreo", 2048, NULL, 1, NULL);

}


