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
#include "analog_io_mcu.h"
#include "timer_mcu.h"


#define MOTOR_GPIO GPIO_7         // GPIO para PWM del motor
#define SENSOR_GPIO GPIO_6        // Sensor óptico
#define BAT_ADC_CH ADC1_CHANNEL_0 // GPIO0 en ESP-EDU
#define PWM_OUT PWM_0             // Salida PWM usada
#define PWM_FREQ_HZ 1000          // Frecuencia del PWM

#define SENSOR_SLOTS 20       // Ranuras por vuelta
#define WHEEL_RADIUS 0.03f    // Radio de rueda en metros
#define BAT_DIV_FACTOR 2.0f   // Divisor resistivo
#define BAT_LOW_PERCENT 10.0f // Umbral de batería baja

// === Variables de estado ===
static int duty_cycle = 0;
static bool mantener_velocidad = false;
static bool mantener_presionado = false;
static bool bateria_baja = false;
static bool cmd_frenar = false;

// --- DEFINICIONES DE LAS VARIABLES DE TIEMPO DEL SENSOR ---
uint32_t pulse_interval_us = 0;

static uint32_t last_pulse_time = 0;

static int64_t last_touch_time = 0; // Esta variable sigue siendo necesaria para la detección de doble toque.

/**
 * @brief Callback de recepción de comandos vía BLE
 *
 * Esta función se llama automáticamente cada vez que se recibe un mensaje desde la app móvil
 * a través del canal Bluetooth Low Energy (BLE).
 *
 * Interpreta el primer byte del mensaje (`data[0]`) como un comando de control, que puede ser:
 *
 * - 'A' (Avanzar):
 * - Si es un segundo toque dentro de 500 milisegundos -> activa mantener velocidad constante
 * - Si no, interpreta que el botón está siendo presionado -> activa aceleración progresiva
 *
 * - 'R' (Release):
 * - El usuario soltó el botón de avanzar -> se desactiva la aceleración
 *
 * - 'F' (Frenar):
 * - Se activa la bandera `cmd_frenar` para ejecutar el frenado progresivo en la tarea principal
 *
 * @param data   Puntero a los datos recibidos vía BLE en formato ASCII
 * @param length Longitud del array recibido (no usado en este caso)
 */
void read_data(uint8_t *data, uint8_t length)
{
    int64_t now = esp_timer_get_time(); // Momento actual en microsegundos

    if (data[0] == 'A')
    { /** Comando "Avanzar"
        if (now - last_touch_time < 500000)
        {                              // Si es un doble toque (<500 ms)
            mantener_velocidad = true; // Activar modo de velocidad constante
        }
        else
        {
            mantener_presionado = true; // Interpretar como botón sostenido (aceleración)
        }
        last_touch_time = now; // Guardar tiempo del toque actual
       */
        mantener_velocidad=true;
    }
    /**else if (data[0] == 'R')
    {  Comando "Release" → soltar botón
        mantener_presionado = false;
    }
    */
    else if (data[0] == 'F')
    {                      // Comando "Frenar"
        cmd_frenar = true; // Se activará el frenado en la tarea de comandos
    }
}

/**
 * @brief ISR del sensor óptico usando timer_mcu.h
 *
 * Esta rutina de interrupción se llama automáticamente cuando el sensor óptico
 * detecta el paso de una ranura (flanco de bajada en el pin configurado).
 * Su propósito es medir el intervalo de tiempo entre dos eventos consecutivos,
 * que corresponde al tiempo entre dos ranuras pasando por el sensor.
 * Este valor luego se usa para calcular la velocidad lineal del longboard.
 *
 * Lee el tiempo actual desde TIMER_A al momento del pulso y calcula la diferencia
 * con el último pulso anterior. El valor queda almacenado en `pulse_interval_us`.
 */
