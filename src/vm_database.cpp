/**
 * @file vm_database.cpp
 * @brief Implementación del repositorio SQLite para la Máquina Expendedora SAID.
 *
 * Cada método que escribe datos usa BEGIN IMMEDIATE / COMMIT para garantizar
 * atomicidad e idempotencia.  El Mutex de FreeRTOS serializa todos los accesos
 * entre el loop() principal y las tareas asíncronas del servidor web.
 *
 * @warning Si LittleFS reporta error de escritura, verificar que el sistema de
 *          archivos se montó correctamente y que el tamaño de partición en
 *          platformio.ini es suficiente.  SQLite necesita al menos el doble del
 *          tamaño de la BD para operar el WAL journal.
 */

#include "vm_database.h"
#include "vm_seed_data.h"

const char* VmDatabase::DB_PATH = "/littlefs/db/vending.db";

// ---------------------------------------------------------------------------
// Constructor / begin
// ---------------------------------------------------------------------------

VmDatabase::VmDatabase() : _db(nullptr), _mutex(nullptr) {}

bool VmDatabase::begin() {
    // Crear mutex antes que cualquier otro subsistema lo use.
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        Serial.println("[DB] Error: no se pudo crear el mutex.");
        return false;
    }

    sqlite3_initialize();

    int rc = sqlite3_open(DB_PATH, &_db);
    if (rc != SQLITE_OK) {
        Serial.printf("[DB] Error abriendo BD: %s\n", sqlite3_errmsg(_db));
        return false;
    }

    // Configuraciones de rendimiento y seguridad.
    execSqlLocked("PRAGMA foreign_keys = ON;");
    execSqlLocked("PRAGMA journal_mode = WAL;");
    execSqlLocked("PRAGMA synchronous = NORMAL;");

    // Aplicar esquema y semilla (idempotente).
    if (!VmSeedData::applySchema(_db)) {
        Serial.println("[DB] Advertencia: el semillado no completó correctamente.");
    }

    Serial.println("[DB] Base de datos operativa.");
    return true;
}

// ---------------------------------------------------------------------------
// Helpers internos
// ---------------------------------------------------------------------------

bool VmDatabase::execSqlLocked(const char* sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(_db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Serial.printf("[DB] SQL error: %s\n", errMsg ? errMsg : "?");
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

bool VmDatabase::execSql(const char* sql) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ok = execSqlLocked(sql);
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// Consultas
// ---------------------------------------------------------------------------

bool VmDatabase::getSlot(uint8_t slotId, SlotInfo& out) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT s.id_slot, s.precio_centavos, s.stock, s.reservado,"
        "       s.capacidad, s.habilitado, s.version, p.nombre "
        "FROM slots s "
        "LEFT JOIN productos p ON p.id_producto = s.id_producto "
        "WHERE s.id_slot = %u AND s.habilitado = 1 LIMIT 1;",
        slotId);

    bool found = false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out.slotId         = (uint8_t) sqlite3_column_int(stmt, 0);
            out.priceCentavos  = (uint32_t)sqlite3_column_int(stmt, 1);
            out.stock          = (uint32_t)sqlite3_column_int(stmt, 2);
            out.reserved       = (uint32_t)sqlite3_column_int(stmt, 3);
            out.capacity       = (uint32_t)sqlite3_column_int(stmt, 4);
            out.enabled        = sqlite3_column_int(stmt, 5) == 1;
            out.version        = (uint32_t)sqlite3_column_int(stmt, 6);
            const char* name   = (const char*)sqlite3_column_text(stmt, 7);
            strncpy(out.productName, name ? name : "?", sizeof(out.productName) - 1);
            out.productName[sizeof(out.productName) - 1] = '\0';
            found = true;
        }
        sqlite3_finalize(stmt);
    }

    xSemaphoreGive(_mutex);
    return found;
}

bool VmDatabase::checkCard(const String& uidHex, CardInfo& out) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT id_tarjeta, saldo_centavos, reserva_centavos, habilitada "
        "FROM tarjetas_demo WHERE uid = '%s' LIMIT 1;",
        uidHex.c_str());

    bool found = false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out.cardId          = (uint32_t)sqlite3_column_int(stmt, 0);
            out.balanceCentavos = (uint32_t)sqlite3_column_int(stmt, 1);
            out.reserveCentavos = (uint32_t)sqlite3_column_int(stmt, 2);
            out.enabled         = sqlite3_column_int(stmt, 3) == 1;
            found = true;
        }
        sqlite3_finalize(stmt);
    }

    xSemaphoreGive(_mutex);
    return found;
}

