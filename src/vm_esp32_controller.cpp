/**
 * @file vm_esp32_controller.cpp
 * @brief Implementación de VmEsp32Controller (ver vm_esp32_controller.h).
 */

#include "vm_esp32_controller.h"

VmEsp32Controller* VmEsp32Controller::s_instance = nullptr;

VmEsp32Controller::VmEsp32Controller()
    : _link(nullptr),
      _handshakeComplete(false),
      _nextTxId(VM_TX_ID_MIN),
      _onKey(nullptr),
      _onVendResult(nullptr),
      _onStatusUpdate(nullptr),
      _onHandshake(nullptr) {
}

void VmEsp32Controller::begin(VmUartLink& link) {
    _link = &link;
    s_instance = this;
    _link->onFrame(&VmEsp32Controller::frameTrampoline);
}

void VmEsp32Controller::startHandshake() {
    if (_link == nullptr) {
        return;
    }
    _handshakeComplete = false;
    uint8_t seq = _link->nextSeq();
    _link->sendHello(seq, VM_ROLE_ESP32);
}

bool VmEsp32Controller::isHandshakeComplete() const {
    return _handshakeComplete;
}

void VmEsp32Controller::onHandshakeComplete(HandshakeCallback callback) {
    _onHandshake = callback;
}

void VmEsp32Controller::onKeyEvent(KeyEventCallback callback) {
    _onKey = callback;
}

void VmEsp32Controller::onVendResult(VendResultCallback callback) {
    _onVendResult = callback;
}

void VmEsp32Controller::onStatusUpdate(StatusUpdateCallback callback) {
    _onStatusUpdate = callback;
}

void VmEsp32Controller::requestStatus() {
    if (_link == nullptr) {
        return;
    }
    uint8_t seq = _link->nextSeq();
    _link->sendStatusRequest(seq);
}

void VmEsp32Controller::setMode(uint8_t mode) {
    if (_link == nullptr) {
        return;
    }
    uint8_t seq = _link->nextSeq();
    _link->sendSetMode(seq, mode);
}

void VmEsp32Controller::sendHeartbeat() {
    if (_link == nullptr) {
        return;
    }
    uint8_t seq = _link->nextSeq();
    _link->sendHeartbeat(seq);
}

uint32_t VmEsp32Controller::vend(uint8_t channel) {
    if (_link == nullptr) {
        return VM_TX_ID_NULL;
    }
    if (channel < VM_CHANNEL_MIN || channel > VM_CHANNEL_MAX) {
        return VM_TX_ID_NULL;  // Validación defensiva; el Mega también la revalida.
    }
    uint32_t txId = generateTransactionId();
    uint8_t seq = _link->nextSeq();
    _link->sendVend(seq, channel, txId);
    return txId;
}

void VmEsp32Controller::updateDisplay(const char* text1, const char* text2) {
    if (_link == nullptr) {
        return;
    }
    char line1[VM_DISPLAY_LINE_LEN];
    char line2[VM_DISPLAY_LINE_LEN];
    VmUartLink::padDisplayLine(text1, line1);
    VmUartLink::padDisplayLine(text2, line2);
    uint8_t seq = _link->nextSeq();
    _link->sendDisplay(seq, line1, line2);
}

uint32_t VmEsp32Controller::generateTransactionId() {
    // NOTA: contador en RAM, se reinicia a VM_TX_ID_MIN en cada
    // arranque del ESP32. El contrato (2.4) solo exige que el ESP32
    // sea el único generador y que se respete el rango operativo;
    // no exige persistencia entre reinicios. Si el diseño de
    // negocio requiere IDs que sobrevivan a un reinicio (para
    // correlacionar con SQLite tras un corte de energía), sustituir
    // este contador por uno leído/incrementado en la base de datos.
    uint32_t txId = _nextTxId;

    if (_nextTxId >= VM_TX_ID_MAX) {
        _nextTxId = VM_TX_ID_MIN;  // Envuelve evitando los centinelas.
    } else {
        _nextTxId++;
    }
    return txId;
}

