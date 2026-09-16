/**
 * @file vm_rfid.h
 * @brief Driver no bloqueante para el lector RFID MFRC522 (SPI).
 *
 * Pinout (acordado con Electrónica, ESP32 WROOM DevKit):
 *   SS/SDA → GPIO21   RST → GPIO22
 *   SCK    → GPIO18   MISO → GPIO19   MOSI → GPIO23
 *   VCC    → 3.3 V    GND → GND
 *
 * @warning NUNCA conectar al riel de 5 V del Mega; quema el MFRC522.
 * @warning Si el módulo no responde en el arranque, la biblioteca MFRC522
 *          puede bloquearse indefinidamente.  begin() detecta esa situación
 *          e informa; la FSM continúa sin RFID habilitado.
 */

#ifndef VM_RFID_H
#define VM_RFID_H

#include <Arduino.h>
#include <MFRC522.h>
#include "vm_board_config.h"

/** Firma del callback que recibe el UID leído. */
typedef void (*RfidCallback)(const String& uidHex);

class VmRfid {
public:
    VmRfid();

    /**
     * @brief Inicializa el bus SPI y el lector.
     * @return true si el MFRC522 respondió correctamente.
     */
    bool begin();

    /**
     * @brief Sondeo no bloqueante.  Llamar en cada loop().
     *
     * Si detecta una nueva tarjeta, extrae el UID hexadecimal y
     * llama a _callback.  Detiene la comunicación con la tarjeta
     * antes de regresar para no bloquear el bus.
     */
    void poll();

    /** Registra el callback hacia la FSM. */
    void onCard(RfidCallback callback);

    /** true si el lector se inicializó correctamente. */
    bool isAvailable() const { return _available; }

private:
    MFRC522     _mfrc;
    RfidCallback _callback;
    bool         _available;

    /** Convierte el array de bytes del UID a String hexadecimal en mayúsculas. */
    static String uidToHex(MFRC522::Uid& uid);
};

#endif // VM_RFID_H