bool VmDatabase::verifyAdminPin(const char* pin) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT 1 FROM administradores "
        "WHERE hash_pin = '%s' AND activo = 1 LIMIT 1;",
        pin);

    bool valid = false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        valid = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
    }

    xSemaphoreGive(_mutex);
    return valid;
}

// ---------------------------------------------------------------------------
// Transacciones de Venta
// ---------------------------------------------------------------------------

bool VmDatabase::reserveSlot(uint8_t slotId, const char* metodo, uint32_t cardId,
                              uint32_t priceCentavos, const char* productName,
                              uint32_t& outTxId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    bool ok = false;
    outTxId = 0;

    if (!execSqlLocked("BEGIN IMMEDIATE;")) goto end;

    {
        // Verificar stock disponible.
        char check[128];
        snprintf(check, sizeof(check),
            "SELECT stock FROM slots WHERE id_slot = %u AND habilitado = 1;", slotId);
        sqlite3_stmt* stmt = nullptr;
        bool hasStock = false;
        if (sqlite3_prepare_v2(_db, check, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                hasStock = sqlite3_column_int(stmt, 0) > 0;
            }
            sqlite3_finalize(stmt);
        }
        if (!hasStock) {
            execSqlLocked("ROLLBACK;");
            goto end;
        }

        // Reducir stock y aumentar reservado.
        char update[128];
        snprintf(update, sizeof(update),
            "UPDATE slots SET stock = stock - 1, reservado = reservado + 1, "
            "version = version + 1 WHERE id_slot = %u;", slotId);
        if (!execSqlLocked(update)) { execSqlLocked("ROLLBACK;"); goto end; }

        // Crear transacción en estado RESERVADA.
        char cardRef[32];
        if (cardId > 0) {
            snprintf(cardRef, sizeof(cardRef), "%lu", (unsigned long)cardId);
        } else {
            strncpy(cardRef, "NULL", sizeof(cardRef));
        }

        char insert[512];
        snprintf(insert, sizeof(insert),
            "INSERT INTO transacciones "
            "(id_slot, id_tarjeta, metodo, precio_historico_centavos, nombre_historico, estado) "
            "VALUES (%u, %s, '%s', %lu, '%.30s', 'RESERVADA');",
            slotId, cardRef, metodo, (unsigned long)priceCentavos, productName);
        if (!execSqlLocked(insert)) { execSqlLocked("ROLLBACK;"); goto end; }

        outTxId = (uint32_t)sqlite3_last_insert_rowid(_db);
    }

    if (!execSqlLocked("COMMIT;")) goto end;
    ok = true;

end:
    xSemaphoreGive(_mutex);
    return ok;
}

bool VmDatabase::confirmSale(uint8_t slotId, uint32_t txId, uint32_t cardId,
                              uint32_t priceCentavos) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    bool ok = false;
    if (!execSqlLocked("BEGIN IMMEDIATE;")) goto end;

    {
        char sql[128];
        // Liberar reservado.
        snprintf(sql, sizeof(sql),
            "UPDATE slots SET reservado = reservado - 1, version = version + 1 "
            "WHERE id_slot = %u;", slotId);
        if (!execSqlLocked(sql)) { execSqlLocked("ROLLBACK;"); goto end; }

        // Marcar transacción como CONFIRMADA.
        snprintf(sql, sizeof(sql),
            "UPDATE transacciones SET estado = 'CONFIRMADA' WHERE id_transaccion = %lu;",
            (unsigned long)txId);
        if (!execSqlLocked(sql)) { execSqlLocked("ROLLBACK;"); goto end; }

        // Cobrar saldo RFID si aplica.
        if (cardId > 0) {
            snprintf(sql, sizeof(sql),
                "UPDATE tarjetas_demo "
                "SET saldo_centavos   = saldo_centavos   - %lu, "
                "    reserva_centavos = reserva_centavos - %lu "
                "WHERE id_tarjeta = %lu;",
                (unsigned long)priceCentavos,
                (unsigned long)priceCentavos,
                (unsigned long)cardId);
            if (!execSqlLocked(sql)) { execSqlLocked("ROLLBACK;"); goto end; }
        }
    }

    if (!execSqlLocked("COMMIT;")) goto end;
    ok = true;

end:
    xSemaphoreGive(_mutex);
    return ok;
}

