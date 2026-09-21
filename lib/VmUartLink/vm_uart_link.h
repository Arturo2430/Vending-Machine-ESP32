/**
 * @file vm_uart_link.h
 * @brief Capa de enlace UART: armado, envío y parseo de tramas del
 *        protocolo VM (Vending Machine), ESP32 <-> Arduino Mega.
 *
 * Basado en: Contrato UART, Ver. 2.1.0 (21/09/2026)
 *
 * v2.1: DISPLAY ahora envía 4 líneas × 20 chars (80 bytes).
 *       Nuevo helper sendRfidCard() (Mega → ESP32).
 */

#ifndef VM_UART_LINK_H
#define VM_UART_LINK_H

#include <Arduino.h>
#include "vm_uart_protocol.h"

class VmUartLink {
public:
    typedef void (*FrameCallback)(uint8_t cmd, uint8_t seq,
                                   const uint8_t* payload, uint8_t len);

    VmUartLink();

    void begin(Stream& serialPort);
    void onFrame(FrameCallback callback);
    void poll();
    uint8_t nextSeq();

    // --- Envío genérico ---
    void sendFrame(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint8_t len);

    // --- Helpers por comando ---
    void sendHello(uint8_t seq, uint8_t role);
    void sendAck(uint8_t seq, uint8_t cmdReferenciado, uint8_t resultado, uint8_t motivo);
    void sendStatusRequest(uint8_t seq);
    void sendStatusResponse(uint8_t seq, uint8_t door, uint8_t barrier,
                             uint8_t mode, uint32_t transactionId);
    void sendSetMode(uint8_t seq, uint8_t mode);
    void sendHeartbeat(uint8_t seq);
    void sendKey(uint8_t seq, uint8_t keyAscii, uint8_t keySeq);

    /**
     * Envía las 4 líneas del LCD 20×4.  Cada línea debe tener exactamente
     * VM_DISPLAY_LINE_LEN (20) bytes.  Usar padDisplayLine() si la cadena
     * fuente es más corta o contiene caracteres no imprimibles.
     */
    void sendDisplay(uint8_t seq,
                     const char line1[VM_DISPLAY_LINE_LEN],
                     const char line2[VM_DISPLAY_LINE_LEN],
                     const char line3[VM_DISPLAY_LINE_LEN],
                     const char line4[VM_DISPLAY_LINE_LEN]);

    void sendVend(uint8_t seq, uint8_t channel, uint32_t transactionId);
    void sendResult(uint8_t seq, uint32_t transactionId, uint8_t result);

    /**
     * Envía el UID de una tarjeta RFID leída por el Mega.
     * @param seq     Número de secuencia.
     * @param uidHex  UID en formato hexadecimal ASCII (ej. "A1B2C3D4").
     * @param uidLen  Longitud de uidHex en bytes (sin terminador nulo).
     */
    void sendRfidCard(uint8_t seq, const char* uidHex, uint8_t uidLen);

    // --- Utilidades estáticas ---
    static void encodeU32LE(uint32_t value, uint8_t* dest);
    static uint32_t decodeU32LE(const uint8_t* src);
    static int16_t expectedPayloadLen(uint8_t cmd);

    /**
     * Rellena dest[0..VM_DISPLAY_LINE_LEN-1] (20 chars) a partir de src.
     */
    static void padDisplayLine(const char* src, char dest[VM_DISPLAY_LINE_LEN]);

private:
    enum ParserState {
        WAIT_SOF1, WAIT_SOF2, WAIT_PROTO,
        WAIT_CMD, WAIT_SEQ, WAIT_LEN, WAIT_PAYLOAD
    };

    Stream* _serial;
    FrameCallback _callback;
    uint8_t _txSeq;

    ParserState _state;
    uint8_t _cmd;
    uint8_t _seq;
    uint8_t _len;
    uint8_t _payloadIndex;
    uint8_t _payload[VM_MAX_PAYLOAD_LEN]; // 80 bytes (v2.1)

    void resetParser();
    void handleByte(uint8_t b);
};

#endif // VM_UART_LINK_H
