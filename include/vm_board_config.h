/**
 * @file vm_board_config.h
 * @brief Parámetros de capa física del ESP32: baudrate UART, pines GPIO.
 *
 * v2.1: RFID removido del ESP32 (ahora conectado al Mega).
 *       LCD actualizado a 20×4 I2C.
 *
 * @warning VALIDAR que la versión final de la placa tenga exactamente estos pines
 *          antes de energizar.  Una conexión incorrecta entre niveles 5 V (Mega) y
 *          3.3 V (ESP32) puede destruir los GPIO del ESP32 de forma permanente.
 */

#ifndef VM_BOARD_CONFIG_H
#define VM_BOARD_CONFIG_H

// ============================================================
// UART hacia el Arduino Mega (Serial2)
// ============================================================
#define VM_UART_BAUDRATE   38400
// GPIO16 recibe datos provenientes de TX1 (D18) del Mega.
// GPIO17 envía datos hacia   RX1 (D19) del Mega.
// ⚠ El Mega trabaja a 5 V: use un divisor resistivo o level-shifter en la
//   línea TX-Mega → RX-ESP32 para no sobrepasar 3.3 V en el GPIO16.
#define VM_UART_RX_PIN     16
#define VM_UART_TX_PIN     17

// ============================================================
// LCD — Características de la pantalla (para formateo de texto)
// ============================================================
// LCD I2C 20×4 en el Mega (dirección 0x27).
// El ESP32 no controla el LCD directamente; envía DISPLAY vía UART.
#define VM_LCD_COLS        20
#define VM_LCD_ROWS         4

// ============================================================
// Timeouts de la FSM (en milisegundos)
// ============================================================
// Inactividad en modo compra/reposo → cancelar y regresar a S2.
#define VM_TIMEOUT_INACTIVITY_MS    180000UL   // 180 s

// Tiempo máximo esperando respuesta RESULT del Mega tras enviar VEND.
#define VM_TIMEOUT_MOTOR_MS          10000UL   // 10 s

// Intervalo entre reintentos de VEND cuando el ACK no llega.
#define VM_UART_RETRY_INTERVAL_MS      200UL   // 200 ms

// Número máximo de reintentos de VEND antes de declarar FALLA.
#define VM_UART_MAX_RETRIES              2      // 2 reintentos → 3 intentos totales

// Intervalo de rotación de subpantallas en el carrusel del LCD.
#define VM_CAROUSEL_INTERVAL_MS        2000UL  // 2 s

// Penalización por 3 intentos fallidos de PIN de administrador.
#define VM_PIN_LOCKOUT_MS             30000UL  // 30 s

// Timeout de espera de tarjeta RFID en estado S6.
#define VM_RFID_TIMEOUT_MS            30000UL  // 30 s

#endif // VM_BOARD_CONFIG_H
