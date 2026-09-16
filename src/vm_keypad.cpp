/**
 * @file vm_keypad.cpp
 * @brief Lógica de interpretación contextual de teclas del teclado 4×4.
 */

#include "vm_keypad.h"

VmKeypad::VmKeypad() {}

KeyAction VmKeypad::interpret(char key, KeyMode mode) const {
    switch (mode) {

        // ------------------------------------------------------------------
        case KeyMode::REPOSO:
            if (key == '1') return KeyAction::SELECT_CHANNEL_1;
            if (key == '2') return KeyAction::SELECT_CHANNEL_2;
            if (key == '3') return KeyAction::SELECT_CHANNEL_3;
            if (key == '4') return KeyAction::SELECT_CHANNEL_4;
            if (key == 'D') return KeyAction::ENTER_ADMIN;
            return KeyAction::NONE;

        // ------------------------------------------------------------------
        case KeyMode::PAGO:
            if (key == 'A') return KeyAction::CHOOSE_CASH;
            if (key == 'B') return KeyAction::CHOOSE_RFID;
            if (key == '*') return KeyAction::CANCEL;
            return KeyAction::NONE;

        // ------------------------------------------------------------------
        // Mapa de denominaciones de pesos mexicanos para ingreso por teclado.
        // La relación tecla→denominación está acordada con el equipo de Arduino.
        case KeyMode::EFECTIVO:
            if (key == '1') return KeyAction::INSERT_COIN_100;   // $1
            if (key == '2') return KeyAction::INSERT_COIN_200;   // $2
            if (key == '3') return KeyAction::INSERT_COIN_500;   // $5
            if (key == '4') return KeyAction::INSERT_COIN_1000;  // $10
            if (key == '5') return KeyAction::INSERT_COIN_2000;  // $20 moneda
            if (key == '6') return KeyAction::INSERT_BILL_5000;  // $50 billete
            if (key == '7') return KeyAction::INSERT_BILL_10000; // $100
            if (key == '8') return KeyAction::INSERT_BILL_20000; // $200
            if (key == '9') return KeyAction::INSERT_BILL_50000; // $500
            if (key == 'B') return KeyAction::CANCEL;
            return KeyAction::NONE;

        // ------------------------------------------------------------------
        case KeyMode::ADMIN_PIN:
            if (key >= '0' && key <= '9') {
                return static_cast<KeyAction>(
                    static_cast<uint8_t>(KeyAction::DIGIT_0) + (key - '0'));
            }
            if (key == 'C') return KeyAction::BACKSPACE;
            if (key == 'A') return KeyAction::CONFIRM;
            if (key == 'B') return KeyAction::CANCEL;
            return KeyAction::NONE;

        // ------------------------------------------------------------------
        case KeyMode::ADMIN_MENU:
            if (key == '1') return KeyAction::SELECT_CHANNEL_1;
            if (key == '2') return KeyAction::SELECT_CHANNEL_2;
            if (key == '3') return KeyAction::SELECT_CHANNEL_3;
            if (key == '4') return KeyAction::SELECT_CHANNEL_4;
            if (key == '5') return KeyAction::EXIT_ADMIN;
            if (key == 'B') return KeyAction::EXIT_ADMIN;
            return KeyAction::NONE;

        // ------------------------------------------------------------------
        case KeyMode::ADMIN_ACCION:
            if (key == '1') return KeyAction::ACTION_PRICE;
            if (key == '2') return KeyAction::ACTION_STOCK;
            if (key == 'B') return KeyAction::CANCEL;
            return KeyAction::NONE;

        // ------------------------------------------------------------------
        case KeyMode::NUMERICO:
            if (key >= '0' && key <= '9') {
                return static_cast<KeyAction>(
                    static_cast<uint8_t>(KeyAction::DIGIT_0) + (key - '0'));
            }
            if (key == 'C') return KeyAction::BACKSPACE;
            if (key == 'A') return KeyAction::CONFIRM;
            if (key == 'B') return KeyAction::CANCEL;
            if (key == '#') return KeyAction::CONFIRM;
            return KeyAction::NONE;

        default:
            return KeyAction::NONE;
    }
}

uint32_t VmKeypad::coinActionToCentavos(KeyAction action) {
    switch (action) {
        case KeyAction::INSERT_COIN_100:   return 100;
        case KeyAction::INSERT_COIN_200:   return 200;
        case KeyAction::INSERT_COIN_500:   return 500;
        case KeyAction::INSERT_COIN_1000:  return 1000;
        case KeyAction::INSERT_COIN_2000:  return 2000;
        case KeyAction::INSERT_BILL_5000:  return 5000;
        case KeyAction::INSERT_BILL_10000: return 10000;
        case KeyAction::INSERT_BILL_20000: return 20000;
        case KeyAction::INSERT_BILL_50000: return 50000;
        default:                           return 0;
    }
}
