/**
 * @file vm_esp32_controller.h
 * @brief Lógica de negocio del rol ESP32 sobre el enlace UART VM.
 */

#ifndef VM_ESP32_CONTROLLER_H
#define VM_ESP32_CONTROLLER_H

#include <Arduino.h>
#include "vm_uart_protocol.h"
#include "vm_uart_link.h"

class VmEsp32Controller {
public:
    typedef void (*KeyEventCallback)(char keyAscii, uint8_t keySeq);
    typedef void (*VendResultCallback)(uint32_t transactionId, uint8_t result);
    typedef void (*StatusUpdateCallback)(uint8_t door, uint8_t barrier,
                                          uint8_t mode, uint32_t currentTxId);
    typedef void (*HandshakeCallback)(uint8_t megaVersionMajor, uint8_t megaVersionMinor);
    /**
     * @brief Callback emitido al recibir un ACK del Mega.
     *
     * Parámetros: cmdReferenciado, resultado (VM_ACK_RECEIVED/REJECTED), motivo.
     * Útil para que la FSM detecte si el Mega aceptó o rechazó un VEND antes
     * de moverse al estado S8_DISPENSANDO.
     */
    typedef void (*AckCallback)(uint8_t cmdRef, uint8_t result, uint8_t reason);
    
    /** Callback para el comando RFID_CARD (v2.1) */
    typedef void (*RfidCardCallback)(const String& uidHex);

    VmEsp32Controller();

    /** Conecta este controlador a un VmUartLink ya inicializado (begin() ya llamado). */
    void begin(VmUartLink& link);

    /**
     * Envía la trama HELLO inicial identificándose como ROLE_ESP32.
     * Llamar una vez en setup(), después de begin(). No bloquea
     * esperando la respuesta; el handshake se confirma vía
     * onHandshakeComplete() cuando llegue el HELLO del Mega.
     */
    void startHandshake();

    /** true una vez que el Mega respondió HELLO. */
    bool isHandshakeComplete() const;

    // ------------------------------------------------------------
    // Callbacks hacia la capa de negocio superior
    // ------------------------------------------------------------

    void onHandshakeComplete(HandshakeCallback callback);
    void onKeyEvent(KeyEventCallback callback);
    void onVendResult(VendResultCallback callback);
    void onStatusUpdate(StatusUpdateCallback callback);
    /** Registra el callback de ACK (opcional). */
    void onAck(AckCallback callback);
    /** Registra el callback de tarjeta RFID (opcional). */
    void onRfidCard(RfidCardCallback callback);

    // ------------------------------------------------------------
    // Acciones que el ESP32 puede iniciar
    // ------------------------------------------------------------

    /** Solicita STATUS al Mega. La respuesta llega por onStatusUpdate(). */
    void requestStatus();

    /** Ordena cambio de modo (VM_MODE_VENTA o VM_MODE_MANTENIMIENTO). */
    void setMode(uint8_t mode);

    /** Envía HEARTBEAT (diagnóstico opcional, sin periodicidad normativa). */
    void sendHeartbeat();

    /**
     * Genera un transaction_id nuevo y envía VEND(channel, txId).
     * Devuelve el transaction_id generado, o VM_TX_ID_NULL si channel
     * está fuera de 1..4 (validación defensiva del lado ESP32; el
     * Mega igual la revalida según el contrato).
     * El resultado físico llega después, de forma asíncrona, por
     * onVendResult().
     */
    uint32_t vend(uint8_t channel);

    /**
     * Actualiza el LCD 20x4 (v2.1). Las cadenas son normales terminadas en
     * '\0' (no requieren venir ya paddeadas); esta función se encarga
     * de recortar/rellenar a 20 caracteres y sustituir símbolos fuera
     * de rango ASCII imprimible por '?'.
     */
    void updateDisplay(const char* text1, const char* text2, const char* text3, const char* text4);

private:
    VmUartLink* _link;
    bool _handshakeComplete;
    uint32_t _nextTxId;  // Contador monotónico en RAM (ver nota abajo)

    KeyEventCallback    _onKey;
    VendResultCallback  _onVendResult;
    StatusUpdateCallback _onStatusUpdate;
    HandshakeCallback   _onHandshake;
    AckCallback         _onAck;
    RfidCardCallback    _onRfidCard;

    uint32_t generateTransactionId();
    void handleIncomingFrame(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint8_t len);
    static VmEsp32Controller* s_instance;
    static void frameTrampoline(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint8_t len);
};

#endif // VM_ESP32_CONTROLLER_H

