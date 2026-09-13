#include "vm_web_server.h"

VmWebServer::VmWebServer(VmDatabase* db) : server(80), database(db) {
}

bool VmWebServer::begin() {
    Serial.println("[WEB] Configurando SoftAP...");
    
    // Configuración del Access Point
    WiFi.softAP("MAQUINA_SAID", "admin1234");
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("[WEB] AP Iniciado. IP: ");
    Serial.println(IP);

    setupRoutes();
    
    server.begin();
    Serial.println("[WEB] Servidor HTTP Asíncrono iniciado en el puerto 80.");
    
    return true;
}

void VmWebServer::setupRoutes() {
    // 1. Ruta para entregar la página web (Archivos estáticos desde LittleFS)
    // El ESPAsyncWebServer puede servir directamente archivos de LittleFS.
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    // 2. Ruta API: Estado general
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        // Retornamos un JSON hardcodeado temporalmente
        request->send(200, "application/json", "{\"status\":\"ok\", \"door\":\"closed\"}");
    });

    // 3. Ruta API: Inventario (Slots) que llama a la base de datos
    server.on("/api/slots", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(this->database) {
            String jsonRespuesta = this->database->getSlotsJson();
            request->send(200, "application/json", jsonRespuesta);
        } else {
            request->send(500, "application/json", "{\"error\":\"Database not initialized\"}");
        }
    });

    // Ruta de fallback (404)
    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Error 404: Ruta no encontrada en la Máquina SAID.");
    });
}
