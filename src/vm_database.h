#ifndef VM_DATABASE_H
#define VM_DATABASE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <sqlite3.h>
#include <ArduinoJson.h>

class VmDatabase {
public:
    VmDatabase();
    
    // Inicializa el sistema de archivos y abre la base de datos
    bool begin();
    
    // Ejecuta una consulta SQL y devuelve el resultado en JSON (por ejemplo, para /api/slots)
    String getSlotsJson();

private:
    sqlite3 *db;
    const char *dbPath = "/littlefs/db/vending.db";
    
    // Callback genérico para SQLite (opcional, dependiendo de cómo hagamos el fetch)
    static int sqliteCallback(void *data, int argc, char **argv, char **azColName);
};

#endif // VM_DATABASE_H
