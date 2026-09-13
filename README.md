<div align="center">

# Firmware ESP32 - Máquina Expendedora

[![ESP32](https://img.shields.io/badge/ESP32-8faa8b?style=for-the-badge&logo=espressif&logoColor=white)](#)
[![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](#)
[![PlatformIO](https://img.shields.io/badge/PlatformIO-F56600?style=for-the-badge&logo=platformio&logoColor=white)](#)
[![SQLite](https://img.shields.io/badge/SQLite-003B57?style=for-the-badge&logo=sqlite&logoColor=white)](#)

**ITIID 7-1 · Septiembre - Diciembre 2026**

*Docente: Dr. Said Polanco Martagón*

</div>

---

## Resumen

Este repositorio contiene el código fuente para el microcontrolador central (ESP32) de la máquina expendedora SAID. 
El firmware opera de manera independiente (sin conexión a Internet) y actúa como el cerebro de las operaciones del sistema, coordinándose mediante UART con el Arduino Mega.

### Funciones principales del firmware
- **Máquina de Estados (FSM):** Controlador central de negocio y secuencia de operación.
- **Servidor HTTP y SoftAP:** Generación de red local y alojamiento de la Interfaz Web administrativa.
- **Módulo de Datos:** Integración y consumo de la base de datos SQLite embebida.
- **Comunicación UART v2.0:** Contrato de mensajería asíncrona con el hardware esclavo (Mega).
- **RFID:** Lectura y autorización local de tarjetas para recargas y compras.

---

## Estructura del Proyecto

El repositorio sigue la convención estándar de **PlatformIO** para mantener una arquitectura limpia y facilitar el trabajo colaborativo:

| Directorio | Propósito |
| :--- | :--- |
| 📁 **`src/`** | Código fuente principal (`.cpp`, `.ino`). Punto de entrada `setup()` y `loop()`. |
| 📁 **`data/`** | Sistema de archivos Flash (LittleFS/SPIFFS). |
| 📄 **`data/www/`** | Recursos estáticos empaquetados de la App Web (HTML, CSS, JS). |
| 🗄️ **`data/db/`** | Archivo binario de la base de datos SQLite. |
| 📚 **`lib/`** | Librerías locales específicas del proyecto (ej. `UartParser`, `StateMachine`). |
| ⚙️ **`include/`** | Archivos de cabecera (`.h`) para configuraciones globales y constantes. |

---

## Subgrupo ESP32

El desarrollo de este firmware está a cargo de:

* **Diego Eduardo Zapata Aguilar:** Máquina de estados ejecutable, UART v2, secuencias y reconciliación.
* **Junior Arturo Vázquez Leonel:** Integración del módulo de datos (SQLite), servidor HTTP, SoftAP y recursos de Web.
* **Jared de Jesus Olazaran Lopez:** RFID, autenticación administrativa, PIN, recargas y flujo de mantenimiento.

---

<div align="center">

**Universidad Politécnica de Victoria · ITIID 7-1 · Septiembre - Diciembre 2026**

</div>