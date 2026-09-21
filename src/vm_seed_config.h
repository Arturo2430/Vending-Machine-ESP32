/**
 * @file vm_seed_config.h
 * @brief Configuración de datos semilla (productos, tarjetas, admin).
 *        Modificar estos valores antes de compilar para inicializar la BD.
 */

#ifndef VM_SEED_CONFIG_H
#define VM_SEED_CONFIG_H

// ---------------------------------------------------------------------------
// Productos: código, nombre, costo de referencia (centavos)
// ---------------------------------------------------------------------------
static const char DML_PRODUCTOS[] =
    "INSERT OR IGNORE INTO productos (codigo_unico, nombre, costo_referencia) VALUES"
    "  ('PROD01','Coca-Cola 355ml', 1800),"
    "  ('PROD02','Galletas Marias', 1500),"
    "  ('PROD03','Agua 600ml', 1200),"
    "  ('PROD04','Jugo Naranja', 1400);";

// ---------------------------------------------------------------------------
// Slots: id_slot, id_producto, precio_centavos, capacidad, stock
// ---------------------------------------------------------------------------
static const char DML_SLOTS[] =
    "INSERT OR IGNORE INTO slots (id_slot, id_producto, precio_centavos, capacidad, stock) VALUES"
    "  (1, 1, 1800, 10, 8),"
    "  (2, 2, 1500, 10, 6),"
    "  (3, 3, 1200, 10, 9),"
    "  (4, 4, 1400, 10, 5);";

// ---------------------------------------------------------------------------
// Tarjetas RFID (UID hexadecimal, saldo inicial centavos)
// ---------------------------------------------------------------------------
static const char DML_TARJETAS[] =
    "INSERT OR IGNORE INTO tarjetas_demo (uid, saldo_centavos) VALUES"
    "  ('A1B2C3D4', 5000),"   // $50.00
    "  ('E5F6A7B8', 10000);"; // $100.00

// ---------------------------------------------------------------------------
// Denominaciones de monedas (centavos, tipo)
// ---------------------------------------------------------------------------
static const char DML_DENOMINACIONES[] =
    "INSERT OR IGNORE INTO denominaciones (valor_centavos, tipo) VALUES"
    "  (100,   'moneda'),"  // $1
    "  (200,   'moneda'),"  // $2
    "  (500,   'moneda'),"  // $5
    "  (1000,  'moneda');"; // $10

// ---------------------------------------------------------------------------
// Stock inicial de caja de monedas (10 de cada denominación activa)
// ---------------------------------------------------------------------------
static const char DML_CAJA_EFECTIVO[] =
    "INSERT OR IGNORE INTO caja_efectivo (id_denominacion, cantidad)"
    "  SELECT id_denominacion, 10 FROM denominaciones WHERE tipo = 'moneda';";

// ---------------------------------------------------------------------------
// Credenciales de Administrador (usuario, PIN)
// ---------------------------------------------------------------------------
static const char DML_ADMINISTRADOR[] =
    "INSERT OR IGNORE INTO administradores (nombre_acceso, hash_pin) VALUES"
    "  ('admin', '1234');";

// ---------------------------------------------------------------------------
// Marca de inicialización
// ---------------------------------------------------------------------------
static const char DML_CONFIGURACION[] =
    "INSERT OR IGNORE INTO configuraciones (id_configuracion, version_esquema, estado_inicializacion)"
    "  VALUES (1, 1, 'OK');";

#endif // VM_SEED_CONFIG_H
