#include "vm_database.h"

VmDatabase::VmDatabase() {
    db = nullptr;
}

bool VmDatabase::begin() {
    sqlite3_initialize();
    
    // Abrir la base de datos (se creará un archivo vacío si no existe, a la espera del equipo de BD)
    int rc = sqlite3_open(dbPath, &db);
    if (rc != SQLITE_OK) {
        Serial.printf("[DB] Error abriendo base de datos: %s\n", sqlite3_errmsg(db));
        return false;
    }
    
    Serial.println("[DB] Base de datos SQLite inicializada. (A la espera del esquema oficial)");
    return true;
}

String VmDatabase::getSlotsJson() {
    // Como el esquema oficial aún no existe, devolvemos un JSON de prueba
    // para que puedas ir probando la red y el servidor.
    return "[{\"id\":1,\"product\":\"Prueba 1\",\"price\":15,\"stock\":5}]";
}

