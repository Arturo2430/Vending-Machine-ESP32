/**
 * @file vm_board_config.h
 * @brief Parámetros de capa física del ESP32 (fuera del alcance del
 *        contrato UART de Arquitectura, ver sección 1.3 del contrato:
 *        baudrate, pines GPIO, etc. los define Electrónica).
 *
 * Este archivo es específico del proyecto ESP32. El proyecto del
 * Arduino Mega tiene su propio vm_board_config.h (sin pines, porque
 * Serial1 ya está fijo en hardware a D18/D19), pero DEBE compartir el
 * mismo VM_UART_BAUDRATE, ya que ambos extremos tienen que coincidir
 * en velocidad para entenderse.
 */

#ifndef VM_BOARD_CONFIG_H
#define VM_BOARD_CONFIG_H

// Acordado con Electrónica/Arquitectura. Debe ser idéntico en el Mega.
#define VM_UART_BAUDRATE 38400

// TODO: VALIDAR QUE LA VERSION FINAL TENGA ESTOS PINES
// Mapa de pines del ESP32 (documento Electrónica, UART2).
#define VM_UART_RX_PIN 16
#define VM_UART_TX_PIN 17

#endif // VM_BOARD_CONFIG_H
