/**
 * @file vm_fsm.h
 * @brief Máquina de Estados Finita (FSM) de la Máquina Expendedora SAID.
 *
 * Implementa los estados S0–S17 de la especificación SAID-SPEC-FSM v1.3.0.
 * El método update() debe llamarse en cada iteración de loop(); nunca bloquea.
 *
 * Estados:
 *   S0  ARRANQUE         : Boot y verificación de BD.
 *   S1  FALLA_INTERNA    : Error irrecuperable; muestra mensaje y no avanza.
 *   S2  REPOSO           : Carrusel de productos.  Espera selección de canal.
 *   S3  SEL_CANAL        : Canal seleccionado; muestra precio y stock.
 *   S4  SEL_PAGO         : Elige método de pago: efectivo (A) o RFID (B).
 *   S5  ESP_EFECTIVO     : Ingreso de monedas/billetes hasta cubrir el precio.
 *   S6  ESP_RFID         : Espera lectura de tarjeta RFID.
 *   S7  RESERVADA        : Stock/saldo reservado; envía VEND al Mega.
 *   S8  DISPENSANDO      : Esperando RESULT del Mega.
 *   S9  CONFIRMADA       : RESULT=DELIVERED; consolida venta en BD.
 *   S10 FALLA_DISP       : RESULT≠DELIVERED o timeout motor; revierte reserva.
 *   S11 CALC_CAMBIO      : Calcula y descuenta cambio de la caja.
 *   S12 PANTALLA_FIN     : Carrusel de resumen (cambio, desglose, gracias).
 *   S13 ADMIN_AUTH       : Ingreso de PIN de administrador.
 *   S14 ADMIN_CANAL      : Selección de canal para mantenimiento.
 *   S15 ADMIN_ACCION     : Selección de acción sobre el canal.
 *   S16 MOD_PRECIO       : Captura e ingresa el nuevo precio.
 *   S17 MOD_STOCK        : Captura el nuevo stock total del canal.
 */

#ifndef VM_FSM_H
#define VM_FSM_H

#include <Arduino.h>
#include "vm_database.h"
#include "vm_keypad.h"
#include "vm_carousel.h"
#include "vm_change_calculator.h"
#include "vm_uart_protocol.h"
#include "vm_board_config.h"

// Puntero a función para enviar comandos al Mega (abstrae el controller).
typedef uint32_t (*VendFn)(uint8_t channel);
typedef void     (*DisplayFn)(const char* l1, const char* l2, const char* l3, const char* l4);
typedef void     (*SetModeFn)(uint8_t mode);

// ---------------------------------------------------------------------------
// Enumeración de estados
// ---------------------------------------------------------------------------

enum class FsmState : uint8_t {
    S0_ARRANQUE,
    S1_FALLA_INTERNA,
    S2_REPOSO,
    S3_SEL_CANAL,
    S4_SEL_PAGO,
    S5_ESP_EFECTIVO,
    S6_ESP_RFID,
    S7_RESERVADA,
    S8_DISPENSANDO,
    S9_CONFIRMADA,
    S10_FALLA_DISP,
    S11_CALC_CAMBIO,
    S12_PANTALLA_FIN,
    S13_ADMIN_AUTH,
    S14_ADMIN_CANAL,
    S15_ADMIN_ACCION,
    S16_MOD_PRECIO,
    S17_MOD_STOCK,
};

// ---------------------------------------------------------------------------
// Clase FSM
// ---------------------------------------------------------------------------

class VmFsm {
public:
    VmFsm(VmDatabase& db, VendFn vendFn, DisplayFn displayFn, SetModeFn setModeFn);

    /** Debe llamarse en setup() después de que el handshake UART esté completo. */
    void begin();

    /** Punto de entrada del ciclo no bloqueante.  Llamar en cada loop(). */
    void update();

    // ---- Eventos externos -------------------------------------------------

    /** Tecla recibida desde el Mega por UART. */
    void handleKey(char key);

    /** RESULT de VEND recibido desde el Mega. */
    void handleVendResult(uint32_t txId, uint8_t result);

