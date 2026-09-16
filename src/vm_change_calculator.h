/**
 * @file vm_change_calculator.h
 * @brief Algoritmo voraz (greedy) de cálculo de cambio por denominaciones.
 *
 * Denominaciones operativas (sin tolva física):
 *   $10 = 1000 centavos
 *   $5  = 500  centavos
 *   $2  = 200  centavos
 *   $1  = 100  centavos
 *
 * El algoritmo consulta el stock de monedas en la caja y lo descuenta.
 * Si no hay suficiente cambio en alguna denominación, ajusta a lo disponible
 * y lleva el remanente al monto de adeudo final reportado en pantalla.
 */

#ifndef VM_CHANGE_CALCULATOR_H
#define VM_CHANGE_CALCULATOR_H

#include <stdint.h>
#include "vm_database.h"

// Número de denominaciones que maneja el algoritmo (sólo monedas).
static constexpr uint8_t CHANGE_DENOM_COUNT = 4;

struct ChangeResult {
    uint32_t coins[CHANGE_DENOM_COUNT]; ///< Monedas a entregar por denominación [0..3] → $10,$5,$2,$1.
    uint32_t denomsCentavos[CHANGE_DENOM_COUNT]; ///< Valor de cada denominación en centavos.
    uint32_t debtCentavos;             ///< Monto que no se pudo dar en monedas (adeudo).
    uint32_t changeCentavos;           ///< Cambio total calculado (total - precio).
};

class VmChangeCalculator {
public:
    VmChangeCalculator();

    /**
     * @brief Calcula y aplica el cambio.
     *
     * Descuenta las monedas utilizadas de la caja de efectivo (VmDatabase).
     * Registra en caja las monedas recibidas como pago (método addCoins).
     *
     * @param paidCentavos    Monto ingresado por el cliente.
     * @param priceCentavos   Precio del producto.
     * @param db              Referencia al repositorio de base de datos.
     * @param out             Resultado desglosado por denominación.
     * @return true si el cálculo y los descuentos en BD se realizaron.
     */
    bool calculate(uint32_t paidCentavos, uint32_t priceCentavos,
                   VmDatabase& db, ChangeResult& out);
};

#endif // VM_CHANGE_CALCULATOR_H
