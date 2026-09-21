/**
 * @file vm_uart_protocol.h
 * @brief Definición de constantes, comandos y enumeraciones del protocolo
 *        UART de la máquina expendedora (VM = Vending Machine),
 *        ESP32 <-> Arduino Mega.
 *
 * Basado en: Contrato UART, Ver. 2.1.0 (21/09/2026)
 *
 * Cambios v2.0 → v2.1:
 *   - VM_MAX_PAYLOAD_LEN: 32 → 80 (LCD 20×4).
 *   - DISPLAY: 2 líneas × 16 → 4 líneas × 20.
 *   - Nuevo comando RFID_CARD (0x14): Mega → ESP32 (UID de tarjeta).
 *   - RFID RC522 ahora conectado al Mega (no al ESP32).
 *
 * Este archivo debe mantenerse IDÉNTICO en el firmware del ESP32 y en el
 * firmware del Arduino Mega. Cualquier cambio aquí debe reflejarse en
 * ambos proyectos simultáneamente (UART-REQ-001).
 */

#ifndef VM_UART_PROTOCOL_H
#define VM_UART_PROTOCOL_H

#include <stdint.h>  // UART-REQ-001: solo tipos de ancho fijo

/* ============================================================
 * 3. FORMATO GENERAL DEL FRAME
 * ============================================================ */

#define VM_SOF1                  0xA5u  // Byte 1 de sincronización
#define VM_SOF2                  0x5Au  // Byte 2 de sincronización
#define VM_PROTO_VERSION         0x02u  // Versión mayor del protocolo

#define VM_HEADER_LEN            6u     // SOF1+SOF2+PROTO+CMD+SEQ+LEN
#define VM_MAX_PAYLOAD_LEN       80u    // LEN máximo permitido (v2.1: 80 para LCD 20×4)
#define VM_MIN_FRAME_LEN         (VM_HEADER_LEN)                      // 6 bytes, LEN=0
#define VM_MAX_FRAME_LEN         (VM_HEADER_LEN + VM_MAX_PAYLOAD_LEN) // 86 bytes

// Offsets dentro de la trama (útiles para el parser)
#define VM_OFFSET_SOF1           0u
#define VM_OFFSET_SOF2           1u
#define VM_OFFSET_PROTO          2u
#define VM_OFFSET_CMD            3u
#define VM_OFFSET_SEQ            4u
#define VM_OFFSET_LEN            5u
#define VM_OFFSET_PAYLOAD        6u

/* ============================================================
 * 4. DICCIONARIO DEFINITIVO DE COMANDOS (CMD)
 * ============================================================ */

typedef enum {
    VM_CMD_HELLO      = 0x01,  // Ambas direcciones
    VM_CMD_ACK        = 0x02,  // Ambas direcciones
    VM_CMD_STATUS     = 0x03,  // ESP32 -> Mega (solicitud) / Mega -> ESP32 (respuesta)
    VM_CMD_SET_MODE   = 0x04,  // ESP32 -> Mega
    VM_CMD_HEARTBEAT  = 0x05,  // ESP32 -> Mega -> ESP32
    VM_CMD_KEY        = 0x10,  // Mega -> ESP32
    VM_CMD_DISPLAY    = 0x11,  // ESP32 -> Mega
    VM_CMD_VEND       = 0x12,  // ESP32 -> Mega
    VM_CMD_RESULT     = 0x13,  // Mega -> ESP32
    VM_CMD_RFID_CARD  = 0x14   // Mega -> ESP32 (v2.1: UID de tarjeta RFID)
} vm_cmd_t;

/* ============================================================
 * 5.1 RESULTADO DE CONFIRMACIÓN (campo "resultado" en ACK)
 * ============================================================ */

typedef enum {
    VM_ACK_RECEIVED = 0x00,  // Trama aceptada
    VM_ACK_REJECTED = 0x01   // Trama rechazada (ver "motivo")
} vm_ack_result_t;

/* ============================================================
 * 5.2 MOTIVOS DE RECHAZO (campo "motivo" en ACK)
 * ============================================================ */

typedef enum {
    VM_REASON_NONE                   = 0x00,
    VM_REASON_INVALID_CMD            = 0x01,
    VM_REASON_INVALID_LENGTH         = 0x02,
    VM_REASON_INVALID_PAYLOAD        = 0x03,
    VM_REASON_INVALID_STATE          = 0x04,
    VM_REASON_BUSY                   = 0x05,
    VM_REASON_INVALID_CHANNEL        = 0x06,
    VM_REASON_VERSION_MISMATCH       = 0x07,
    VM_REASON_DUPLICATE_CONFLICT     = 0x08,
    VM_REASON_UNSAFE_PHYSICAL_STATE  = 0x09,
    VM_REASON_INTERNAL_ERROR         = 0x0A,
    VM_REASON_UNKNOWN_TRANSACTION    = 0x0B
} vm_reject_reason_t;

/* ============================================================
 * 5.3 RESULTADO FÍSICO DE DISPENSADO (campo "result" en RESULT)
 * ============================================================ */

typedef enum {
    VM_RESULT_DELIVERED               = 0x00,
    VM_RESULT_REJECTED_BEFORE_MOTION  = 0x01,
    VM_RESULT_UNCERTAIN                = 0x02
} vm_dispense_result_t;

/* ============================================================
 * 5.4 IDENTIFICADOR DE ROL (campo "role" en HELLO)
 * ============================================================ */

typedef enum {
    VM_ROLE_ESP32 = 0x00,
    VM_ROLE_MEGA  = 0x01
} vm_role_t;

/* ============================================================
 * 5.5 MODO DE OPERACIÓN DEL SISTEMA (campo "mode")
 * ============================================================ */

