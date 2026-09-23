/**
 * @file vm_fsm.cpp
 * @brief Implementación de la FSM de la Máquina Expendedora SAID.
 *
 * Reglas generales:
 *  - Cero llamadas a delay().  Toda espera usa millis().
 *  - Cada estado tiene un handler onEnter*() que se ejecuta al entrar.
 *  - update() comprueba timers y condiciones periódicas.
 *  - handleKey() delega a processKey*() del estado activo.
 */

#include "vm_fsm.h"
#include <stdio.h>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

static void noopDisplay(const char*, const char*, const char*, const char*) {}

VmFsm::VmFsm(VmDatabase& db, VendFn vendFn, DisplayFn displayFn, SetModeFn setModeFn)
    : _db(db),
      _vendFn(vendFn),
      _displayFn(displayFn),
      _setModeFn(setModeFn),
      _keypad(),
      _carousel(displayFn ? displayFn : noopDisplay),
      _changeCalc(),
      _state(FsmState::S0_ARRANQUE),
      _uartReady(false),
      _selectedSlot(0),
      _insertedCentavos(0),
      _activeTxId(0),
      _dbTxId(0),
      _paymentMethod(0),
      _pinLen(0),
      _pinFailCount(0),
      _pinLockoutEnd(0),
      _adminSlot(0),
      _numLen(0),
      _inactivityTimer(0),
      _motorTimer(0),
      _vendRetryCount(0) {
    memset(_pinBuffer, 0, sizeof(_pinBuffer));
    memset(_numBuffer, 0, sizeof(_numBuffer));
}

// ---------------------------------------------------------------------------
// Inicialización
// ---------------------------------------------------------------------------

void VmFsm::begin() {
    _state = FsmState::S0_ARRANQUE;
    onEnterArranque();
}

// ---------------------------------------------------------------------------
// Bucle principal (no bloqueante)
// ---------------------------------------------------------------------------

