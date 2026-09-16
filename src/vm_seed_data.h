/**
 * @file vm_seed_data.h
 * @brief Esquema DDL y datos semilla que el ESP32 aplica automáticamente cuando
 *        la base de datos no existe o está vacía al arrancar.
 *
 * El equipo de Base de Datos entregará el esquema SQLite oficial; este módulo
 * actúa como fallback de autoinicialización para que el firmware funcione de
 * forma autónoma sin depender de pasos manuales de preparación.
 *
 * @note Todas las cadenas SQL se almacenan en Flash (PROGMEM) para no consumir
 *       heap de RAM en tiempo de ejecución.
 */

#ifndef VM_SEED_DATA_H
#define VM_SEED_DATA_H

#include <sqlite3.h>

namespace VmSeedData {
    /**
     * @brief Crea las tablas e inserta los datos iniciales en la base de datos.
     *
     * Es seguro llamar a esta función varias veces: usa «CREATE TABLE IF NOT
     * EXISTS» e «INSERT OR IGNORE» para que sea idempotente.
     *
     * @param db  Conexión SQLite abierta.
     * @return    true si todo el esquema se aplicó sin errores.
     */
    bool applySchema(sqlite3* db);
}

#endif // VM_SEED_DATA_H
