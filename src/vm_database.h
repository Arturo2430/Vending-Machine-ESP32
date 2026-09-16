/**
 * @file vm_database.h
 * @brief Repositorio SQLite para la Máquina Expendedora SAID.
 *
 * Expone las operaciones transaccionales atómicas que la FSM necesita.
 * Toda la lectura/escritura a SQLite pasa por esta clase; RFID, UART y la
 * API web NUNCA abren la conexión directamente.
 *
 * Concurrencia: El servidor HTTP asíncrono corre en un contexto de tarea
 * FreeRTOS diferente al loop() principal. Se usa un Mutex binario para
 * serializar el acceso a la conexión SQLite.
 *
 * Tablas NO gestionadas activamente por la FSM (solo existencia en esquema):
 *   - movimientos_stock, movimientos_saldo, movimientos_efectivo → auditoría web.
 *   - comandos, reposiciones, eventos → complejidad innecesaria para MCU.
 */

#ifndef VM_DATABASE_H
#define VM_DATABASE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <sqlite3.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ---------------------------------------------------------------------------
// Estructuras de transferencia (plain data, sin heap dinámico)
// ---------------------------------------------------------------------------

struct SlotInfo {
    uint8_t  slotId;
    uint32_t priceCentavos;
    uint32_t stock;
    uint32_t reserved;
    uint32_t capacity;
    bool     enabled;
    uint32_t version;
    char     productName[32];
};

struct CardInfo {
    uint32_t cardId;
    uint32_t balanceCentavos;
    uint32_t reserveCentavos;
    bool     enabled;
};

// ---------------------------------------------------------------------------
// Clase repositorio
// ---------------------------------------------------------------------------

class VmDatabase {
public:
    VmDatabase();

    /**
     * @brief Inicializa SQLite, abre (o crea) la base de datos y aplica el
     *        semillado automático mediante VmSeedData si el archivo es nuevo.
     * @return true si la BD quedó operativa.
     */
    bool begin();

    // ---- Consultas --------------------------------------------------------

    /** Rellena @p out con datos del slot. false si no existe o está inactivo. */
    bool getSlot(uint8_t slotId, SlotInfo& out);

    /** Rellena @p out con datos de la tarjeta por UID hexadecimal. */
    bool checkCard(const String& uidHex, CardInfo& out);

    /** Valida el PIN de administrador. Comparación directa en texto plano. */
    bool verifyAdminPin(const char* pin);

    // ---- Transacciones de Venta -------------------------------------------

    /**
     * @brief Reserva un slot atómicamente y crea una transacción en estado RESERVADA.
     *
     * Disminuye stock y aumenta reservado en una sola transacción BEGIN IMMEDIATE.
     *
     * @param slotId            Canal a dispensar (1–4).
     * @param metodo            "EFECTIVO" o "RFID".
     * @param cardId            id_tarjeta si RFID; 0 si efectivo.
     * @param priceCentavos     Precio vigente del slot (snapshot histórico).
     * @param productName       Nombre del producto (snapshot histórico).
     * @param outTxId           ID de la transacción creada.
     * @return true si la reserva se realizó.
     */
    bool reserveSlot(uint8_t slotId, const char* metodo, uint32_t cardId,
                     uint32_t priceCentavos, const char* productName,
                     uint32_t& outTxId);

    /**
     * @brief Confirma la entrega física: reduce reservado y descuenta saldo de
     *        la tarjeta RFID si aplica.  Estado pasa a CONFIRMADA.
     */
    bool confirmSale(uint8_t slotId, uint32_t txId, uint32_t cardId,
                     uint32_t priceCentavos);

    /**
     * @brief Revierte la reserva por cancelación, timeout o falla de entrega.
     *        Stock vuelve a subir; estado pasa a INCIERTA.
     */
    bool revertSale(uint8_t slotId, uint32_t txId, uint32_t cardId,
                    uint32_t priceCentavos);

    /**
     * @brief Bloquea preventivamente el saldo de la tarjeta (reserva_centavos).
     * Llamar antes de enviar VEND al Mega.
     */
    bool reserveCardBalance(uint32_t cardId, uint32_t amountCentavos);

    /**
     * @brief Libera la reserva preventiva de saldo sin cobrar (cancelación).
     */
    bool releaseCardBalance(uint32_t cardId, uint32_t amountCentavos);

    // ---- Operaciones de Caja de Efectivo ----------------------------------

    /**
     * @brief Consulta el stock de una denominación de moneda.
     * @param denomCentavos  Valor en centavos (100, 200, 500, 1000).
     * @param outStock       Unidades disponibles.
     */
    bool getCoinStock(uint32_t denomCentavos, uint32_t& outStock);

    /**
     * @brief Registra el ingreso de monedas (pago del cliente).
     */
    bool addCoins(uint32_t denomCentavos, uint32_t count);

    /**
     * @brief Descuenta monedas entregadas como cambio.
     */
    bool deductCoins(uint32_t denomCentavos, uint32_t count);

    // ---- Mantenimiento (Admin) --------------------------------------------

    /** Actualiza el precio de un slot. */
    bool updateSlotPrice(uint8_t slotId, uint32_t newPriceCentavos);

    /** Actualiza el stock de un slot (cantidad total final, no delta). */
    bool updateSlotStock(uint8_t slotId, uint32_t newStock);

    // ---- API Web ----------------------------------------------------------

    /** Retorna los slots en JSON para el endpoint /api/slots. */
    String getSlotsJson();

    // ---- Gestión de Tarjetas (API Web) ------------------------------------

    /** Listar todas las tarjetas sin importar la habilitación (JSON) */
    String getAllCardsJson();

    /** Listar todas las tarjetas activas o inactivas según el parámetro (JSON) */
    String getCardsByStateJson(bool habilitada);

    /** Consulta de una tarjeta por ID (JSON) */
    String getCardByIdJson(uint32_t id);

    /** Registro de una nueva tarjeta */
    bool registerCard(const char* uid, uint32_t saldoCentavos, uint32_t reservaCentavos, bool habilitada);

    /** Modificar la habilitación de una tarjeta */
    bool setCardEnablement(uint32_t id, bool habilitada);

    /** Modificar el saldo de una tarjeta */
    bool updateCardBalance(uint32_t id, uint32_t saldoCentavos);

    // ---- Informes y Productos (API Web) -----------------------------------

    /** Obtener historial de ventas paginado en JSON */
    String getTransactionsJson(int page, int limit);

    /** Agregar un nuevo producto al catálogo general */
    bool addProduct(const char* nombre, uint32_t costoCentavos);

    /** Activar o desactivar un producto del catálogo (No se borran por historial) */
    bool setProductActive(uint32_t productId, bool activo);

private:
    sqlite3*          _db;
    SemaphoreHandle_t _mutex;

    static const char* DB_PATH;

    // Ejecuta SQL dentro del mutex ya tomado. No lo toma internamente.
    bool execSqlLocked(const char* sql);

    // Toma el mutex, ejecuta y lo libera.
    bool execSql(const char* sql);
};

#endif // VM_DATABASE_H
