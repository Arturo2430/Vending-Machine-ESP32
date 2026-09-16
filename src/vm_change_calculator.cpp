/**
 * @file vm_change_calculator.cpp
 * @brief Implementación del algoritmo voraz de cambio.
 */

#include "vm_change_calculator.h"
#include <Arduino.h>

// Denominaciones en centavos, de mayor a menor (orden greedy).
static const uint32_t DENOMINATIONS[CHANGE_DENOM_COUNT] = {1000, 500, 200, 100};

VmChangeCalculator::VmChangeCalculator() {}

bool VmChangeCalculator::calculate(uint32_t paidCentavos, uint32_t priceCentavos,
                                   VmDatabase& db, ChangeResult& out) {
    out.changeCentavos = 0;
    out.debtCentavos   = 0;
    for (uint8_t i = 0; i < CHANGE_DENOM_COUNT; i++) {
        out.coins[i]         = 0;
        out.denomsCentavos[i] = DENOMINATIONS[i];
    }

    if (paidCentavos < priceCentavos) {
        // No debería ocurrir si la FSM validó el saldo antes de llegar aquí.
        Serial.println("[CHANGE] Error: pago insuficiente.");
        return false;
    }

    out.changeCentavos = paidCentavos - priceCentavos;
    uint32_t remaining = out.changeCentavos;

    // Algoritmo greedy: denominación más alta primero.
    for (uint8_t i = 0; i < CHANGE_DENOM_COUNT && remaining > 0; i++) {
        uint32_t denom = DENOMINATIONS[i];
        if (remaining < denom) continue;

        uint32_t needed = remaining / denom;

        // Limitar al stock disponible en la caja.
        uint32_t available = 0;
        db.getCoinStock(denom, available);
        uint32_t toUse = min(needed, available);

        if (toUse > 0) {
            // Descontar del stock de la caja.
            if (!db.deductCoins(denom, toUse)) {
                Serial.printf("[CHANGE] Error al descontar %lu x $%lu ct\n",
                              (unsigned long)toUse, (unsigned long)denom);
                // Continuar con el resto de denominaciones.
                toUse = 0;
            }
        }

        out.coins[i] = toUse;
        remaining -= toUse * denom;
    }

    out.debtCentavos = remaining; // Adeudo que no se pudo cubrir con monedas.

    if (out.debtCentavos > 0) {
        Serial.printf("[CHANGE] Adeudo de %lu centavos por falta de monedas.\n",
                      (unsigned long)out.debtCentavos);
    }

    return true;
}