bool VmDatabase::revertSale(uint8_t slotId, uint32_t txId, uint32_t cardId,
                             uint32_t priceCentavos) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    bool ok = false;
    if (!execSqlLocked("BEGIN IMMEDIATE;")) goto end;

    {
        char sql[128];
        // Devolver stock.
        snprintf(sql, sizeof(sql),
            "UPDATE slots SET stock = stock + 1, reservado = reservado - 1, "
            "version = version + 1 WHERE id_slot = %u;", slotId);
        if (!execSqlLocked(sql)) { execSqlLocked("ROLLBACK;"); goto end; }

        // Marcar transacción como INCIERTA.
        snprintf(sql, sizeof(sql),
            "UPDATE transacciones SET estado = 'INCIERTA' WHERE id_transaccion = %lu;",
            (unsigned long)txId);
        if (!execSqlLocked(sql)) { execSqlLocked("ROLLBACK;"); goto end; }

        // Liberar reserva de saldo RFID si aplica.
        if (cardId > 0) {
            snprintf(sql, sizeof(sql),
                "UPDATE tarjetas_demo "
                "SET reserva_centavos = reserva_centavos - %lu "
                "WHERE id_tarjeta = %lu;",
                (unsigned long)priceCentavos,
                (unsigned long)cardId);
            if (!execSqlLocked(sql)) { execSqlLocked("ROLLBACK;"); goto end; }
        }
    }

    if (!execSqlLocked("COMMIT;")) goto end;
    ok = true;

end:
    xSemaphoreGive(_mutex);
    return ok;
}

bool VmDatabase::reserveCardBalance(uint32_t cardId, uint32_t amountCentavos) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE tarjetas_demo "
        "SET reserva_centavos = reserva_centavos + %lu "
        "WHERE id_tarjeta = %lu AND (saldo_centavos - reserva_centavos) >= %lu;",
        (unsigned long)amountCentavos,
        (unsigned long)cardId,
        (unsigned long)amountCentavos);
    bool ok = execSql(sql);
    if (ok) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        ok = sqlite3_changes(_db) > 0;
        xSemaphoreGive(_mutex);
    }
    return ok;
}

bool VmDatabase::releaseCardBalance(uint32_t cardId, uint32_t amountCentavos) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE tarjetas_demo "
        "SET reserva_centavos = MAX(0, reserva_centavos - %lu) "
        "WHERE id_tarjeta = %lu;",
        (unsigned long)amountCentavos,
        (unsigned long)cardId);
    return execSql(sql);
}

// ---------------------------------------------------------------------------
// Caja de Efectivo
// ---------------------------------------------------------------------------

bool VmDatabase::getCoinStock(uint32_t denomCentavos, uint32_t& outStock) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT c.cantidad FROM caja_efectivo c "
        "JOIN denominaciones d ON d.id_denominacion = c.id_denominacion "
        "WHERE d.valor_centavos = %lu AND d.tipo = 'moneda' AND d.activa = 1 LIMIT 1;",
        (unsigned long)denomCentavos);

    bool found = false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            outStock = (uint32_t)sqlite3_column_int(stmt, 0);
            found = true;
        }
        sqlite3_finalize(stmt);
    }

    xSemaphoreGive(_mutex);
    return found;
}

bool VmDatabase::addCoins(uint32_t denomCentavos, uint32_t count) {
    char sql[256];
    snprintf(sql, sizeof(sql),
        "UPDATE caja_efectivo SET cantidad = cantidad + %lu, version = version + 1 "
        "WHERE id_denominacion = ("
        "  SELECT id_denominacion FROM denominaciones "
        "  WHERE valor_centavos = %lu AND tipo = 'moneda' LIMIT 1);",
        (unsigned long)count, (unsigned long)denomCentavos);
    return execSql(sql);
}

bool VmDatabase::deductCoins(uint32_t denomCentavos, uint32_t count) {
    char sql[256];
    snprintf(sql, sizeof(sql),
        "UPDATE caja_efectivo SET cantidad = cantidad - %lu, version = version + 1 "
        "WHERE id_denominacion = ("
        "  SELECT id_denominacion FROM denominaciones "
        "  WHERE valor_centavos = %lu AND tipo = 'moneda' LIMIT 1) "
        "  AND cantidad >= %lu;",
        (unsigned long)count, (unsigned long)denomCentavos, (unsigned long)count);
    if (!execSql(sql)) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ok = sqlite3_changes(_db) > 0;
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// Mantenimiento (Admin)
// ---------------------------------------------------------------------------

bool VmDatabase::updateSlotPrice(uint8_t slotId, uint32_t newPriceCentavos) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE slots SET precio_centavos = %lu, version = version + 1 "
        "WHERE id_slot = %u;",
        (unsigned long)newPriceCentavos, slotId);
    return execSql(sql);
}

