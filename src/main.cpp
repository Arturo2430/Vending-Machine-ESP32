/**
 * @file main.cpp
 * @brief Punto de entrada del firmware ESP32 — Máquina Expendedora SAID.
 *
 * Inicializa todos los subsistemas en order de dependencia y los conecta
 * mediante callbacks.  El bucle principal es estrictamente no bloqueante.
 *
 * Orden de inicialización en setup():
 *   1. LittleFS (sistema de archivos).
 *   2. VmDatabase (SQLite + semillado automático).
 *   3. VmWebServer (SoftAP + servidor HTTP asíncrono).
 *   4. VmRfid (SPI — tolerante a falla de módulo ausente).
 *   5. VmUartLink + VmEsp32Controller (UART hacia el Mega + handshake).
 *   6. VmFsm (máquina de estados — espera handshake completado antes de operar).
 *
 * Bucle en loop():
 *   uartLink.poll()  → procesa tramas UART sin bloquear.
 *   rfid.poll()      → sondea tarjetas RFID sin bloquear.
 *   fsm.update()     → avanza timers y transiciones de estado.
 *
 * @warning Cero llamadas a delay() en producción.  Cualquier invocación a
 *          delay() en este archivo o en los módulos que llama puede corromper
 *          el parser incremental de UART al saturar el buffer de hardware.
 */

#include <Arduino.h>
#include <LittleFS.h>

#include "vm_board_config.h"
#include "vm_uart_protocol.h"
#include "vm_uart_link.h"
#include "vm_esp32_controller.h"
#include "vm_database.h"
#include "vm_web_server.h"
#include "vm_rfid.h"
#include "vm_fsm.h"

// ---------------------------------------------------------------------------
// Instancias globales (una sola vez en todo el firmware)
// ---------------------------------------------------------------------------

static VmUartLink        uartLink;
static VmEsp32Controller controller;
static VmDatabase        vendingDB;
static VmWebServer       webServer(&vendingDB);
static VmRfid            rfid;
static VmFsm*            fsmPtr = nullptr; // Asignado en setup() tras construir con referencia a DB.

// ---------------------------------------------------------------------------
// Adaptadores: conectan los callbacks del controller y rfid hacia la FSM.
// (Funciones libres estáticas para evitar captura de this en lambdas globales.)
// ---------------------------------------------------------------------------

static void onHandshakeDone(uint8_t major, uint8_t minor) {
    Serial.printf("[UART] Handshake completado con Mega v%u.%u\n", major, minor);
    if (fsmPtr) fsmPtr->handleHandshakeComplete();
}

static void onKeyReceived(char keyAscii, uint8_t keySeq) {
    Serial.printf("[UART] KEY='%c' seq=%u\n", keyAscii, keySeq);
    if (fsmPtr) fsmPtr->handleKey(keyAscii);
}

static void onVendResultReceived(uint32_t txId, uint8_t result) {
    Serial.printf("[UART] RESULT tx=%lu result=%u\n", (unsigned long)txId, result);
    if (fsmPtr) fsmPtr->handleVendResult(txId, result);
}

static void onStatusReceived(uint8_t door, uint8_t barrier, uint8_t mode, uint32_t currentTxId) {
    Serial.printf("[UART] STATUS door=%u barrier=%u mode=%u tx=%lu\n",
                  door, barrier, mode, (unsigned long)currentTxId);
    // El estado del Mega es informativo; la FSM no reacciona directamente.
}

static void onAckReceived(uint8_t cmdRef, uint8_t result, uint8_t reason) {
    Serial.printf("[UART] ACK cmdRef=0x%02X result=%u reason=%u\n",
                  cmdRef, result, reason);
    // Si el Mega rechazó un VEND, la FSM lo detectará vía timeout de motor
    // o via el RESULT posterior.  Un ACK de rechazo aquí es informativo.
}

static void onRfidCard(const String& uid) {
    if (fsmPtr) fsmPtr->handleRfidCard(uid);
}

// ---------------------------------------------------------------------------
// Adaptadores para la FSM: wrappean los métodos del controller
// ---------------------------------------------------------------------------

static uint32_t fsmVend(uint8_t channel) {
    return controller.vend(channel);
}

static void fsmDisplay(const char* l1, const char* l2) {
    controller.updateDisplay(l1, l2);
}

static void fsmSetMode(uint8_t mode) {
    controller.setMode(mode);
}

// ---------------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500); // Solo en setup(), para dar tiempo al monitor serie.
    Serial.println("\n=== INICIANDO MAQUINA EXPENDEDORA SAID ===");

    // 1. Sistema de archivos.
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] Error crítico: no se pudo montar LittleFS.");
        Serial.println("     Verificar tamaño de partición en platformio.ini.");
        while (true) { delay(1000); } // Detenerse; no hay manera de continuar.
    }
    Serial.println("[FS] LittleFS montado.");

    // 2. Base de datos SQLite.
    if (!vendingDB.begin()) {
        Serial.println("[DB] Error crítico: base de datos no disponible.");
        while (true) { delay(1000); }
    }

    // 3. Servidor web (SoftAP + HTTP asíncrono).
    webServer.begin();

    // 4. Lector RFID (falla silenciosa si el módulo no está conectado).
    rfid.onCard(onRfidCard);
    if (!rfid.begin()) {
        Serial.println("[RFID] Módulo no detectado; pago RFID deshabilitado.");
        Serial.println("       Verificar: alimentacion 3.3V, pines SPI, cableado.");
    }

    // 5. UART hacia el Mega.
    Serial.println("[UART] Inicializando enlace con el Mega...");
    // ⚠ GPIO16 (RX2) recibe de TX1 (D18) del Mega.
    //   GPIO17 (TX2) transmite a RX1 (D19) del Mega.
    //   REQUIERE adaptador de niveles 5V→3.3V en la línea Mega→ESP32.
    Serial2.begin(VM_UART_BAUDRATE, SERIAL_8N1, VM_UART_RX_PIN, VM_UART_TX_PIN);
    uartLink.begin(Serial2);

    controller.begin(uartLink);
    controller.onHandshakeComplete(onHandshakeDone);
    controller.onKeyEvent(onKeyReceived);
    controller.onVendResult(onVendResultReceived);
    controller.onStatusUpdate(onStatusReceived);
    controller.onAck(onAckReceived);

    // 6. Máquina de estados.
    // Se construye en el heap con new para poder pasarle la referencia a vendingDB.
    fsmPtr = new VmFsm(vendingDB, fsmVend, fsmDisplay, fsmSetMode);
    fsmPtr->begin();

    // Iniciar handshake.  La FSM espera handleHandshakeComplete() antes de
    // avanzar de S0 a S2; mientras tanto muestra "Iniciando...".
    controller.startHandshake();

    Serial.println("=== SETUP COMPLETADO ===");
}

// ---------------------------------------------------------------------------
// loop() — Estrictamente no bloqueante.
// ---------------------------------------------------------------------------

void loop() {
    uartLink.poll();   // Recibe y procesa tramas UART del Mega.
    rfid.poll();       // Sondea nuevas tarjetas RFID.
    fsmPtr->update();  // Avanza timers y transiciones de estado.
}
