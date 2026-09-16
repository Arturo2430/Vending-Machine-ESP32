/**
 * @file vm_seed_data.cpp
 * @brief Implementación del semillado automático de la base de datos SQLite.
 *
 * Aplica el DDL mínimo requerido por la FSM y los datos de prueba necesarios
 * para operar sin configuración previa:
 *   - 4 productos y sus slots con stock y precio.
 *   - 2 tarjetas RFID de prueba con saldo.
 *   - Denominaciones de monedas MXN aceptadas.
 *   - Stock inicial de caja de efectivo (monedas disponibles para dar cambio).
 *   - 1 usuario administrador con PIN "1234".
 *
 * @note La tabla `configuraciones` solo se usa en lectura durante el arranque;
 *       no se escribe desde la FSM en tiempo de ejecución.
 * @note Las tablas de auditoría (movimientos_stock, movimientos_saldo,
 *       movimientos_efectivo, comandos, reposiciones, eventos) se crean aquí
 *       para satisfacer las claves foráneas del esquema, pero no son escritas
 *       por el flujo principal del microcontrolador.
 */

#include "vm_seed_data.h"
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------

static bool execSql(sqlite3* db, const char* sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Serial.printf("[SEED] Error SQL: %s\n", errMsg ? errMsg : "?");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// DDL — Creación de tablas (idempotente con IF NOT EXISTS)
// ---------------------------------------------------------------------------

static const char DDL_ADMINISTRADORES[] =
    "CREATE TABLE IF NOT EXISTS administradores ("
    "  id_admin      INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  nombre_acceso TEXT    NOT NULL UNIQUE,"
    "  hash_pin      TEXT    NOT NULL,"
    "  activo        INTEGER NOT NULL DEFAULT 1 CHECK (activo IN (0,1))"
    ");";

static const char DDL_PRODUCTOS[] =
    "CREATE TABLE IF NOT EXISTS productos ("
    "  id_producto  INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  codigo_unico TEXT    NOT NULL UNIQUE,"
    "  nombre       TEXT    NOT NULL,"
    "  activo       INTEGER NOT NULL DEFAULT 1 CHECK (activo IN (0,1))"
    ");";

static const char DDL_SLOTS[] =
    "CREATE TABLE IF NOT EXISTS slots ("
    "  id_slot         INTEGER PRIMARY KEY CHECK (id_slot BETWEEN 1 AND 4),"
    "  id_producto     INTEGER REFERENCES productos(id_producto),"
    "  precio_centavos INTEGER NOT NULL CHECK (precio_centavos >= 0),"
    "  capacidad       INTEGER NOT NULL CHECK (capacidad >= 0),"
    "  stock           INTEGER NOT NULL DEFAULT 0 CHECK (stock >= 0),"
    "  reservado       INTEGER NOT NULL DEFAULT 0 CHECK (reservado >= 0),"
    "  habilitado      INTEGER NOT NULL DEFAULT 1 CHECK (habilitado IN (0,1)),"
    "  version         INTEGER NOT NULL DEFAULT 0,"
    "  CHECK (stock + reservado <= capacidad)"
    ");";

static const char DDL_TARJETAS[] =
    "CREATE TABLE IF NOT EXISTS tarjetas_demo ("
    "  id_tarjeta       INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  uid              TEXT    NOT NULL UNIQUE,"
    "  saldo_centavos   INTEGER NOT NULL DEFAULT 0 CHECK (saldo_centavos >= 0),"
    "  reserva_centavos INTEGER NOT NULL DEFAULT 0 CHECK (reserva_centavos >= 0),"
    "  habilitada       INTEGER NOT NULL DEFAULT 1 CHECK (habilitada IN (0,1)),"
    "  CHECK (saldo_centavos - reserva_centavos >= 0)"
    ");";

static const char DDL_TRANSACCIONES[] =
    "CREATE TABLE IF NOT EXISTS transacciones ("
    "  id_transaccion            INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  id_slot                   INTEGER NOT NULL REFERENCES slots(id_slot),"
    "  id_tarjeta                INTEGER REFERENCES tarjetas_demo(id_tarjeta),"
    "  metodo                    TEXT    NOT NULL,"
    "  precio_historico_centavos INTEGER NOT NULL CHECK (precio_historico_centavos >= 0),"
    "  nombre_historico          TEXT    NOT NULL,"
    "  estado                    TEXT    NOT NULL CHECK (estado IN ('RESERVADA','CONFIRMADA','INCIERTA'))"
    ");";

static const char DDL_DENOMINACIONES[] =
    "CREATE TABLE IF NOT EXISTS denominaciones ("
    "  id_denominacion INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  valor_centavos  INTEGER NOT NULL CHECK (valor_centavos > 0),"
    "  tipo            TEXT    NOT NULL CHECK (tipo IN ('moneda','billete')),"
    "  activa          INTEGER NOT NULL DEFAULT 1 CHECK (activa IN (0,1)),"
    "  UNIQUE (valor_centavos, tipo)"
    ");";

static const char DDL_CAJA_EFECTIVO[] =
    "CREATE TABLE IF NOT EXISTS caja_efectivo ("
    "  id_denominacion INTEGER PRIMARY KEY REFERENCES denominaciones(id_denominacion),"
    "  cantidad        INTEGER NOT NULL DEFAULT 0 CHECK (cantidad >= 0),"
    "  version         INTEGER NOT NULL DEFAULT 0"
    ");";

// Tablas de auditoría — Solo se crean, no se escriben desde la FSM.
static const char DDL_AUDIT_TABLES[] =
    "CREATE TABLE IF NOT EXISTS movimientos_stock ("
    "  id_movimiento_stock INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  id_slot             INTEGER NOT NULL REFERENCES slots(id_slot),"
    "  referencia_operacion TEXT,"
    "  delta               INTEGER NOT NULL,"
    "  motivo              TEXT NOT NULL CHECK (motivo IN ('VENTA','REPOSICION','AJUSTE')),"
    "  operador            INTEGER REFERENCES administradores(id_admin)"
    ");"
    "CREATE TABLE IF NOT EXISTS movimientos_saldo ("
    "  id_movimiento_saldo INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  id_tarjeta          INTEGER NOT NULL REFERENCES tarjetas_demo(id_tarjeta),"
    "  referencia_opcional TEXT,"
    "  delta_centavos      INTEGER NOT NULL,"
    "  tipo                TEXT NOT NULL CHECK (tipo IN ('COMPRA','RECARGA')),"
    "  operador            INTEGER REFERENCES administradores(id_admin),"
    "  request_id          TEXT NOT NULL UNIQUE"
    ");"
    "CREATE TABLE IF NOT EXISTS eventos ("
    "  id_evento             INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  tipo                  TEXT NOT NULL CHECK (tipo IN ('ERROR','INFO','ADVERTENCIA')),"
    "  operacion_relacionada TEXT,"
    "  codigo_error          TEXT,"
    "  datos_breves          TEXT"
    ");"
    "CREATE TABLE IF NOT EXISTS configuraciones ("
    "  id_configuracion      INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  version_esquema       INTEGER NOT NULL,"
    "  estado_inicializacion TEXT"
    ");";

// ---------------------------------------------------------------------------
// DML — Datos semilla (idempotente con INSERT OR IGNORE)
// ---------------------------------------------------------------------------

static const char DML_PRODUCTOS[] =
    "INSERT OR IGNORE INTO productos (codigo_unico, nombre) VALUES"
    "  ('PROD01','Coca-Cola 355ml'),"
    "  ('PROD02','Galletas Marias'),"
    "  ('PROD03','Agua 600ml'),"
    "  ('PROD04','Jugo de Naranja');";

// precio en centavos: $18.00 = 1800, $15.00 = 1500, $12.00 = 1200, $14.00 = 1400
static const char DML_SLOTS[] =
    "INSERT OR IGNORE INTO slots (id_slot, id_producto, precio_centavos, capacidad, stock) VALUES"
    "  (1, 1, 1800, 10, 8),"
    "  (2, 2, 1500, 10, 6),"
    "  (3, 3, 1200, 10, 9),"
    "  (4, 4, 1400, 10, 5);";

// UIDs de ejemplo; en producción reemplazar con los UIDs reales del lote de tarjetas.
static const char DML_TARJETAS[] =
    "INSERT OR IGNORE INTO tarjetas_demo (uid, saldo_centavos) VALUES"
    "  ('A1B2C3D4', 5000),"   // $50.00
    "  ('E5F6A7B8', 10000);"; // $100.00

static const char DML_DENOMINACIONES[] =
    "INSERT OR IGNORE INTO denominaciones (valor_centavos, tipo) VALUES"
    "  (100,   'moneda'),"  // $1
    "  (200,   'moneda'),"  // $2
    "  (500,   'moneda'),"  // $5
    "  (1000,  'moneda');"; // $10
// Las denominaciones de billete no se usan en el algoritmo de cambio (sin tolva).

// Stock inicial de monedas para dar cambio (10 de cada denominación).
static const char DML_CAJA_EFECTIVO[] =
    "INSERT OR IGNORE INTO caja_efectivo (id_denominacion, cantidad)"
    "  SELECT id_denominacion, 10 FROM denominaciones WHERE tipo = 'moneda';";

static const char DML_ADMINISTRADOR[] =
    "INSERT OR IGNORE INTO administradores (nombre_acceso, hash_pin) VALUES"
    "  ('admin', '1234');";

static const char DML_CONFIGURACION[] =
    "INSERT OR IGNORE INTO configuraciones (id_configuracion, version_esquema, estado_inicializacion)"
    "  VALUES (1, 1, 'OK');";

// ---------------------------------------------------------------------------
// Implementación pública
// ---------------------------------------------------------------------------

namespace VmSeedData {

bool applySchema(sqlite3* db) {
    if (!db) return false;

    Serial.println("[SEED] Aplicando esquema y datos iniciales...");

    // Activar claves foráneas.
    if (!execSql(db, "PRAGMA foreign_keys = ON;")) return false;

    // Crear tablas operativas.
    if (!execSql(db, DDL_ADMINISTRADORES)) return false;
    if (!execSql(db, DDL_PRODUCTOS))       return false;
    if (!execSql(db, DDL_SLOTS))           return false;
    if (!execSql(db, DDL_TARJETAS))        return false;
    if (!execSql(db, DDL_TRANSACCIONES))   return false;
    if (!execSql(db, DDL_DENOMINACIONES))  return false;
    if (!execSql(db, DDL_CAJA_EFECTIVO))   return false;

    // Crear tablas de auditoría (sin datos semilla).
    if (!execSql(db, DDL_AUDIT_TABLES))    return false;

    // Insertar datos semilla.
    if (!execSql(db, DML_PRODUCTOS))        return false;
    if (!execSql(db, DML_SLOTS))            return false;
    if (!execSql(db, DML_TARJETAS))         return false;
    if (!execSql(db, DML_DENOMINACIONES))   return false;
    if (!execSql(db, DML_CAJA_EFECTIVO))    return false;
    if (!execSql(db, DML_ADMINISTRADOR))    return false;
    if (!execSql(db, DML_CONFIGURACION))    return false;

    Serial.println("[SEED] Esquema e inicialización completados correctamente.");
    return true;
}

} // namespace VmSeedData
