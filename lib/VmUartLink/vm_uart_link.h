/**
 * @file vm_uart_link.h
 * @brief Capa de enlace UART: armado, envío y parseo de tramas del
 *        protocolo VM (Vending Machine), ESP32 <-> Arduino Mega.
 *
 * Basado en: Contrato UART, Ver. 2.0.0 FINAL (12/09/2026), secciones
 * 2 (convenciones de datos) y 3 (formato general del frame).
 *
 * Este módulo NO conoce reglas de negocio: no valida canales de VEND,
 * no implementa idempotencia de transacciones, no decide modos de
 * operación ni interpreta teclas. Solo sabe:
 *   1. Construir una trama válida a partir de CMD/SEQ/PAYLOAD.
 *   2. Leer bytes entrantes y reconstruir tramas válidas, byte a byte,
 *      con resincronización ante corrupción (según 3.3 del contrato).
 *   3. Notificar al llamador cada trama completa recibida mediante un
 *      callback, para que la lógica de negocio (FSM, idempotencia,
 *      etc.) decida qué hacer con ella.
 *
 * El mismo código de este módulo puede usarse tanto en el ESP32 como
 * en el Arduino Mega: solo cambia qué comandos envía cada uno y cómo
 * reacciona su capa de negocio ante los que recibe.
 */

#ifndef VM_UART_LINK_H
#define VM_UART_LINK_H

#include <Arduino.h>   // Stream, HardwareSerial
#include "vm_uart_protocol.h"

class VmUartLink {
public:
    /**
     * Firma del callback invocado por cada trama completa y
     * sintácticamente válida (SOF, PROTO y LEN correctos) recibida.
     * payload puede ser nullptr si len == 0.
     */
    typedef void (*FrameCallback)(uint8_t cmd, uint8_t seq,
                                   const uint8_t* payload, uint8_t len);

    VmUartLink();

    /**
     * Inicializa el enlace sobre un puerto serie ya configurado
     * (Serial.begin(...) o Serial2.begin(...) debe haberse llamado
     * antes, con el baudrate definido por Electrónica; ese parámetro
     * está fuera del alcance de este contrato).
     */
    void begin(Stream& serialPort);

    /** Registra la función que recibirá cada trama válida entrante. */
    void onFrame(FrameCallback callback);

    /**
     * Debe llamarse continuamente desde loop(). Consume todos los
     * bytes disponibles en el puerto serie y actualiza el parser.
     * Nunca bloquea.
     */
    void poll();

    /**
     * Devuelve el siguiente número de secuencia propio (0..255,
     * incremento módulo 256) para tramas que este nodo origina.
     * No debe usarse para tramas de respuesta directa (ACK, eco de
     * HEARTBEAT, respuesta de STATUS/HELLO), que deben reutilizar el
     * SEQ del mensaje entrante (ver 2.5 del contrato).
     */
    uint8_t nextSeq();

    // ------------------------------------------------------------
    // Envío de tramas genérico
    // ------------------------------------------------------------

    /**
     * Arma y transmite una trama completa. len debe ser <=
     * VM_MAX_PAYLOAD_LEN; si es mayor, la función no envía nada
     * (falla silenciosa, ya que violaría 3.3 del contrato).
     */
    void sendFrame(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint8_t len);

    // ------------------------------------------------------------
    // Helpers de envío por comando (sección 6 del contrato)
    // ------------------------------------------------------------

    void sendHello(uint8_t seq, uint8_t role);
    void sendAck(uint8_t seq, uint8_t cmdReferenciado, uint8_t resultado, uint8_t motivo);
    void sendStatusRequest(uint8_t seq);
    void sendStatusResponse(uint8_t seq, uint8_t door, uint8_t barrier,
                             uint8_t mode, uint32_t transactionId);
    void sendSetMode(uint8_t seq, uint8_t mode);
    void sendHeartbeat(uint8_t seq);
    void sendKey(uint8_t seq, uint8_t keyAscii, uint8_t keySeq);

    /**
     * line1 y line2 deben tener exactamente VM_DISPLAY_LINE_LEN (16)
     * bytes cada uno. Usa vm_padDisplayLine() si tu texto original es
     * más corto o contiene caracteres fuera de rango ASCII imprimible.
     */
    void sendDisplay(uint8_t seq, const char line1[VM_DISPLAY_LINE_LEN],
                      const char line2[VM_DISPLAY_LINE_LEN]);
    void sendVend(uint8_t seq, uint8_t channel, uint32_t transactionId);
    void sendResult(uint8_t seq, uint32_t transactionId, uint8_t result);

    // ------------------------------------------------------------
    // Utilidades de codificación (independientes de la instancia)
    // ------------------------------------------------------------

    /** Codifica value en little-endian dentro de dest[0..3]. */
    static void encodeU32LE(uint32_t value, uint8_t* dest);

    /** Decodifica 4 bytes little-endian a partir de src. */
    static uint32_t decodeU32LE(const uint8_t* src);

    /**
     * Devuelve la longitud de payload esperada (fija) para un CMD,
     * o -1 si el comando no tiene longitud fija universal (caso de
     * STATUS, cuya solicitud mide 0 bytes y su respuesta 7; el
     * llamador debe distinguir por dirección/rol).
     * Útil para validar INVALID_LENGTH antes de aceptar una trama.
     */
    static int16_t expectedPayloadLen(uint8_t cmd);

    /**
     * Rellena dest[0..VM_DISPLAY_LINE_LEN-1] a partir de src
     * (terminada en '\0'), recortando o rellenando con espacios y
     * sustituyendo por '?' los caracteres fuera de rango ASCII
     * imprimible (0x20-0x7E), según 2.3 del contrato.
     */
    static void padDisplayLine(const char* src, char dest[VM_DISPLAY_LINE_LEN]);

private:
    enum ParserState {
        WAIT_SOF1,
        WAIT_SOF2,
        WAIT_PROTO,
        WAIT_CMD,
        WAIT_SEQ,
        WAIT_LEN,
        WAIT_PAYLOAD
    };

    Stream* _serial;
    FrameCallback _callback;
    uint8_t _txSeq;

    ParserState _state;
    uint8_t _cmd;
    uint8_t _seq;
    uint8_t _len;
    uint8_t _payloadIndex;
    uint8_t _payload[VM_MAX_PAYLOAD_LEN];

    void resetParser();
    void handleByte(uint8_t b);
};

#endif // VM_UART_LINK_H
