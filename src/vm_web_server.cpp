#include "vm_web_server.h"
#include "AsyncJson.h"
#include <ArduinoJson.h>

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
    // 1. Ruta estática (Interfaz Web)
    server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    // ---------------------------------------------------------
    // RUTAS GET (Solo lectura)
    // ---------------------------------------------------------

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"status\":\"ok\", \"door\":\"closed\", \"mode\":\"maintenance\"}");
    });

    server.on("/api/slots", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(this->database) request->send(200, "application/json", this->database->getSlotsJson());
        else request->send(500, "application/json", "{\"error\":\"DB error\"}");
    });

    server.on("/api/cards", HTTP_GET, [this](AsyncWebServerRequest *request){
        if(this->database) request->send(200, "application/json", this->database->getAllCardsJson());
        else request->send(500, "application/json", "{\"error\":\"DB error\"}");
    });

    server.on("/api/reports/transactions", HTTP_GET, [this](AsyncWebServerRequest *request){
        int page = 1;
        int limit = 10;
        if (request->hasParam("page")) page = request->getParam("page")->value().toInt();
        if (request->hasParam("limit")) limit = request->getParam("limit")->value().toInt();
        
        if(this->database) request->send(200, "application/json", this->database->getTransactionsJson(page, limit));
        else request->send(500, "application/json", "{\"error\":\"DB error\"}");
    });

    // ---------------------------------------------------------
    // RUTAS POST (Escritura / JSON Body)
    // ---------------------------------------------------------

    // Autenticación
    AsyncCallbackJsonWebHandler *loginHandler = new AsyncCallbackJsonWebHandler("/api/login", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        const char* pin = jsonObj["pin"];
        if (this->database && this->database->verifyAdminPin(pin)) {
            request->send(200, "application/json", "{\"success\":true,\"token\":\"token_valido\"}");
        } else {
            request->send(401, "application/json", "{\"success\":false,\"error\":\"Credenciales invalidas\"}");
        }
    });
    server.addHandler(loginHandler);

    server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Agregar Producto
    AsyncCallbackJsonWebHandler *addProductHandler = new AsyncCallbackJsonWebHandler("/api/products", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        const char* name = jsonObj["name"];
        uint32_t cost = jsonObj["cost"];
        if (this->database && name && this->database->addProduct(name, cost)) {
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"success\":false}");
        }
    });
    server.addHandler(addProductHandler);

    // Activar/Desactivar Producto
    AsyncCallbackJsonWebHandler *prodStateHandler = new AsyncCallbackJsonWebHandler("/api/products/state", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        uint32_t prodId = jsonObj["product_id"];
        bool active = jsonObj["active"];
        if (this->database && this->database->setProductActive(prodId, active)) {
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"success\":false}");
        }
    });
    server.addHandler(prodStateHandler);

    // Rellenar Slot
    AsyncCallbackJsonWebHandler *restockHandler = new AsyncCallbackJsonWebHandler("/api/slots/restock", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        uint8_t slotId = jsonObj["slot"];
        uint32_t addedQty = jsonObj["added_qty"];
        
        if (this->database) {
            SlotInfo info;
            // 1. Obtener el stock actual
            if (this->database->getSlot(slotId, info)) {
                // 2. Sumarle la cantidad agregada
                if (this->database->updateSlotStock(slotId, info.stock + addedQty)) {
                    request->send(200, "application/json", "{\"success\":true}");
                    return;
                }
            }
        }
        request->send(500, "application/json", "{\"success\":false}");
    });
    server.addHandler(restockHandler);

    // Recargar Tarjeta
    AsyncCallbackJsonWebHandler *rechargeHandler = new AsyncCallbackJsonWebHandler("/api/cards/recharge", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        JsonObject jsonObj = json.as<JsonObject>();
        String uid = jsonObj["uid"].as<String>();
        uint32_t amount = jsonObj["amount"];
        
        if (this->database) {
            CardInfo info;
            // 1. Obtener saldo actual
            if (this->database->checkCard(uid, info)) {
                // 2. Sumar el saldo y guardar
                if (this->database->updateCardBalance(info.cardId, info.balanceCentavos + amount)) {
                    request->send(200, "application/json", "{\"success\":true}");
                    return;
                }
            }
        }
        request->send(500, "application/json", "{\"success\":false}");
    });
    server.addHandler(rechargeHandler);

    // ---------------------------------------------------------
    // FALLBACK Y CORS
    // ---------------------------------------------------------
    server.onNotFound([](AsyncWebServerRequest *request){
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
        } else {
            request->send(404, "text/plain", "Error 404: Ruta no encontrada en la Máquina SAID.");
        }
    });
}
