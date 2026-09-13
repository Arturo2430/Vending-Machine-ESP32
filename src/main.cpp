/**
 * @file main.cpp
 * @brief Punto de entrada del firmware ESP32.
 *
 * Este archivo por ahora SOLO conecta las piezas de comunicación UART
 * ya existentes (VmUartLink + VmEsp32Controller) y hace el handshake
 * inicial con el Mega.
 *
 * Sirve como base compilable para verificar el enlace físico con el
 * Mega antes de empezar a construir la lógica de negocio encima.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include "vm_board_config.h"
#include "vm_uart_protocol.h"
#include "vm_uart_link.h"
#include "vm_esp32_controller.h"
#include "vm_database.h"
#include "vm_web_server.h"

VmUartLink uartLink;
VmEsp32Controller controller;
VmDatabase vendingDB;
VmWebServer webServer(&vendingDB);

// ------------------------------------------------------------
// Callbacks temporales de diagnóstico (solo imprimen por Serial USB).
// La lógica de negocio real reemplazará estas funciones más adelante.
// ------------------------------------------------------------

void onHandshakeComplete(uint8_t megaVersionMajor, uint8_t megaVersionMinor) {
    Serial.print("[UART] Handshake OK con Mega v");
    Serial.print(megaVersionMajor);
    Serial.print(".");
    Serial.println(megaVersionMinor);
}

void onKeyEvent(char keyAscii, uint8_t keySeq) {
    Serial.print("[UART] KEY recibido: ");
    Serial.print(keyAscii);
    Serial.print(" (seq=");
    Serial.print(keySeq);
    Serial.println(")");
}

void onVendResult(uint32_t transactionId, uint8_t result) {
    Serial.print("[UART] RESULT tx=");
    Serial.print(transactionId);
    Serial.print(" result=");
    Serial.println(result);
}

void onStatusUpdate(uint8_t door, uint8_t barrier, uint8_t mode, uint32_t currentTxId) {
    Serial.print("[UART] STATUS door=");
    Serial.print(door);
    Serial.print(" barrier=");
    Serial.print(barrier);
    Serial.print(" mode=");
    Serial.print(mode);
    Serial.print(" tx=");
    Serial.println(currentTxId);
}

void setup() {
    Serial.begin(115200); // Puerto USB de diagnóstico
    delay(1000);
    
    Serial.println("\n--- INICIANDO VENDING MACHINE SAID ---");

    // 1. Inicializar Sistema de Archivos
    if (!LittleFS.begin(true)) {
        Serial.println("Error montando LittleFS");
        return;
    }
    Serial.println("[FS] LittleFS montado correctamente.");

    // 2. Inicializar Base de Datos (SQLite)
    vendingDB.begin();

    // 3. Inicializar Servidor Web y SoftAP
    webServer.begin();

    // 4. Inicializar UART hacia el Mega
    Serial.println("[UART] Inicializando enlace con el Mega...");
    Serial2.begin(VM_UART_BAUDRATE, SERIAL_8N1, VM_UART_RX_PIN, VM_UART_TX_PIN);
    uartLink.begin(Serial2);

    controller.begin(uartLink);
    controller.onHandshakeComplete(onHandshakeComplete);
    controller.onKeyEvent(onKeyEvent);
    controller.onVendResult(onVendResult);
    controller.onStatusUpdate(onStatusUpdate);

    controller.startHandshake();
}

void loop() {
    uartLink.poll();
}