void VmEsp32Controller::handleIncomingFrame(uint8_t cmd, uint8_t seq,
                                             const uint8_t* payload, uint8_t len) {
    switch (cmd) {

        case VM_CMD_HELLO: {
            // El Mega inició o respondió un HELLO; contestamos con el
            // mismo SEQ (UART-REQ-004) y marcamos el handshake listo.
            if (len == VM_LEN_HELLO && payload != nullptr) {
                uint8_t megaMajor = payload[0];
                uint8_t megaMinor = payload[1];
                // uint8_t megaRole = payload[2]; // debería ser VM_ROLE_MEGA

                _link->sendHello(seq, VM_ROLE_ESP32);
                _handshakeComplete = true;

                if (_onHandshake != nullptr) {
                    _onHandshake(megaMajor, megaMinor);
                }
            }
            break;
        }

        case VM_CMD_ACK: {
            // Confirmación a algo que el ESP32 envió previamente.
            // UART-REQ-005: un ACK nunca genera otro ACK.
            // TODO (capa superior): si se requiere correlacionar ACKs
            // con comandos pendientes (p. ej. detectar BUSY en
            // SET_MODE o DUPLICATE_CONFLICT en VEND), hacerlo aquí
            // exponiendo un callback adicional (onAck).
            break;
        }

        case VM_CMD_STATUS: {
            // Solo tiene sentido como respuesta (len == 7); una
            // solicitud STATUS (len == 0) no debería llegarle al
            // ESP32, ya que STATUS request es ESP32 -> Mega.
            if (len == VM_LEN_STATUS_RESP && payload != nullptr) {
                uint8_t door = payload[0];
                uint8_t barrier = payload[1];
                uint8_t mode = payload[2];
                uint32_t currentTxId = VmUartLink::decodeU32LE(&payload[3]);

                if (_onStatusUpdate != nullptr) {
                    _onStatusUpdate(door, barrier, mode, currentTxId);
                }
            }
            break;
        }

        case VM_CMD_KEY: {
            if (len == VM_LEN_KEY && payload != nullptr) {
                char keyAscii = (char)payload[0];
                uint8_t keySeq = payload[1];

                // UART-REQ-008: responder ACK de inmediato.
                _link->sendAck(seq, VM_CMD_KEY, VM_ACK_RECEIVED, VM_REASON_NONE);

                if (_onKey != nullptr) {
                    _onKey(keyAscii, keySeq);
                }
            }
            break;
        }

        case VM_CMD_RESULT: {
            if (len == VM_LEN_RESULT && payload != nullptr) {
                uint32_t transactionId = VmUartLink::decodeU32LE(&payload[0]);
                uint8_t result = payload[4];

                // UART-REQ-012: confirmar recepción de RESULT con ACK.
                _link->sendAck(seq, VM_CMD_RESULT, VM_ACK_RECEIVED, VM_REASON_NONE);

                if (_onVendResult != nullptr) {
                    _onVendResult(transactionId, result);
                }
            }
            break;
        }

        case VM_CMD_HEARTBEAT: {
            // Eco de un HEARTBEAT que el ESP32 mismo originó; no
            // requiere acción adicional (podría usarse para medir
            // latencia si se guarda un timestamp por SEQ).
            break;
        }

        default: {
            // El ESP32 no debería recibir SET_MODE, DISPLAY ni VEND
            // (son ESP32 -> Mega), ni ningún CMD fuera del catálogo.
            _link->sendAck(seq, cmd, VM_ACK_REJECTED, VM_REASON_INVALID_CMD);
            break;
        }
    }
}

void VmEsp32Controller::frameTrampoline(uint8_t cmd, uint8_t seq,
                                         const uint8_t* payload, uint8_t len) {
    if (s_instance != nullptr) {
        s_instance->handleIncomingFrame(cmd, seq, payload, len);
    }
}
