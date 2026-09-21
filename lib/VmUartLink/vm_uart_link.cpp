/**
 * @file vm_uart_link.cpp
 * @brief Implementación de VmUartLink (ver vm_uart_link.h).
 *        Protocolo v2.1: DISPLAY 4×20, RFID_CARD.
 */

#include "vm_uart_link.h"

VmUartLink::VmUartLink()
    : _serial(nullptr), _callback(nullptr), _txSeq(0) {
    resetParser();
}

void VmUartLink::begin(Stream& serialPort) {
    _serial = &serialPort;
    resetParser();
}

void VmUartLink::onFrame(FrameCallback callback) {
    _callback = callback;
}

uint8_t VmUartLink::nextSeq() {
    uint8_t seq = _txSeq;
    _txSeq = (uint8_t)(_txSeq + 1);
    return seq;
}

// ------------------------------------------------------------
// Parser de recepción
// ------------------------------------------------------------

void VmUartLink::resetParser() {
    _state = WAIT_SOF1;
    _cmd = 0;
    _seq = 0;
    _len = 0;
    _payloadIndex = 0;
}

void VmUartLink::poll() {
    if (_serial == nullptr) return;
    while (_serial->available() > 0) {
        int b = _serial->read();
        if (b < 0) break;
        handleByte((uint8_t)b);
    }
}

void VmUartLink::handleByte(uint8_t b) {
    switch (_state) {
        case WAIT_SOF1:
            if (b == VM_SOF1) _state = WAIT_SOF2;
            break;

        case WAIT_SOF2:
            if (b == VM_SOF2) _state = WAIT_PROTO;
            else if (b == VM_SOF1) _state = WAIT_SOF2;
            else _state = WAIT_SOF1;
            break;

        case WAIT_PROTO:
            if (b == VM_PROTO_VERSION) _state = WAIT_CMD;
            else resetParser();
            break;

        case WAIT_CMD:
            _cmd = b;
            _state = WAIT_SEQ;
            break;

        case WAIT_SEQ:
            _seq = b;
            _state = WAIT_LEN;
            break;

        case WAIT_LEN:
            if (b > VM_MAX_PAYLOAD_LEN) {
                resetParser();
                break;
            }
            _len = b;
            _payloadIndex = 0;
            if (_len == 0) {
                if (_callback != nullptr) _callback(_cmd, _seq, nullptr, 0);
                resetParser();
            } else {
                _state = WAIT_PAYLOAD;
            }
            break;

        case WAIT_PAYLOAD:
            _payload[_payloadIndex++] = b;
            if (_payloadIndex >= _len) {
                if (_callback != nullptr) _callback(_cmd, _seq, _payload, _len);
                resetParser();
            }
            break;
    }
}

// ------------------------------------------------------------
// Envío de tramas
// ------------------------------------------------------------

void VmUartLink::sendFrame(uint8_t cmd, uint8_t seq, const uint8_t* payload, uint8_t len) {
    if (_serial == nullptr || len > VM_MAX_PAYLOAD_LEN) return;

    uint8_t header[VM_HEADER_LEN] = {
        VM_SOF1, VM_SOF2, VM_PROTO_VERSION, cmd, seq, len
    };
    _serial->write(header, VM_HEADER_LEN);
    if (len > 0 && payload != nullptr) {
        _serial->write(payload, len);
    }
}

void VmUartLink::sendHello(uint8_t seq, uint8_t role) {
    uint8_t payload[VM_LEN_HELLO] = {
        VM_HELLO_VERSION_MAJOR, VM_HELLO_VERSION_MINOR, role
    };
    sendFrame(VM_CMD_HELLO, seq, payload, VM_LEN_HELLO);
}

void VmUartLink::sendAck(uint8_t seq, uint8_t cmdRef, uint8_t resultado, uint8_t motivo) {
    uint8_t payload[VM_LEN_ACK] = { cmdRef, resultado, motivo };
    sendFrame(VM_CMD_ACK, seq, payload, VM_LEN_ACK);
}

void VmUartLink::sendStatusRequest(uint8_t seq) {
    sendFrame(VM_CMD_STATUS, seq, nullptr, VM_LEN_STATUS_REQ);
}

void VmUartLink::sendStatusResponse(uint8_t seq, uint8_t door, uint8_t barrier,
                                     uint8_t mode, uint32_t transactionId) {
    uint8_t payload[VM_LEN_STATUS_RESP];
    payload[0] = door;
    payload[1] = barrier;
    payload[2] = mode;
    encodeU32LE(transactionId, &payload[3]);
    sendFrame(VM_CMD_STATUS, seq, payload, VM_LEN_STATUS_RESP);
}

void VmUartLink::sendSetMode(uint8_t seq, uint8_t mode) {
    uint8_t payload[VM_LEN_SET_MODE] = { mode };
    sendFrame(VM_CMD_SET_MODE, seq, payload, VM_LEN_SET_MODE);
}

