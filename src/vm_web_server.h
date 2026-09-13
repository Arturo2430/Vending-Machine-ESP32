#ifndef VM_WEB_SERVER_H
#define VM_WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "vm_database.h"

class VmWebServer {
public:
    VmWebServer(VmDatabase* db);
    
    // Inicia el SoftAP y el servidor HTTP
    bool begin();

private:
    AsyncWebServer server;
    VmDatabase* database;
    
    // Configura las rutas (estáticas y API)
    void setupRoutes();
};

#endif // VM_WEB_SERVER_H
