/**
 * @file vm_keypad.h
 * @brief Interpretación contextual de teclas del teclado matricial 4×4.
 *
 * El teclado físico lo lee el Arduino Mega y lo envía al ESP32 vía UART
 * como una trama KEY(key_ascii, key_seq).  Esta clase recibe el carácter
 * ASCII ya decodificado y lo traduce a una acción semántica según el modo
 * de operación actual de la FSM.
 *
 * Mapa físico del teclado 4×4 (layout estándar telefónico):
 *   1  2  3  A
 *   4  5  6  B
 *   7  8  9  C
 *   *  0  #  D
 *
 * Rol de cada tecla por modo:
 *   REPOSO     : '1'–'4' = elegir canal.  'D' = entrar a admin.
 *   PAGO        : 'A' = efectivo.  'B' = RFID.  '*' o 'B' = cancelar.
 *   EFECTIVO   : '1'–'9' = denominaciones.  'B' = cancelar.
 *   ADMIN_PIN  : '0'–'9' = dígito PIN.  'C' = backspace.  'A' = confirmar.  'B' = salir.
 *   ADMIN_MENU : '1'–'4' = selección canal.  '5' = salir.
 *   ADMIN_ACCION: '1' = mod precio.  '2' = mod stock.  'B' = volver.
 *   NUMERICO   : '0'–'9' = dígito.  'C' = backspace.  'A' = confirmar.  'B' = cancelar.
 */

#ifndef VM_KEYPAD_H
#define VM_KEYPAD_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// Tipos
// ---------------------------------------------------------------------------

enum class KeyMode : uint8_t {
    REPOSO,        ///< Canal 1–4 + D para admin.
    PAGO,          ///< A=efectivo, B=RFID, * o B=cancelar.
    EFECTIVO,      ///< Denominaciones 1–9, B=cancelar.
    ADMIN_PIN,     ///< Captura dígitos de PIN.
    ADMIN_MENU,    ///< Selección de canal o salir.
    ADMIN_ACCION,  ///< Elegir acción sobre el canal.
    NUMERICO,      ///< Captura numérica genérica (precio / stock).
};

enum class KeyAction : uint8_t {
    NONE,             ///< Tecla sin efecto en el modo actual.
    SELECT_CHANNEL_1,
    SELECT_CHANNEL_2,
    SELECT_CHANNEL_3,
    SELECT_CHANNEL_4,
    CHOOSE_CASH,      ///< Elegir pago en efectivo.
    CHOOSE_RFID,      ///< Elegir pago con tarjeta RFID.
    INSERT_COIN_100,  ///< $1.00
    INSERT_COIN_200,  ///< $2.00
    INSERT_COIN_500,  ///< $5.00
    INSERT_COIN_1000, ///< $10.00
    INSERT_COIN_2000, ///< $20.00  (moneda)
    INSERT_BILL_5000, ///< $50.00  (billete, sólo informativo)
    INSERT_BILL_10000,///< $100.00
    INSERT_BILL_20000,///< $200.00
    INSERT_BILL_50000,///< $500.00
    DIGIT_0, DIGIT_1, DIGIT_2, DIGIT_3, DIGIT_4,
    DIGIT_5, DIGIT_6, DIGIT_7, DIGIT_8, DIGIT_9,
    CONFIRM,          ///< Tecla A → aceptar / confirmar.
    BACKSPACE,        ///< Tecla C → borrar último dígito.
    CANCEL,           ///< Tecla B → cancelar / salir.
    ENTER_ADMIN,      ///< Tecla D en reposo → solicitar admin.
    ACTION_PRICE,     ///< Acción 1 en admin: modificar precio.
    ACTION_STOCK,     ///< Acción 2 en admin: modificar stock.
    EXIT_ADMIN,       ///< Salir del menú admin.
};

// ---------------------------------------------------------------------------
// Clase
// ---------------------------------------------------------------------------

class VmKeypad {
public:
    VmKeypad();

    /** Interpreta una tecla ASCII en el modo actual y devuelve la acción. */
    KeyAction interpret(char key, KeyMode mode) const;

    /**
     * @brief Convierte una acción INSERT_COIN_xxx al valor en centavos.
     * @return 0 si la acción no es una denominación.
     */
    static uint32_t coinActionToCentavos(KeyAction action);
};

#endif // VM_KEYPAD_H
