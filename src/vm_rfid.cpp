/**
 * @file vm_rfid.cpp
 * @brief Implementación del driver RFID no bloqueante para MFRC522.
 */

#include "vm_rfid.h"
#include <SPI.h>

VmRfid::VmRfid()
    : _mfrc(VM_RFID_SS_PIN, VM_RFID_RST_PIN),
      _callback(nullptr),
      _available(false) {}

bool VmRfid::begin() {
    SPI.begin(VM_RFID_SCK_PIN, VM_RFID_MISO_PIN, VM_RFID_MOSI_PIN, VM_RFID_SS_PIN);
    _mfrc.PCD_Init();

    // Verificar que el módulo responde leyendo la versión del firmware.
    // Si el registro devuelve 0x00 o 0xFF, el módulo no está conectado.
    byte version = _mfrc.PCD_ReadRegister(MFRC522::VersionReg);
    if (version == 0x00 || version == 0xFF) {
        Serial.println("[RFID] Advertencia: MFRC522 no detectado. "
                       "Verificar cableado SPI y alimentacion 3.3V.");
        _available = false;
        return false;
    }

    Serial.printf("[RFID] MFRC522 listo. Version=0x%02X\n", version);
    _available = true;
    return true;
}

void VmRfid::poll() {
    if (!_available || !_callback) return;

    // PICC_IsNewCardPresent() es rápido y no bloqueante.
    if (!_mfrc.PICC_IsNewCardPresent()) return;
    if (!_mfrc.PICC_ReadCardSerial())   return;

    String uid = uidToHex(_mfrc.uid);
    Serial.printf("[RFID] Tarjeta detectada: %s\n", uid.c_str());
    _callback(uid);

    // Detener la comunicación para permitir la detección de otras tarjetas.
    _mfrc.PICC_HaltA();
    _mfrc.PCD_StopCrypto1();
}

void VmRfid::onCard(RfidCallback callback) {
    _callback = callback;
}

String VmRfid::uidToHex(MFRC522::Uid& uid) {
    String hex;
    hex.reserve(uid.size * 2);
    for (uint8_t i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) hex += '0';
        hex += String(uid.uidByte[i], HEX);
    }
    hex.toUpperCase();
    return hex;
}