    /** Handshake UART completado: la FSM puede iniciar. */
    void handleHandshakeComplete();

    /** UID de tarjeta RFID leída (v2.1: recibida desde UART del Mega). */
    void handleRfidCard(const String& uid);

    /** Estado actual (para diagnóstico). */
    FsmState currentState() const { return _state; }

private:
    // ---- Dependencias inyectadas ----------------------------------------
    VmDatabase&        _db;
    VendFn             _vendFn;
    DisplayFn          _displayFn;
    SetModeFn          _setModeFn;

    // ---- Módulos auxiliares ---------------------------------------------
    VmKeypad           _keypad;
    VmCarousel         _carousel;
    VmChangeCalculator _changeCalc;

    // ---- Estado de la sesión de compra ----------------------------------
    FsmState  _state;
    bool      _uartReady;

    uint8_t   _selectedSlot;         // Canal elegido (1–4).
    SlotInfo  _slotInfo;             // Snapshot del slot al seleccionarlo.
    uint32_t  _insertedCentavos;     // Efectivo acumulado por el cliente.
    uint32_t  _activeTxId;           // ID de transacción UART activa.
    uint32_t  _dbTxId;               // ID de transacción en BD.
    uint8_t   _paymentMethod;        // 0=efectivo, 1=RFID.
    CardInfo  _activeCard;           // Datos de la tarjeta RFID presentada.
    ChangeResult _changeResult;      // Resultado del cálculo de cambio.

    // ---- Estado de la sesión de administración ---------------------------
    char      _pinBuffer[5];         // 4 dígitos + terminador nulo.
    uint8_t   _pinLen;
    uint8_t   _pinFailCount;
    unsigned long _pinLockoutEnd;    // millis() al que termina el bloqueo.
    uint8_t   _adminSlot;            // Canal seleccionado en menú admin.
    char      _numBuffer[8];         // Captura numérica de precio/stock.
    uint8_t   _numLen;

    // ---- Temporizadores -------------------------------------------------
    unsigned long _inactivityTimer;  // millis() del último evento.
    unsigned long _motorTimer;       // millis() al enviar VEND.
    uint8_t       _vendRetryCount;

    // ---- Transiciones a estados -----------------------------------------
    void enterState(FsmState next);

    // ---- Handlers por estado (llamados desde update/handleKey) ----------
    void onEnterArranque();
    void onEnterReposo();
    void onEnterFallaInterna(const char* msg);
    void onEnterSelCanal(uint8_t slot);
    void onEnterSelPago();
    void onEnterEspEfectivo();
    void onEnterEspRfid();
    void onEnterReservada();
    void onEnterDispensando();
    void onEnterConfirmada();
    void onEnterFallaDisp();
    void onEnterCalcCambio();
    void onEnterPantallaFin();
    void onEnterAdminAuth();
    void onEnterAdminCanal();
    void onEnterAdminAccion();
    void onEnterModPrecio();
    void onEnterModStock();

    // ---- Lógica de teclas por estado ------------------------------------
    void processKeyReposo(KeyAction action);
    void processKeySelCanal(KeyAction action);
    void processKeySelPago(KeyAction action);
    void processKeyEspEfectivo(KeyAction action);
    void processKeyAdminAuth(KeyAction action);
    void processKeyAdminCanal(KeyAction action);
    void processKeyAdminAccion(KeyAction action);
    void processKeyModPrecio(KeyAction action);
    void processKeyModStock(KeyAction action);

    // ---- Helpers --------------------------------------------------------
    void resetInactivityTimer();
    bool inactivityExpired() const;
    bool motorTimerExpired() const;
    void display(const char* l1, const char* l2, const char* l3, const char* l4);
    void buildReposoCarousel();
    void buildFinCarousel();
    uint32_t numBufferValue() const;
    void appendNumBuffer(uint8_t digit);
    void backspaceNumBuffer();
    void clearNumBuffer();
    bool sendVend();             // Envía VEND con reintentos.
};

#endif // VM_FSM_H