void IRAM_ATTR sensor_isr(void *arg) {
    uint32_t now = TimerRead(TIMER_A);      // Lee el tiempo actual en us
    pulse_interval_us = now - last_pulse_time;    // Calcula intervalo entre los pulsos
    last_pulse_time = now;                 // Actualiza último tiempo
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
void init_pwm()
{
    PWMInit(PWM_OUT, MOTOR_GPIO, PWM_FREQ_HZ); // Configura PWM en el pin del motor
    PWMSetDutyCycle(PWM_OUT, 0);               // Comienza con duty del 0% (motor apagado)
}

/**
 * @brief Inicializa el pin del sensor óptico y su interrupción
 *
 * Esta versión utiliza el driver gpio_mcu.h específico para ESP-EDU.
 *
 * Acciones:
 * - Configura el pin como entrada digital con pull-up.
 * - Asocia la rutina de interrupción `sensor_isr()` al flanco de bajada (ranura detectada).
 * - (Opcional) Aplica filtro antirrebote si es necesario.
 */
void init_sensor(void) {
    GPIOInit(SENSOR_GPIO, GPIO_INPUT);               // Configura el pin como entrada con pull-up
    GPIOActivInt(SENSOR_GPIO, sensor_isr, false, NULL); // false = flanco negativo
    GPIOInputFilter(SENSOR_GPIO);                    // Aplica filtro antirrebote (opcional, hasta 8 GPIOs)
}

/**
 * @brief Inicializa el ADC del ESP32-EDU para leer el voltaje de batería.
 *
 * Esta versión utiliza el driver `analog_io_mcu.h`. Se configura el canal
 * analógico en modo de lectura única (no continua), para luego usar
 * `AnalogInputReadSingle()` cuando sea necesario.
 *
 * Acciones realizadas:
 * - Configura el canal `BAT_ADC_CH` como entrada analógica en modo simple.
 * - No requiere establecer resolución ni atenuación manualmente (el driver se encarga).
 */
void init_adc(void) {
    static analog_input_config_t battery_adc_config = {
        .input = BAT_ADC_CH,           // Canal de entrada, por ejemplo CH0
        .mode = ADC_SINGLE,            // Lectura única (no continua)
        .func_p = NULL,                // Sin callback, no es necesario en modo simple
        .param_p = NULL,               // No se pasan parámetros adicionales
        .sample_frec = 0               // No aplica en modo simple
    };

    AnalogInputInit(&battery_adc_config);
}


/**
 * @brief Lee el voltaje de la batería a través del ADC.
 *
 * Usa el driver `analog_io_mcu.h`, que entrega directamente el valor en milivoltios.
 * Se aplica el factor del divisor resistivo para estimar el voltaje real de la batería.
 *
 * @return Voltaje estimado de la batería en voltios.
 */
float leerVoltajeBateria(void) {
    uint16_t val_mv = 0;   // Variable para almacenar la lectura en milivoltios

    AnalogInputReadSingle(BAT_ADC_CH, &val_mv);  // Lee ADC en mV (ya escalado a 0–3300 mV)

    // Aplica el factor de corrección por el divisor resistivo
    return (val_mv / 1000.0f) * BAT_DIV_FACTOR;
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
float calcularVelocidad()
{
    // Evita divisiones por cero o valores inválidos.
    // Usa 'pulse_interval_us' que se actualiza en la ISR.
    if (pulse_interval_us <= 0)
        return 0;

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
void frenar(void) {
    for (int i = duty_cycle; i >= 0; i -= 10) {
        PWMSetDutyCycle(PWM_OUT, i * 100 / 255); // Duty convertido a %
        vTaskDelay(pdMS_TO_TICKS(50));           // Suaviza el frenado
    }

    duty_cycle = 0;
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
void tarea_comandos(void *param)
{
    while (1)
    {

        // 1. Si se recibió un comando de frenado desde BLE
        if (cmd_frenar)
        {
            frenar();           // Frena el motor de forma progresiva
            cmd_frenar = false; // Resetea la bandera para no frenar de nuevo
        }

        // 2. Si se mantiene presionado el botón desde la app y no hay batería baja
        if (mantener_presionado && !bateria_baja)
        {
            if (duty_cycle < 255)
            {
                duty_cycle += 10; // Aumenta el duty cycle
                if (duty_cycle > 255)
                    duty_cycle = 255;                             // No pasa de 255
                PWMSetDutyCycle(PWM_OUT, duty_cycle * 100 / 255); // Aplica duty en %
            }
        }

        /** 3. Si no se mantiene presionado, ni doble toque, ni hay batería baja
        if (!mantener_presionado && !mantener_velocidad && !bateria_baja)
        {
            // Reaplica el último duty actual, para mantener velocidad estable
            PWMSetDutyCycle(PWM_OUT, duty_cycle * 100 / 255);
        }*/

        // Espera 100 milisegundos antes de volver a revisar el estado
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarea periodica que monitorea la bateria, velocidad y envia datos por BLE
 *
 * Esta tarea corre constantemente cada 1 segundo
 * - Verifica si la conexión Bluetooth está activa
 * - Si está desconectado, frena automáticamente el motor y simula estado de batería baja.
 * - Lee el voltaje de la batería con el ADC.
 * - Si es menor al 10%, frena el motor automáticamente.
 * - Calcula la velocidad usando el sensor óptico.
 * - Envía la velocidad y batería como string vía BLE si está conectado.
 */
void tarea_monitoreo(void *param) {
    
    float velocidad = 0.0f;
    char msg[64];

    while (1) {
        // 1. Verifica estado de conexión BLE
        if (BleStatus() != BLE_CONNECTED) {
            frenar();                    // Frena por seguridad
            bateria_baja = true;        // Simula batería baja
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // 2. Lee batería usando la función encapsulada
        float voltaje_bat = leerVoltajeBateria(); // ¡Llamada a la función!

        // 3. Detecta batería baja (menor al 10% por ejemplo: < 3.3 V en 2S)
        if (voltaje_bat < 6.6f) { // Se asume 2S, 3.3V por celda es umbral
            bateria_baja = true;
            frenar();
        } else {
            bateria_baja = false;
        }

        // 4. Calcula velocidad 
        velocidad = calcularVelocidad();

        // 5. Envia datos por BLE
        snprintf(msg, sizeof(msg), "Bat: %.2f V | Vel: %.2f km/h", voltaje_bat, velocidad);
        BleSendString(msg);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Corre cada segundo
    }
}

void app_main(void)
{
    // === Inicializaciones de Hardware y Servicios ===
    // Definición de 'timer_config' como variable LOCAL de app_main()
    // Esto evita el error de "multiple definition" con cualquier otra definición
    // de 'timer_config' que pueda existir en los drivers de ESP-EDU.
    timer_config_t timer_config = {
        .timer = TIMER_A,
        .period = 1000000,      // 1 segundo, pero el contador no se resetea por callback
        .func_p = NULL,         // No se necesita callback en este caso para el TimerRead()
        .param_p = NULL
    };
    TimerInit(&timer_config);   // Inicializa el timer A con la configuración local
    TimerStart(TIMER_A);        // Comienza a contar (en microsegundos)

    init_pwm();                 // Inicializa el PWM del motor
    init_sensor();              // Inicializa el GPIO del sensor y la interrupción
    init_adc();                 // Inicializa el ADC de la batería

    // Inicialización del servicio BLE
    ble_config_t cfg = {
        "ESP_LONGBRD", // Nombre del dispositivo BLE
        read_data      // Callback de recepción de datos BLE
    };
    BleInit(&cfg);

    // === Creación de Tareas FreeRTOS ===
    // Las tareas se ejecutan en segundo plano después de las inicializaciones.
    xTaskCreate(tarea_comandos, "Comandos", 2048, NULL, 2, NULL); // Tarea de control de comandos BLE
    xTaskCreate(tarea_monitoreo, "Monitoreo", 2048, NULL, 1, NULL); // Tarea de monitoreo de batería/velocidad
}