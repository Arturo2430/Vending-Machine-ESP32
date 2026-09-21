# Guía de Integración UART (Arduino Mega) — v2.1.0

Esta guía describe cómo implementar el firmware del Arduino Mega 2560 para comunicarse con el ESP32 utilizando el protocolo **UART Vending Machine v2.1.0**.

> **IMPORTANTE v2.1**: La arquitectura cambió. El lector RFID, el LCD (ahora 20x4 I2C), y los motores DC con drivers están controlados íntegramente por el Arduino Mega.

## 1. Conexiones Físicas Requeridas

| Componente | Conexión en Arduino Mega | Notas |
|:---|:---|:---|
| **Enlace UART a ESP32** | `TX1 (D18) -> RX2 (GPIO16)`<br>`RX1 (D19) <- TX2 (GPIO17)` | **CRÍTICO:** Usar divisor resistivo (ej. 2k/1k) de TX1(Mega) a RX2(ESP32) para reducir 5V a 3.3V. |
| **Teclado 4x4** | Filas: `D22, D23, D24, D25`<br>Columnas: `D26, D27, D28, D29` | Confirmar pines exactos de columnas en el cableado final. |
| **Lector RFID (RC522)**| `MISO=D50, MOSI=D51, SCK=D52`<br>`SS=D53, RST=D8` | El Mega ahora envía las lecturas vía UART (CMD `0x14`). |
| **Sensor de Caída** | `TRIG=D9, ECHO=D10` | HC-SR04 reemplaza a la barrera óptica. |
| **LCD 20x4 I2C** | `SDA=D20, SCL=D21` (I2C default) | Dirección típica `0x27`. |
| **Driver Motores** | `SDA=D20, SCL=D21` (PCA9685) | Dirección típica `0x40`. PWM a 1000 Hz. Controla los puentes H (DRV8833). |

## 2. Parámetros de UART

- **Puerto:** `Serial1`
- **Baudrate:** `38400`
- **Formato:** `8N1` (8 data bits, no parity, 1 stop bit)

## 3. Resumen del Protocolo v2.1.0

Cada trama binaria tiene el siguiente formato (Half-Duplex implícito):

```
[0] SOF1     (0xA5)
[1] SOF2     (0x5A)
[2] VER      (0x02)
[3] CMD      (El comando)
[4] SEQ      (Secuencia correlativa 0-255)
[5] LEN      (Longitud del Payload, 0 a 80)
[6..N] PAYLOAD (LEN bytes)
```

### 3.1 Comandos que el Mega *Recibe* y *Ejecuta*

1. **`HELLO (0x01)`**: El ESP32 inicia el handshake. Responder con `HELLO` y rol=1.
2. **`DISPLAY (0x11)`**: El ESP32 ordena qué mostrar en el LCD.
   - **LEN**: 80
   - **Payload**: 4 líneas consecutivas de 20 caracteres EXACTOS sin nulos.
   - **Acción**: Volcar la línea 1 en `lcd.setCursor(0,0)`, la 2 en `(0,1)`, etc. Responder con `ACK`.
3. **`VEND (0x12)`**: El ESP32 ordena girar un motor.
   - **Payload**: `[Canal (1 byte)] + [TransactionID (4 bytes, Little Endian)]`
   - **Acción**: Responder `ACK(RECEIVED)` inmediato. Luego encender el motor correspondiente en el PCA9685, esperar confirmación del HC-SR04, apagar motor, y **finalmente** enviar `RESULT`.
4. **`STATUS (0x03) Request`**: El ESP32 pregunta estado de sensores.
   - **LEN**: 0
   - **Acción**: Responder enviando `STATUS (0x03)` (LEN=7).

### 3.2 Comandos que el Mega *Envía* (Eventos)

1. **`KEY (0x10)`**: Cuando se pulsa una tecla en el Keypad 4x4.
   - **Payload**: `[KeyAscii (1 byte)] + [Sequence (1 byte)]`.
2. **`RFID_CARD (0x14)`**: (NUEVO v2.1) Cuando el RC522 detecta tarjeta.
   - **Payload**: UID de la tarjeta en cadena Hexadecimal ASCII mayúscula. (Ej: 'A1B2C3D4').
   - **Condición**: Inmediatamente tras leer la tarjeta, se debe invocar `PICC_HaltA()` y `PCD_StopCrypto1()` en la librería MFRC522, y luego enviar este comando.
3. **`RESULT (0x13)`**: Finalización de la orden de giro del motor.
   - **Payload**: `[TransactionID (4 bytes)] + [Resultado (1 byte)]`. (Resultado: 0=Entregado, 1=Rechazado previo, 2=Incierto/Atascado).

## 4. Estructura Sugerida para el Sketch del Mega

```cpp
void setup() {
    Serial1.begin(38400);  // Enlace UART
    lcd.begin(20, 4);      // LCD I2C
    pwm.begin();           // PCA9685
    pwm.setPWMFreq(1000);
    SPI.begin();           // RFID
    mfrc522.PCD_Init();
    // ... pines ultrasonido
}

void loop() {
    pollUART();      // Leer Serial1 y parsear tramas (no bloqueante)
    pollKeypad();    // Leer Keypad, enviar CMD_KEY si hay tecla
    pollRFID();      // Leer RC522, enviar CMD_RFID_CARD si hay tarjeta
    updateMotor();   // Apagar motor tras timeout o detección ultrasónica
}
```

> **Advertencia:** Queda terminantemente prohibido usar `delay()` en el `loop()`. El sondeo UART debe ser capaz de procesar 86 bytes en milisegundos para no saturar el buffer del Arduino.