typedef enum {
    VM_MODE_VENTA         = 0x00,
    VM_MODE_MANTENIMIENTO = 0x01
} vm_mode_t;

/* ============================================================
 * 5.6 ESTADOS DE SENSORES FÍSICOS (respuesta STATUS)
 * ============================================================ */

typedef enum {
    VM_DOOR_CLOSED  = 0x00,
    VM_DOOR_OPEN    = 0x01,
    VM_DOOR_UNKNOWN = 0xFF
} vm_door_state_t;

typedef enum {
    VM_BARRIER_FREE     = 0x00,
    VM_BARRIER_OCCUPIED = 0x01,
    VM_BARRIER_UNKNOWN  = 0xFF
} vm_barrier_state_t;

/* ============================================================
 * 2.4 IDENTIFICADOR DE TRANSACCIÓN (transaction_id)
 * ============================================================ */

#define VM_TX_ID_NULL        0x00000000u  // Inválida / nula
#define VM_TX_ID_NONE        0xFFFFFFFFu  // Sin transacción en curso
#define VM_TX_ID_MIN         0x00000001u  // Rango operativo mínimo
#define VM_TX_ID_MAX         0xFFFFFFFEu  // Rango operativo máximo

/* ============================================================
 * 6. LONGITUDES DE PAYLOAD ESPERADAS POR COMANDO (LEN)
 * ============================================================
 * Útiles para validar INVALID_LENGTH en el parser.
 */

#define VM_LEN_HELLO             3u   // version_major, version_minor, role
#define VM_LEN_ACK               3u   // cmd_referenciado, resultado, motivo
#define VM_LEN_STATUS_REQ        0u   // Solicitud ESP32 -> Mega
#define VM_LEN_STATUS_RESP       7u   // door, barrier, mode, tx_id(4)
#define VM_LEN_SET_MODE          1u   // mode
#define VM_LEN_HEARTBEAT         0u
#define VM_LEN_KEY               2u   // key_ascii, key_seq
#define VM_LEN_DISPLAY           80u  // line1[20] + line2[20] + line3[20] + line4[20]
#define VM_LEN_VEND              5u   // channel, tx_id(4)
#define VM_LEN_RESULT            5u   // tx_id(4), result
// RFID_CARD tiene longitud VARIABLE: el UID puede ser 4, 7 o 10 bytes hex → 8, 14 o 20 chars.
#define VM_LEN_RFID_CARD_MIN     1u   // Mínimo 1 byte de UID
#define VM_LEN_RFID_CARD_MAX    20u   // Máximo 20 bytes (10 bytes UID × 2 hex chars)

#define VM_DISPLAY_LINE_LEN      20u  // Longitud fija por línea de LCD (v2.1: 20 chars)
#define VM_DISPLAY_NUM_LINES      4u  // Número de líneas del LCD (v2.1: 4 líneas)
#define VM_DISPLAY_PAD_CHAR      0x20u // Espacio ASCII de relleno
#define VM_DISPLAY_INVALID_CHAR  0x3Fu // '?' para fuera de rango ASCII imprimible
#define VM_DISPLAY_ASCII_MIN     0x20u // Rango ASCII imprimible: 0x20-0x7E
#define VM_DISPLAY_ASCII_MAX     0x7Eu

/* ============================================================
 * VALORES DE HELLO (versión de protocolo)
 * ============================================================ */

#define VM_HELLO_VERSION_MAJOR   2u
#define VM_HELLO_VERSION_MINOR   1u   // v2.1: LCD 20×4, RFID_CARD

/* ============================================================
 * RANGO DE CANALES DE DISPENSADO (VEND)
 * ============================================================ */

#define VM_CHANNEL_MIN           1u
#define VM_CHANNEL_MAX           4u

/* ============================================================
 * ESTRUCTURAS DE PAYLOAD (opcional, para acceso tipado)
 * ============================================================
 * NOTA: Estas structs son solo para uso en memoria/RAM dentro del
 * firmware, NO representan el layout exacto de bytes en el cable.
 */

typedef struct {
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t role;              // vm_role_t
} vm_payload_hello_t;

typedef struct {
    uint8_t cmd_referenciado;  // vm_cmd_t
    uint8_t resultado;         // vm_ack_result_t
    uint8_t motivo;            // vm_reject_reason_t
} vm_payload_ack_t;

typedef struct {
    uint8_t  door;             // vm_door_state_t
    uint8_t  barrier;          // vm_barrier_state_t
    uint8_t  mode;             // vm_mode_t
    uint32_t current_transaction_id;
} vm_payload_status_resp_t;

typedef struct {
    uint8_t mode;              // vm_mode_t
} vm_payload_set_mode_t;

typedef struct {
    uint8_t key_ascii;
    uint8_t key_seq;
} vm_payload_key_t;

typedef struct {
    char line1[VM_DISPLAY_LINE_LEN];
    char line2[VM_DISPLAY_LINE_LEN];
    char line3[VM_DISPLAY_LINE_LEN];
    char line4[VM_DISPLAY_LINE_LEN];
} vm_payload_display_t;

typedef struct {
    uint8_t  channel;
    uint32_t transaction_id;
} vm_payload_vend_t;

typedef struct {
    uint32_t transaction_id;
    uint8_t  result;           // vm_dispense_result_t
} vm_payload_result_t;

typedef struct {
    char uid_hex[VM_LEN_RFID_CARD_MAX + 1]; // UID como texto hex + terminador nulo
    uint8_t uid_len;                         // Longitud real del UID hex
} vm_payload_rfid_card_t;

#endif // VM_UART_PROTOCOL_H