bool VmDatabase::updateSlotStock(uint8_t slotId, uint32_t newStock) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE slots SET stock = %lu, version = version + 1 "
        "WHERE id_slot = %u AND %lu + reservado <= capacidad;",
        (unsigned long)newStock, slotId, (unsigned long)newStock);
    if (!execSql(sql)) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    bool ok = sqlite3_changes(_db) > 0;
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// API Web
// ---------------------------------------------------------------------------

String VmDatabase::getSlotsJson() {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    const char* sql = "SELECT s.id_slot, p.nombre, s.precio_centavos, s.stock, s.habilitado, s.capacidad, s.version FROM slots s LEFT JOIN productos p ON p.id_producto = s.id_producto ORDER BY s.id_slot;";

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"]       = sqlite3_column_int(stmt, 0);
            if (sqlite3_column_type(stmt, 1) != SQLITE_NULL) {
                obj["product"] = (const char*)sqlite3_column_text(stmt, 1);
            } else {
                obj["product"] = (char*)0;
            }
            obj["price"]    = sqlite3_column_int(stmt, 2);
            obj["stock"]    = sqlite3_column_int(stmt, 3);
            obj["active"]   = sqlite3_column_int(stmt, 4) == 1;
            obj["capacity"] = sqlite3_column_int(stmt, 5);
            obj["version"]  = sqlite3_column_int(stmt, 6);
        }
        sqlite3_finalize(stmt);
    }

    xSemaphoreGive(_mutex);

    String out;
    serializeJson(doc, out);
    return out;
}
// ---- Gestión de Tarjetas (API Web) ------------------------------------

String VmDatabase::getAllCardsJson() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const char* sql = "SELECT id_tarjeta, uid, saldo_centavos, habilitada FROM tarjetas_demo;";
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = sqlite3_column_int(stmt, 0);
            obj["uid"] = (const char*)sqlite3_column_text(stmt, 1);
            obj["balance"] = sqlite3_column_int(stmt, 2);
            obj["active"] = sqlite3_column_int(stmt, 3) == 1;
        }
        sqlite3_finalize(stmt);
    }
    xSemaphoreGive(_mutex);
    String out;
    serializeJson(doc, out);
    return out;
}

String VmDatabase::getCardsByStateJson(bool habilitada) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const char* sql = "SELECT id_tarjeta, uid FROM tarjetas_demo WHERE habilitada = ?;";
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, habilitada ? 1 : 0);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = sqlite3_column_int(stmt, 0);
            obj["uid"] = (const char*)sqlite3_column_text(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    xSemaphoreGive(_mutex);
    String out;
    serializeJson(doc, out);
    return out;
}

String VmDatabase::getCardByIdJson(uint32_t id) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const char* sql = "SELECT uid, saldo_centavos, reserva_centavos, habilitada FROM tarjetas_demo WHERE id_tarjeta = ?;";
    JsonDocument doc;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            doc["uid"] = (const char*)sqlite3_column_text(stmt, 0);
            doc["saldo"] = sqlite3_column_int(stmt, 1);
            doc["reserva"] = sqlite3_column_int(stmt, 2);
            doc["habilitada"] = sqlite3_column_int(stmt, 3) == 1;
        }
        sqlite3_finalize(stmt);
    }
    xSemaphoreGive(_mutex);
    String out;
    serializeJson(doc, out);
    return out;
}

bool VmDatabase::registerCard(const char* uid, uint32_t saldoCentavos, uint32_t reservaCentavos, bool habilitada) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "INSERT INTO tarjetas_demo (uid, saldo_centavos, reserva_centavos, habilitada) "
        "VALUES ('%s', %lu, %lu, %d);",
        uid, (unsigned long)saldoCentavos, (unsigned long)reservaCentavos, habilitada ? 1 : 0);
    return execSql(sql);
}

bool VmDatabase::setCardEnablement(uint32_t id, bool habilitada) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE tarjetas_demo SET habilitada = %d WHERE id_tarjeta = %u;",
        habilitada ? 1 : 0, id);
    return execSql(sql);
}

bool VmDatabase::updateCardBalance(uint32_t id, uint32_t saldoCentavos) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE tarjetas_demo SET saldo_centavos = %lu WHERE id_tarjeta = %u;",
        (unsigned long)saldoCentavos, id);
    return execSql(sql);
}

// ---- Informes y Productos (API Web) -----------------------------------

