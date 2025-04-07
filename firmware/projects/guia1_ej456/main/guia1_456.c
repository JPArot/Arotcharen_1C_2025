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

/*==================[Ejercicio 4]=================================*/
/*Escriba una función que reciba un dato de 32 bits, 
 la cantidad de dígitos de salida y un puntero a un arreglo donde se almacene los n dígitos. 
 La función deberá convertir el dato recibido a BCD, guardando cada uno de los dígitos de salida 
 en el arreglo pasado como puntero.*/

/**
 * @brief Esta función toma un número entero y lo convierte en un arreglo de dígitos BCD.
 *
 * @param data El número entero a convertir,sin signo de 32 bits.
 * @param digits La cantidad de dígitos en el número,sin signo de 8 bits.
 * @param bcd_number El arreglo donde se almacenará la representación BCD.de 8 bit.
 * @return 0 si la conversión fue exitosa.
 */

int8_t  convertToBcdArray (uint32_t data, uint8_t digits, uint8_t * bcd_number)
{
 // Verificación de puntero nulo	
    if (bcd_number == NULL) return -1;

// Con este for separo en centena decena unidad
    for (int i = digits - 1; i >= 0; --i)
    {
        bcd_number[i] = data % 10; // Extraigo el dígito menos significativo
        data /= 10;
    }
// Me tengo que fijar tambien si el número era demasiado grande
    if (data > 0) return -1; 
   return 0;
}
/*==================[Ejercicio 5]===============================*/
/*Escribir una función que reciba como parámetro un dígito BCD
y un vector de estructuras del tipo gpioConf_t.
Incluya el archivo de cabecera gpio_mcu.h
La función deberá cambiar el estado de cada GPIO, a ‘0’ o a ‘1’,
según el estado del bit correspondiente en el BCD ingresado.
Ejemplo: b0 se encuentra en ‘1’, el estado de GPIO_20 debe setearse.*/

/**
 * @brief Estructura para configurar los pines GPIO.
 */
typedef struct
{
    gpio_t pin; /*!< Número del pin GPIO */
    io_t dir;   /*!< Dirección: '0' entrada, '1' salida */
} gpioConf_t;

/* Definición de los pines GPIO para los segmentos del display */
gpioConf_t gpioArray[4] = {
    {GPIO_20, GPIO_OUTPUT},
    {GPIO_21, GPIO_OUTPUT},
    {GPIO_22, GPIO_OUTPUT},
    {GPIO_23, GPIO_OUTPUT}};

/**
 * @brief Configura los pines GPIO según un valor BCD.
 *
 * @param bcd Número BCD a interpretar.
 * @param gpioArray Arreglo con la configuración de los pines GPIO.
 */
	void setGpioState(uint8_t bcd, gpioConf_t *gpioArray)
	{
		if (gpioArray == NULL) return; // Valido por las dudas el puntero nulo
	
		for (int i = 0; i < 4; i++)
		{
		/*Aca uso una máscara, corriendo 1 i veces y comparando en el for cada dígito y dependiendo
        el resultado de la operación del and seteo el estado de los puertosGPIO correspondiente*/
			if ((bcd >> i) & 1)
			{
				GPIOOn(gpioArray[i].pin); // Activa el pin si el bit correspondiente es 1
			}
			else
			{
				GPIOOff(gpioArray[i].pin); // Apaga el pin si el bit es 0
			}
		}
	}

/*==================[Ejercicio 6]=========================*/
/*Escriba una función que reciba un dato de 32 bits,
la cantidad de dígitos de salida y dos vectores de estructuras del tipo  gpioConf_t.
Uno  de estos vectores es igual al definido en el punto anterior y el otro vector mapea los puertos 
con el dígito del LCD a donde mostrar un dato:
Dígito 1 -> GPIO_19
Dígito 2 -> GPIO_18
Dígito 3 -> GPIO_9
La función deberá mostrar por display el valor que recibe.
Reutilice las funciones creadas en el punto 4 y 5.
Realice la documentación de este ejercicio usando Doxygen.*/

/* Definición de los pines GPIO para los dígitos del display */
gpioConf_t gpioMap[3] = {
    {GPIO_19, GPIO_OUTPUT},
    {GPIO_18, GPIO_OUTPUT},
    {GPIO_9, GPIO_OUTPUT}};

/**
 * @brief Muestra un número en un display de 7 segmentos.
 *
 * @param data Número a mostrar.
 * @param digits Cantidad de dígitos en el display.
 * @param gpioArray Configuración de los pines para los segmentos.
 * @param gpioMap Configuración de los pines para los dígitos.
 * @param bcd_number Arreglo donde se almacena el número en formato BCD.
 */
 void mostrarDisplay(uint32_t data, uint8_t digits, gpioConf_t *gpioArray, gpioConf_t *gpioMap, uint8_t *bcd_number)
 {
	//Aca verifico si el numero es demasiado grande
	 if (convertToBcdArray(data, digits, bcd_number) != 0)
	 {
		 printf("Error: Número demasiado grande para el display.\n");
		 return;
	 }
 
	 for (int i = 0; i < digits; i++)
	 {
		 setGpioState(bcd_number[i], gpioArray); // Configura segmentos
		 GPIOOn(gpioMap[i].pin); // Activa el dígito actual
		 vTaskDelay(pdMS_TO_TICKS(5)); // Agrega un pequeño delay para evitar parpadeos
		 GPIOOff(gpioMap[i].pin); // Desactiva el dígito
	 }
 }


/*==================[external functions definition]==========================*/
void app_main(void)
{
    uint32_t numero = 123;
    uint8_t digits = 3;
    uint8_t bcd_number[digits];

    // Convierte el número a BCD
    convertToBcdArray(numero, digits, bcd_number);

    /*
     * Configuración de pines GPIO:
     * gpioArray -> Pines GPIO_20 a GPIO_23
     * gpioMap -> Pines GPIO_19, GPIO_18 y GPIO_9
     */

    gpioConf_t gpioArray[4] = {
        {GPIO_20, GPIO_OUTPUT},
        {GPIO_21, GPIO_OUTPUT}, // Conectado a la pantalla
        {GPIO_22, GPIO_OUTPUT},
        {GPIO_23, GPIO_OUTPUT}  // Vector de 4 estructuras gpioConf_t
    };

    gpioConf_t gpioMap[3] = {
        {GPIO_19, GPIO_OUTPUT},
        {GPIO_18, GPIO_OUTPUT}, // Conectado a la pantalla
        {GPIO_9, GPIO_OUTPUT}
    };

    // Inicializa los GPIOs del array gpioArray
    for (int i = 0; i < 4; i++)
    {
        GPIOInit(gpioArray[i].pin, gpioArray[i].dir);
    }

    // Inicializa los GPIOs del array gpioMap
    for (int i = 0; i < 3; i++)
    {
        GPIOInit(gpioMap[i].pin, gpioMap[i].dir);
    }

    // Muestra el número en el display
    mostrarDisplay(numero, digits, gpioArray, gpioMap, bcd_number);
}

	

/*==================[end of file]============================================*/