void VmUartLink::sendHeartbeat(uint8_t seq) {
    sendFrame(VM_CMD_HEARTBEAT, seq, nullptr, VM_LEN_HEARTBEAT);
}

void VmUartLink::sendKey(uint8_t seq, uint8_t keyAscii, uint8_t keySeq) {
    uint8_t payload[VM_LEN_KEY] = { keyAscii, keySeq };
    sendFrame(VM_CMD_KEY, seq, payload, VM_LEN_KEY);
}

void VmUartLink::sendDisplay(uint8_t seq,
                              const char line1[VM_DISPLAY_LINE_LEN],
                              const char line2[VM_DISPLAY_LINE_LEN],
                              const char line3[VM_DISPLAY_LINE_LEN],
                              const char line4[VM_DISPLAY_LINE_LEN]) {
    uint8_t payload[VM_LEN_DISPLAY];
    memcpy(payload,                              line1, VM_DISPLAY_LINE_LEN);
    memcpy(payload + VM_DISPLAY_LINE_LEN,        line2, VM_DISPLAY_LINE_LEN);
    memcpy(payload + VM_DISPLAY_LINE_LEN * 2,    line3, VM_DISPLAY_LINE_LEN);
    memcpy(payload + VM_DISPLAY_LINE_LEN * 3,    line4, VM_DISPLAY_LINE_LEN);
    sendFrame(VM_CMD_DISPLAY, seq, payload, VM_LEN_DISPLAY);
}

void VmUartLink::sendVend(uint8_t seq, uint8_t channel, uint32_t transactionId) {
    uint8_t payload[VM_LEN_VEND];
    payload[0] = channel;
    encodeU32LE(transactionId, &payload[1]);
    sendFrame(VM_CMD_VEND, seq, payload, VM_LEN_VEND);
}

void VmUartLink::sendResult(uint8_t seq, uint32_t transactionId, uint8_t result) {
    uint8_t payload[VM_LEN_RESULT];
    encodeU32LE(transactionId, &payload[0]);
    payload[4] = result;
    sendFrame(VM_CMD_RESULT, seq, payload, VM_LEN_RESULT);
}

void VmUartLink::sendRfidCard(uint8_t seq, const char* uidHex, uint8_t uidLen) {
    if (uidLen > VM_LEN_RFID_CARD_MAX) uidLen = VM_LEN_RFID_CARD_MAX;
    sendFrame(VM_CMD_RFID_CARD, seq, (const uint8_t*)uidHex, uidLen);
}

// ------------------------------------------------------------
// Utilidades estáticas
// ------------------------------------------------------------

void VmUartLink::encodeU32LE(uint32_t value, uint8_t* dest) {
    dest[0] = (uint8_t)(value & 0xFF);
    dest[1] = (uint8_t)((value >> 8) & 0xFF);
    dest[2] = (uint8_t)((value >> 16) & 0xFF);
    dest[3] = (uint8_t)((value >> 24) & 0xFF);
}

uint32_t VmUartLink::decodeU32LE(const uint8_t* src) {
    return  (uint32_t)src[0] |
            ((uint32_t)src[1] << 8) |
            ((uint32_t)src[2] << 16) |
            ((uint32_t)src[3] << 24);
}

int16_t VmUartLink::expectedPayloadLen(uint8_t cmd) {
    switch (cmd) {
        case VM_CMD_HELLO:     return VM_LEN_HELLO;
        case VM_CMD_ACK:       return VM_LEN_ACK;
        case VM_CMD_SET_MODE:  return VM_LEN_SET_MODE;
        case VM_CMD_HEARTBEAT: return VM_LEN_HEARTBEAT;
        case VM_CMD_KEY:       return VM_LEN_KEY;
        case VM_CMD_DISPLAY:   return VM_LEN_DISPLAY;
        case VM_CMD_VEND:      return VM_LEN_VEND;
        case VM_CMD_RESULT:    return VM_LEN_RESULT;
        case VM_CMD_RFID_CARD: return -1; // Variable (1..20 bytes)
        case VM_CMD_STATUS:    return -1; // Depende de dirección (0 o 7)
        default:               return -1;
    }
}

void VmUartLink::padDisplayLine(const char* src, char dest[VM_DISPLAY_LINE_LEN]) {
    uint8_t i = 0;
    for (; i < VM_DISPLAY_LINE_LEN && src[i] != '\0'; i++) {
        uint8_t c = (uint8_t)src[i];
        if (c < VM_DISPLAY_ASCII_MIN || c > VM_DISPLAY_ASCII_MAX) {
            dest[i] = (char)VM_DISPLAY_INVALID_CHAR;
        } else {
            dest[i] = (char)c;
        }
    }
    for (; i < VM_DISPLAY_LINE_LEN; i++) {
        dest[i] = (char)VM_DISPLAY_PAD_CHAR;
    }
}