void VmFsm::update() {
    _carousel.update();

    switch (_state) {

        case FsmState::S0_ARRANQUE:
            // Si el UART ya está listo (handshake completado), ir a reposo.
            if (_uartReady) {
                enterState(FsmState::S2_REPOSO);
            }
            break;

        case FsmState::S1_FALLA_INTERNA:
            // Estado terminal; requiere reinicio físico.
            break;

        case FsmState::S2_REPOSO:
        case FsmState::S3_SEL_CANAL:
        case FsmState::S4_SEL_PAGO:
        case FsmState::S5_ESP_EFECTIVO:
        case FsmState::S6_ESP_RFID:
            // Timeout de inactividad → cancelar y volver a reposo.
            if (inactivityExpired()) {
                Serial.println("[FSM] Timeout de inactividad → REPOSO");
                display("  Tiempo agotado  ", "  Volviendo...   ", "", "");
                enterState(FsmState::S2_REPOSO);
            }
            break;

        case FsmState::S7_RESERVADA:
            // Reintento de VEND si no llegó ACK a tiempo.
            if ((millis() - _motorTimer) >= VM_UART_RETRY_INTERVAL_MS) {
                if (_vendRetryCount < VM_UART_MAX_RETRIES) {
                    _vendRetryCount++;
                    Serial.printf("[FSM] Reintento VEND #%u\n", _vendRetryCount);
                    _activeTxId = _vendFn(_selectedSlot);
                    _motorTimer = millis();
                } else {
                    Serial.println("[FSM] Max reintentos VEND → FALLA");
                    enterState(FsmState::S10_FALLA_DISP);
                }
            }
            break;

        case FsmState::S8_DISPENSANDO:
            // Timeout de motor → falla de entrega.
            if (motorTimerExpired()) {
                Serial.println("[FSM] Timeout de motor → FALLA_DISP");
                enterState(FsmState::S10_FALLA_DISP);
            }
            break;

        case FsmState::S13_ADMIN_AUTH:
            // Timeout de inactividad en admin.
            if (inactivityExpired()) {
                display(" Sin actividad   ", "  Saliendo...   ", "", "");
                enterState(FsmState::S2_REPOSO);
            }
            break;

        case FsmState::S14_ADMIN_CANAL:
        case FsmState::S15_ADMIN_ACCION:
        case FsmState::S16_MOD_PRECIO:
        case FsmState::S17_MOD_STOCK:
            if (inactivityExpired()) {
                display(" Sin actividad   ", "  Saliendo...   ", "", "");
                _setModeFn(VM_MODE_VENTA);
                enterState(FsmState::S2_REPOSO);
            }
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Eventos externos
// ---------------------------------------------------------------------------

void VmFsm::handleHandshakeComplete() {
    _uartReady = true;
    Serial.println("[FSM] UART listo.");
}

void VmFsm::handleKey(char key) {
    resetInactivityTimer();
    KeyMode mode = KeyMode::REPOSO;

    switch (_state) {
        case FsmState::S2_REPOSO:     mode = KeyMode::REPOSO;       break;
        case FsmState::S3_SEL_CANAL:  mode = KeyMode::PAGO;         break; // reusar PAGO para confirmar/cancelar
        case FsmState::S4_SEL_PAGO:   mode = KeyMode::PAGO;         break;
        case FsmState::S5_ESP_EFECTIVO:mode = KeyMode::EFECTIVO;    break;
        case FsmState::S13_ADMIN_AUTH: mode = KeyMode::ADMIN_PIN;   break;
        case FsmState::S14_ADMIN_CANAL:mode = KeyMode::ADMIN_MENU;  break;
        case FsmState::S15_ADMIN_ACCION:mode = KeyMode::ADMIN_ACCION;break;
        case FsmState::S16_MOD_PRECIO: mode = KeyMode::NUMERICO;    break;
        case FsmState::S17_MOD_STOCK:  mode = KeyMode::NUMERICO;    break;
        default: return;
    }

    KeyAction action = _keypad.interpret(key, mode);
    if (action == KeyAction::NONE) return;

    switch (_state) {
        case FsmState::S2_REPOSO:       processKeyReposo(action);      break;
        case FsmState::S3_SEL_CANAL:    processKeySelCanal(action);    break;
        case FsmState::S4_SEL_PAGO:     processKeySelPago(action);     break;
        case FsmState::S5_ESP_EFECTIVO: processKeyEspEfectivo(action); break;
        case FsmState::S13_ADMIN_AUTH:  processKeyAdminAuth(action);   break;
        case FsmState::S14_ADMIN_CANAL: processKeyAdminCanal(action);  break;
        case FsmState::S15_ADMIN_ACCION:processKeyAdminAccion(action); break;
        case FsmState::S16_MOD_PRECIO:  processKeyModPrecio(action);   break;
        case FsmState::S17_MOD_STOCK:   processKeyModStock(action);    break;
        default: break;
    }
}

void VmFsm::handleVendResult(uint32_t txId, uint8_t result) {
    if (_state != FsmState::S7_RESERVADA && _state != FsmState::S8_DISPENSANDO) {
        Serial.printf("[FSM] RESULT inesperado en estado %u — ignorado.\n",
                      (uint8_t)_state);
        return;
    }

    // Si es duplicado (tx_id distinto), ignorar.
    if (_activeTxId != 0 && txId != _activeTxId) {
        Serial.printf("[FSM] RESULT tx_id=%lu != activo=%lu — ignorado.\n",
                      (unsigned long)txId, (unsigned long)_activeTxId);
        return;
    }

    switch (result) {
        case VM_RESULT_DELIVERED:
            enterState(FsmState::S9_CONFIRMADA);
            break;
        case VM_RESULT_REJECTED_BEFORE_MOTION:
        case VM_RESULT_UNCERTAIN:
        default:
            enterState(FsmState::S10_FALLA_DISP);
            break;
    }
}

void VmFsm::handleRfidCard(const String& uid) {
    if (_state != FsmState::S6_ESP_RFID) return;

    resetInactivityTimer();
    Serial.printf("[FSM] RFID leída: %s\n", uid.c_str());

    CardInfo card;
    if (!_db.checkCard(uid, card)) {
        display("Tarjeta no       ", "  registrada     ", "", "");
        return;
    }
    if (!card.enabled) {
        display("Tarjeta          ", "  desactivada    ", "", "");
        return;
    }
    uint32_t available = card.balanceCentavos - card.reserveCentavos;
    if (available < _slotInfo.priceCentavos) {
        char l2[21];
        snprintf(l2, sizeof(l2), "Saldo: $%lu.%02lu",
                 (unsigned long)(available / 100),
                 (unsigned long)(available % 100));
        display("Saldo insuf.     ", l2, "", "");
        return;
    }

    // Reservar saldo y pasar a dispensar.
    if (!_db.reserveCardBalance(card.cardId, _slotInfo.priceCentavos)) {
        display("Error reserva    ", "  saldo RFID     ", "", "");
        return;
    }

    _activeCard    = card;
    _paymentMethod = 1; // RFID
    enterState(FsmState::S7_RESERVADA);
}

// ---------------------------------------------------------------------------
// Máquina de transiciones
// ---------------------------------------------------------------------------

void VmFsm::enterState(FsmState next) {
    Serial.printf("[FSM] %u → %u\n", (uint8_t)_state, (uint8_t)next);
    _state = next;
    switch (next) {
        case FsmState::S0_ARRANQUE:     onEnterArranque();      break;
        case FsmState::S1_FALLA_INTERNA:                        break; // llamada explícita
        case FsmState::S2_REPOSO:       onEnterReposo();        break;
        case FsmState::S4_SEL_PAGO:     onEnterSelPago();       break;
        case FsmState::S5_ESP_EFECTIVO: onEnterEspEfectivo();   break;
        case FsmState::S6_ESP_RFID:     onEnterEspRfid();       break;
        case FsmState::S7_RESERVADA:    onEnterReservada();     break;
        case FsmState::S8_DISPENSANDO:  onEnterDispensando();   break;
        case FsmState::S9_CONFIRMADA:   onEnterConfirmada();    break;
        case FsmState::S10_FALLA_DISP:  onEnterFallaDisp();     break;
        case FsmState::S11_CALC_CAMBIO: onEnterCalcCambio();    break;
        case FsmState::S12_PANTALLA_FIN:onEnterPantallaFin();   break;
        case FsmState::S13_ADMIN_AUTH:  onEnterAdminAuth();     break;
        case FsmState::S14_ADMIN_CANAL: onEnterAdminCanal();    break;
        case FsmState::S15_ADMIN_ACCION:onEnterAdminAccion();   break;
        case FsmState::S16_MOD_PRECIO:  onEnterModPrecio();     break;
        case FsmState::S17_MOD_STOCK:   onEnterModStock();      break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Handlers onEnter*
// ---------------------------------------------------------------------------

void VmFsm::onEnterArranque() {
    display("  Iniciando...   ", "  Por favor esp. ", "", "");
}

void VmFsm::onEnterFallaInterna(const char* msg) {
    _state = FsmState::S1_FALLA_INTERNA;
    Serial.printf("[FSM] FALLA INTERNA: %s\n", msg);
    display("!! FALLA INTERNA ", msg, "", "");
}

void VmFsm::onEnterReposo() {
    // Limpiar sesión.
    _selectedSlot     = 0;
    _insertedCentavos = 0;
    _activeTxId       = 0;
    _dbTxId           = 0;
    _paymentMethod    = 0;
    _vendRetryCount   = 0;
    memset(&_activeCard,   0, sizeof(_activeCard));
    memset(&_changeResult, 0, sizeof(_changeResult));

    // Asegurarse de que el Mega esté en modo VENTA.
    _setModeFn(VM_MODE_VENTA);

    buildReposoCarousel();
    resetInactivityTimer();
}

void VmFsm::onEnterSelCanal(uint8_t slot) {
    _selectedSlot = slot;
    if (!_db.getSlot(slot, _slotInfo)) {
        display("Canal no disp.   ", "  Intente otro  ", "", "");
        enterState(FsmState::S2_REPOSO);
        return;
    }
    if (_slotInfo.stock == 0) {
        display("Producto agotado ", "  Elija otro    ", "", "");
        enterState(FsmState::S2_REPOSO);
        return;
    }
    char l1[21], l2[17];
    snprintf(l1, sizeof(l1), "%-20s", _slotInfo.productName);
    snprintf(l2, sizeof(l2), "Precio: $%lu.%02lu  ",
             (unsigned long)(_slotInfo.priceCentavos / 100),
             (unsigned long)(_slotInfo.priceCentavos % 100));
    display(l1, l2, "", "");
    _state = FsmState::S3_SEL_CANAL;
    resetInactivityTimer();
}

void VmFsm::onEnterSelPago() {
    display("A=Efectivo       ", "B=Tarjeta  *=Sal", "", "");
    resetInactivityTimer();
}

void VmFsm::onEnterEspEfectivo() {
    _insertedCentavos = 0;
    char l2[21];
    snprintf(l2, sizeof(l2), "Necesita:$%lu.%02lu ",
             (unsigned long)(_slotInfo.priceCentavos / 100),
             (unsigned long)(_slotInfo.priceCentavos % 100));
    display("Inserte dinero   ", l2, "", "");
    resetInactivityTimer();
}

void VmFsm::onEnterEspRfid() {
    display("Acerque tarjeta  ", "  al lector...  ", "", "");
    resetInactivityTimer();
}

void VmFsm::onEnterReservada() {
    // Reservar en BD y enviar VEND.
    const char* metodo = (_paymentMethod == 1) ? "RFID" : "EFECTIVO";
    uint32_t cardId = (_paymentMethod == 1) ? _activeCard.cardId : 0;

    if (!_db.reserveSlot(_selectedSlot, metodo, cardId,
                         _slotInfo.priceCentavos, _slotInfo.productName,
                         _dbTxId)) {
        display("Error al reservar", "  stock BD       ", "", "");
        // Si fue RFID, liberar la reserva de saldo ya hecha.
        if (_paymentMethod == 1) {
            _db.releaseCardBalance(cardId, _slotInfo.priceCentavos);
        }
        enterState(FsmState::S2_REPOSO);
        return;
    }

    display("Procesando...    ", "  Dispensando   ", "", "");
    sendVend();
    enterState(FsmState::S8_DISPENSANDO);
}

void VmFsm::onEnterDispensando() {
    _motorTimer = millis();
    display("Dispensando...   ", "  Por favor esp.", "", "");
}

void VmFsm::onEnterConfirmada() {
    uint32_t cardId = (_paymentMethod == 1) ? _activeCard.cardId : 0;
    _db.confirmSale(_selectedSlot, _dbTxId, cardId, _slotInfo.priceCentavos);

    if (_paymentMethod == 0) {
        // Efectivo: calcular y desglosar cambio.
        enterState(FsmState::S11_CALC_CAMBIO);
    } else {
        // RFID: sin cambio; ir directamente a pantalla fin.
        memset(&_changeResult, 0, sizeof(_changeResult));
        _changeResult.changeCentavos = 0;
        enterState(FsmState::S12_PANTALLA_FIN);
    }
}

void VmFsm::onEnterFallaDisp() {
    uint32_t cardId = (_paymentMethod == 1) ? _activeCard.cardId : 0;
    _db.revertSale(_selectedSlot, _dbTxId, cardId, _slotInfo.priceCentavos);

    display("Error al dispen. ", "Reintente/llame ", "", "");
    // Devolver efectivo insertado (informativo en pantalla; sin tolva).
    if (_paymentMethod == 0 && _insertedCentavos > 0) {
        char l2[21];
        snprintf(l2, sizeof(l2), "Devuelva:$%lu.%02lu",
                 (unsigned long)(_insertedCentavos / 100),
                 (unsigned long)(_insertedCentavos % 100));
        display("Devol. efectivo  ", l2, "", "");
    }
    // Volver a reposo después de 3 s (se implementa con el timer de inactividad).
    _inactivityTimer = millis() - VM_TIMEOUT_INACTIVITY_MS + 3000UL;
}

void VmFsm::onEnterCalcCambio() {
    if (_insertedCentavos > _slotInfo.priceCentavos) {
        // Registrar monedas recibidas en la caja antes de calcular cambio.
        // Simplificación: solo se registra una denominación "$10" o el total.
        // El desglose exacto del pago no se conoce aquí; se usa el total.
        // (El equipo de BD puede expandir esto con historial de inserción).
        _changeCalc.calculate(_insertedCentavos, _slotInfo.priceCentavos,
                              _db, _changeResult);
    } else {
        memset(&_changeResult, 0, sizeof(_changeResult));
    }
    enterState(FsmState::S12_PANTALLA_FIN);
}

void VmFsm::onEnterPantallaFin() {
    buildFinCarousel();
}

void VmFsm::onEnterAdminAuth() {
    // Comprobar bloqueo por intentos fallidos.
    if (_pinLockoutEnd > 0 && millis() < _pinLockoutEnd) {
        uint32_t secsLeft = (_pinLockoutEnd - millis()) / 1000UL;
        char l2[21];
        snprintf(l2, sizeof(l2), "Espere %lus      ", (unsigned long)secsLeft);
        display("Bloqueado        ", l2, "", "");
        enterState(FsmState::S2_REPOSO);
        return;
    }
    _pinLen = 0;
    memset(_pinBuffer, 0, sizeof(_pinBuffer));
    display("PIN Admin:       ", "                ", "", "");
    resetInactivityTimer();

    // Solicitar modo mantenimiento al Mega para detener actuadores.
    _setModeFn(VM_MODE_MANTENIMIENTO);
}

void VmFsm::onEnterAdminCanal() {
    _carousel.clear();
    _carousel.addSlide("Seleccione canal ", "1-4  o 5=Salir  ");

    // Mostrar stock de cada canal.
    for (uint8_t s = 1; s <= 4; s++) {
        SlotInfo si;
        if (_db.getSlot(s, si)) {
            char l1[21], l2[17];
            snprintf(l1, sizeof(l1), "Canal %u: %-8s", s, si.productName);
            snprintf(l2, sizeof(l2), "Stock:%lu/%lu Px$%lu",
                     (unsigned long)si.stock,
                     (unsigned long)si.capacity,
                     (unsigned long)(si.priceCentavos / 100));
            _carousel.addSlide(l1, l2);
        }
    }
    _carousel.start();
    resetInactivityTimer();
}

void VmFsm::onEnterAdminAccion() {
    char l1[21];
    snprintf(l1, sizeof(l1), "Canal %u          ", _adminSlot);
    display(l1, "1=Precio 2=Stock ", "", "");
    resetInactivityTimer();
}

void VmFsm::onEnterModPrecio() {
    clearNumBuffer();
    SlotInfo si;
    char l2[21];
    if (_db.getSlot(_adminSlot, si)) {
        snprintf(l2, sizeof(l2), "Actual:$%lu.%02lu   ",
                 (unsigned long)(si.priceCentavos / 100),
                 (unsigned long)(si.priceCentavos % 100));
    } else {
        strncpy(l2, "               ", sizeof(l2));
    }
    display("Nuevo precio:    ", l2, "", "");
    resetInactivityTimer();
}

void VmFsm::onEnterModStock() {
    clearNumBuffer();
    SlotInfo si;
    char l2[21];
    if (_db.getSlot(_adminSlot, si)) {
        snprintf(l2, sizeof(l2), "Actual: %lu/%lu     ",
                 (unsigned long)si.stock,
                 (unsigned long)si.capacity);
    } else {
        strncpy(l2, "               ", sizeof(l2));
    }
    display("Stock total:     ", l2, "", "");
    resetInactivityTimer();
}

// ---------------------------------------------------------------------------
// Procesadores de tecla por estado
// ---------------------------------------------------------------------------

void VmFsm::processKeyReposo(KeyAction action) {
    switch (action) {
        case KeyAction::SELECT_CHANNEL_1: onEnterSelCanal(1); break;
        case KeyAction::SELECT_CHANNEL_2: onEnterSelCanal(2); break;
        case KeyAction::SELECT_CHANNEL_3: onEnterSelCanal(3); break;
        case KeyAction::SELECT_CHANNEL_4: onEnterSelCanal(4); break;
        case KeyAction::ENTER_ADMIN:      enterState(FsmState::S13_ADMIN_AUTH); break;
        default: break;
    }
}

void VmFsm::processKeySelCanal(KeyAction action) {
    switch (action) {
        case KeyAction::CONFIRM:  enterState(FsmState::S4_SEL_PAGO); break;
        case KeyAction::CANCEL:   enterState(FsmState::S2_REPOSO);   break;
        // Permitir cambiar de canal sin volver a reposo.
        case KeyAction::SELECT_CHANNEL_1: onEnterSelCanal(1); break;
        case KeyAction::SELECT_CHANNEL_2: onEnterSelCanal(2); break;
        case KeyAction::SELECT_CHANNEL_3: onEnterSelCanal(3); break;
        case KeyAction::SELECT_CHANNEL_4: onEnterSelCanal(4); break;
        default: break;
    }
}

void VmFsm::processKeySelPago(KeyAction action) {
    switch (action) {
        case KeyAction::CHOOSE_CASH: enterState(FsmState::S5_ESP_EFECTIVO); break;
        case KeyAction::CHOOSE_RFID: enterState(FsmState::S6_ESP_RFID);     break;
        case KeyAction::CANCEL:      enterState(FsmState::S2_REPOSO);       break;
        default: break;
    }
}

void VmFsm::processKeyEspEfectivo(KeyAction action) {
    if (action == KeyAction::CANCEL) {
        // Devolver dinero ya insertado (informativo).
        if (_insertedCentavos > 0) {
            char l2[21];
            snprintf(l2, sizeof(l2), "Devuelva:$%lu.%02lu",
                     (unsigned long)(_insertedCentavos / 100),
                     (unsigned long)(_insertedCentavos % 100));
            display("Cancelado        ", l2, "", "");
        }
        enterState(FsmState::S2_REPOSO);
        return;
    }

    uint32_t coinValue = VmKeypad::coinActionToCentavos(action);
    if (coinValue == 0) return;

    _insertedCentavos += coinValue;

    // Registrar moneda en caja (para que esté disponible como cambio).
    if (coinValue <= 1000) { // Solo monedas (≤$10) van al algoritmo de cambio.
        _db.addCoins(coinValue, 1);
    }

    char l1[21], l2[17];
    snprintf(l1, sizeof(l1), "Insertado:$%lu.%02lu",
             (unsigned long)(_insertedCentavos / 100),
             (unsigned long)(_insertedCentavos % 100));
    int32_t falta = (int32_t)_slotInfo.priceCentavos - (int32_t)_insertedCentavos;
    if (falta > 0) {
        snprintf(l2, sizeof(l2), "Faltan: $%ld.%02ld  ",
                 (long)(falta / 100), (long)(falta % 100));
        display(l1, l2, "", "");
    } else {
        // Suficiente dinero insertado.
        enterState(FsmState::S7_RESERVADA);
    }
}

void VmFsm::processKeyAdminAuth(KeyAction action) {
    if (action >= KeyAction::DIGIT_0 && action <= KeyAction::DIGIT_9) {
        if (_pinLen >= 4) return; // MAX 4 dígitos.
        uint8_t digit = (uint8_t)action - (uint8_t)KeyAction::DIGIT_0;
        _pinBuffer[_pinLen++] = '0' + digit;
        _pinBuffer[_pinLen]   = '\0';
        // Mostrar asteriscos.
        char mask[21] = "PIN: ";
        for (uint8_t i = 0; i < _pinLen; i++) mask[5 + i] = '*';
        mask[5 + _pinLen] = '\0';
        display(mask, "A=OK  B=Cancelar", "", "");
        return;
    }
    if (action == KeyAction::BACKSPACE && _pinLen > 0) {
        _pinBuffer[--_pinLen] = '\0';
        return;
    }
    if (action == KeyAction::CANCEL) {
        _setModeFn(VM_MODE_VENTA);
        enterState(FsmState::S2_REPOSO);
        return;
    }
    if (action == KeyAction::CONFIRM) {
        if (_db.verifyAdminPin(_pinBuffer)) {
            _pinFailCount  = 0;
            _pinLockoutEnd = 0;
            enterState(FsmState::S14_ADMIN_CANAL);
        } else {
            _pinFailCount++;
            Serial.printf("[FSM] PIN incorrecto (intento %u)\n", _pinFailCount);
            if (_pinFailCount >= 3) {
                _pinLockoutEnd = millis() + VM_PIN_LOCKOUT_MS;
                _pinFailCount  = 0;
                display("Bloqueado 30s    ", "Demasiados err. ", "", "");
                _setModeFn(VM_MODE_VENTA);
                enterState(FsmState::S2_REPOSO);
            } else {
                display("PIN incorrecto   ", "Intente de nuevo", "", "");
                _pinLen = 0;
                memset(_pinBuffer, 0, sizeof(_pinBuffer));
            }
        }
    }
}

void VmFsm::processKeyAdminCanal(KeyAction action) {
    switch (action) {
        case KeyAction::SELECT_CHANNEL_1: _adminSlot = 1; enterState(FsmState::S15_ADMIN_ACCION); break;
        case KeyAction::SELECT_CHANNEL_2: _adminSlot = 2; enterState(FsmState::S15_ADMIN_ACCION); break;
        case KeyAction::SELECT_CHANNEL_3: _adminSlot = 3; enterState(FsmState::S15_ADMIN_ACCION); break;
        case KeyAction::SELECT_CHANNEL_4: _adminSlot = 4; enterState(FsmState::S15_ADMIN_ACCION); break;
        case KeyAction::EXIT_ADMIN:
            _setModeFn(VM_MODE_VENTA);
            enterState(FsmState::S2_REPOSO);
            break;
        default: break;
    }
}

void VmFsm::processKeyAdminAccion(KeyAction action) {
    switch (action) {
        case KeyAction::ACTION_PRICE: enterState(FsmState::S16_MOD_PRECIO); break;
        case KeyAction::ACTION_STOCK: enterState(FsmState::S17_MOD_STOCK);  break;
        case KeyAction::CANCEL:       enterState(FsmState::S14_ADMIN_CANAL);break;
        default: break;
    }
}

void VmFsm::processKeyModPrecio(KeyAction action) {
    if (action >= KeyAction::DIGIT_0 && action <= KeyAction::DIGIT_9) {
        appendNumBuffer((uint8_t)action - (uint8_t)KeyAction::DIGIT_0);
        char l2[21];
        snprintf(l2, sizeof(l2), "$%s             ", _numBuffer);
        display("Nuevo precio:    ", l2, "", "");
        return;
    }
    if (action == KeyAction::BACKSPACE) { backspaceNumBuffer(); return; }
    if (action == KeyAction::CANCEL)    { enterState(FsmState::S15_ADMIN_ACCION); return; }
    if (action == KeyAction::CONFIRM) {
        uint32_t pesos = numBufferValue();
        uint32_t centavos = pesos * 100;
        if (pesos == 0) { display("Precio invalido  ", "               ", "", ""); return; }
        if (_db.updateSlotPrice(_adminSlot, centavos)) {
            display("Precio guardado  ", "               ", "", "");
        } else {
            display("Error al guardar ", "               ", "", "");
        }
        enterState(FsmState::S14_ADMIN_CANAL);
    }
}

void VmFsm::processKeyModStock(KeyAction action) {
    if (action >= KeyAction::DIGIT_0 && action <= KeyAction::DIGIT_9) {
        appendNumBuffer((uint8_t)action - (uint8_t)KeyAction::DIGIT_0);
        char l2[21];
        snprintf(l2, sizeof(l2), "Stock: %s       ", _numBuffer);
        display("Stock total:     ", l2, "", "");
        return;
    }
    if (action == KeyAction::BACKSPACE) { backspaceNumBuffer(); return; }
    if (action == KeyAction::CANCEL)    { enterState(FsmState::S15_ADMIN_ACCION); return; }
    if (action == KeyAction::CONFIRM) {
        uint32_t newStock = numBufferValue();
        if (_db.updateSlotStock(_adminSlot, newStock)) {
            display("Stock guardado   ", "               ", "", "");
        } else {
            display("Error al guardar ", "(cap. excedida?)", "", "");
        }
        enterState(FsmState::S14_ADMIN_CANAL);
    }
}

// ---------------------------------------------------------------------------
// Helpers privados
// ---------------------------------------------------------------------------

void VmFsm::resetInactivityTimer() {
    _inactivityTimer = millis();
}

bool VmFsm::inactivityExpired() const {
    return (millis() - _inactivityTimer) >= VM_TIMEOUT_INACTIVITY_MS;
}

bool VmFsm::motorTimerExpired() const {
    return (millis() - _motorTimer) >= VM_TIMEOUT_MOTOR_MS;
}

void VmFsm::display(const char* l1, const char* l2, const char* l3, const char* l4) {
    if (_displayFn) _displayFn(l1, l2, l3, l4);
}

bool VmFsm::sendVend() {
    _vendRetryCount = 0;
    _activeTxId = _vendFn(_selectedSlot);
    _motorTimer = millis();
    return (_activeTxId != VM_TX_ID_NULL);
}

void VmFsm::buildReposoCarousel() {
    _carousel.clear();
    char line1[21], line2[21];
    snprintf(line1, sizeof(line1), "%-20s", "  SAID VENDING  ");
    snprintf(line2, sizeof(line2), "%-20s", " Selecc. un canal ");
    _carousel.addSlide(line1, line2, "                    ", "                    ");

    for (uint8_t slot = 1; slot <= 4; slot++) {
        SlotInfo info;
        if (_db.getSlot(slot, info) && info.enabled) {
            snprintf(line1, sizeof(line1), "Ch%d: %s", slot, info.productName);
            char priceStr[21];
            snprintf(priceStr, sizeof(priceStr), "$%d.%02d", info.priceCentavos / 100, info.priceCentavos % 100);
            if (info.stock == 0) {
                snprintf(line2, sizeof(line2), "%-9s [AGOTADO]", priceStr);
            } else {
                snprintf(line2, sizeof(line2), "%-9s Stock:%02d", priceStr, info.stock);
            }
            _carousel.addSlide(line1, line2, "                    ", "                    ");
        }
    }
}

void VmFsm::buildFinCarousel() {
    _carousel.clear();
    _carousel.addSlide("Gracias por su  ", "  compra!       ");

    if (_changeResult.changeCentavos > 0) {
        char l2[21];
        snprintf(l2, sizeof(l2), "Cambio:$%lu.%02lu  ",
                 (unsigned long)(_changeResult.changeCentavos / 100),
                 (unsigned long)(_changeResult.changeCentavos % 100));
        _carousel.addSlide("Su cambio es:   ", l2);

        // Desglose por denominación.
        const char* labels[] = {"$10: ", "$5:  ", "$2:  ", "$1:  "};
        for (uint8_t i = 0; i < CHANGE_DENOM_COUNT; i++) {
            if (_changeResult.coins[i] > 0) {
                char l1[21], l2b[17];
                snprintf(l1, sizeof(l1), "%-20s", labels[i]);
                snprintf(l2b, sizeof(l2b), "  %lu moneda(s)  ",
                         (unsigned long)_changeResult.coins[i]);
                _carousel.addSlide(l1, l2b);
            }
        }

        if (_changeResult.debtCentavos > 0) {
            char ld[21];
            snprintf(ld, sizeof(ld), "Adeudo:$%lu.%02lu  ",
                     (unsigned long)(_changeResult.debtCentavos / 100),
                     (unsigned long)(_changeResult.debtCentavos % 100));
            _carousel.addSlide("Sin cambio suf.  ", ld);
        }
    } else if (_paymentMethod == 1) {
        char l2[21];
        uint32_t newBal = _activeCard.balanceCentavos - _slotInfo.priceCentavos;
        snprintf(l2, sizeof(l2), "Saldo:$%lu.%02lu    ",
                 (unsigned long)(newBal / 100),
                 (unsigned long)(newBal % 100));
        _carousel.addSlide("Cobrado RFID     ", l2);
    }

    _carousel.start();

    // Programar regreso a reposo cuando el carrusel haya rodado (timer de inactividad corto).
    _inactivityTimer = millis() - VM_TIMEOUT_INACTIVITY_MS +
                       (unsigned long)(_carousel.count()) * VM_CAROUSEL_INTERVAL_MS + 1000UL;
}

uint32_t VmFsm::numBufferValue() const {
    uint32_t v = 0;
    for (uint8_t i = 0; i < _numLen; i++) {
        v = v * 10 + (_numBuffer[i] - '0');
    }
    return v;
}

void VmFsm::appendNumBuffer(uint8_t digit) {
    if (_numLen >= 6) return; // Máximo 6 dígitos (precio/stock razonable).
    _numBuffer[_numLen++] = '0' + digit;
    _numBuffer[_numLen]   = '\0';
}

void VmFsm::backspaceNumBuffer() {
    if (_numLen > 0) {
        _numBuffer[--_numLen] = '\0';
    }
}

void VmFsm::clearNumBuffer() {
    _numLen = 0;
    memset(_numBuffer, 0, sizeof(_numBuffer));
}