String VmDatabase::getTransactionsJson(int page, int limit) {
    if (page < 1) page = 1;
    if (limit < 1) limit = 10;
    int offset = (page - 1) * limit;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    
    // Primero obtener el total de registros para la paginación
    int totalRegistros = 0;
    const char* countSql = "SELECT COUNT(*) FROM transacciones;";
    sqlite3_stmt* countStmt = nullptr;
    if (sqlite3_prepare_v2(_db, countSql, -1, &countStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(countStmt) == SQLITE_ROW) {
            totalRegistros = sqlite3_column_int(countStmt, 0);
        }
        sqlite3_finalize(countStmt);
    }
    
    int totalPages = (totalRegistros + limit - 1) / limit;

    // Obtener los datos paginados
    const char* sql = "SELECT id_transaccion, slot, producto, metodo, precio_historico, estado, secuencia "
                      "FROM transacciones ORDER BY id_transaccion DESC LIMIT ? OFFSET ?;";
                      
    JsonDocument doc;
    doc["page"] = page;
    doc["total_pages"] = totalPages;
    doc["total_records"] = totalRegistros;
    
    JsonArray arr = doc["data"].to<JsonArray>();

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);
        
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = sqlite3_column_int(stmt, 0);
            obj["slot"] = sqlite3_column_int(stmt, 1);
            obj["producto"] = (const char*)sqlite3_column_text(stmt, 2);
            obj["metodo"] = (const char*)sqlite3_column_text(stmt, 3);
            obj["precio"] = sqlite3_column_int(stmt, 4);
            obj["estado"] = (const char*)sqlite3_column_text(stmt, 5);
            obj["secuencia"] = sqlite3_column_int(stmt, 6);
        }
        sqlite3_finalize(stmt);
    }

    xSemaphoreGive(_mutex);

    String out;
    serializeJson(doc, out);
    return out;
}

bool VmDatabase::addProduct(const char* nombre, uint32_t costoCentavos) {
    char sql[128];
    // Se inserta activo = 1 por defecto
    snprintf(sql, sizeof(sql),
        "INSERT INTO productos (codigo_unico, nombre, costo_referencia, activo) VALUES ('P-%lu', '%s', %lu, 1);",
        (unsigned long)millis(), nombre, (unsigned long)costoCentavos);
    return execSql(sql);
}

bool VmDatabase::setProductActive(uint32_t productId, bool activo) {
    char sql[128];
    snprintf(sql, sizeof(sql),
        "UPDATE productos SET activo = %d WHERE id_producto = %u;",
        activo ? 1 : 0, productId);
    return execSql(sql);
}

String VmDatabase::getProductsJson() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    const char* sql = "SELECT id_producto, nombre, costo_referencia, activo FROM productos;";
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = sqlite3_column_int(stmt, 0);
            obj["name"] = (const char*)sqlite3_column_text(stmt, 1);
            obj["cost"] = sqlite3_column_int(stmt, 2);
            obj["active"] = sqlite3_column_int(stmt, 2) == 1;
            
        }
        sqlite3_finalize(stmt);
    }
    xSemaphoreGive(_mutex);
    String out;
    serializeJson(doc, out);
    return out;
}

bool VmDatabase::updateSlotProduct(uint8_t slotId, uint32_t productId) {
    xSemaphoreTake(_mutex, portMAX_DELAY);

    char sql[128];
    snprintf(sql, sizeof(sql), "SELECT costo_referencia FROM productos WHERE id_producto = %u LIMIT 1;", productId);

    uint32_t nuevoPrecio = 0;
    bool found = false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            nuevoPrecio = sqlite3_column_int(stmt, 0);
            found = true;
        }
        sqlite3_finalize(stmt);
    }

    bool ok = false;
    if (found) {
        snprintf(sql, sizeof(sql), "UPDATE slots SET id_producto = %u, precio_centavos = %lu, version = version + 1 WHERE id_slot = %u;", productId, (unsigned long)nuevoPrecio, slotId);
        ok = execSqlLocked(sql);
    }

    xSemaphoreGive(_mutex);
    return ok;
}

bool VmDatabase::setCardActive(const char* uid, bool active) {
    char sql[128];
    snprintf(sql, sizeof(sql), "UPDATE tarjetas_demo SET habilitada = %d WHERE uid = '%s';", active ? 1 : 0, uid);
    return execSql(sql);
}

bool VmDatabase::registerCardWeb(const char* uid, uint32_t initialBalance) {
    char sql[128];
    snprintf(sql, sizeof(sql), "INSERT INTO tarjetas_demo (uid, saldo_centavos, reserva_centavos, habilitada) VALUES ('%s', %lu, 0, 1);", uid, initialBalance);
    return execSql(sql);
}




