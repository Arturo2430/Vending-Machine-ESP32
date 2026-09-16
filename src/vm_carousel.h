/**
 * @file vm_carousel.h
 * @brief Gestor no bloqueante de rotación de pantallas en el LCD 16×2.
 *
 * Permite cargar N subpantallas (cada una con dos líneas de texto) y
 * rotarlas automáticamente cada VM_CAROUSEL_INTERVAL_MS, o avanzar
 * manualmente con advance().  Emite la actualización llamando a
 * VmEsp32Controller::updateDisplay() de la instancia registrada.
 *
 * Uso típico:
 *   carousel.clear();
 *   carousel.addSlide("  SAID VENDING  ", "  Selec 1-4 OK  ");
 *   carousel.addSlide("Slot 1 $18.00   ", "Stock: 8        ");
 *   carousel.start();
 *   // En loop():
 *   carousel.update();
 */

#ifndef VM_CAROUSEL_H
#define VM_CAROUSEL_H

#include <Arduino.h>
#include "vm_board_config.h"

// Tamaño máximo de la línea LCD (excluye terminador nulo).
static constexpr uint8_t LCD_LINE_LEN = 16;

// Número máximo de subpantallas por carrusel.
static constexpr uint8_t CAROUSEL_MAX_SLIDES = 6;

// Firma del callback para actualizar el display (wrappea updateDisplay del controller).
typedef void (*DisplayCallback)(const char* line1, const char* line2);

// ---------------------------------------------------------------------------

struct Slide {
    char line1[LCD_LINE_LEN + 1];
    char line2[LCD_LINE_LEN + 1];
};

class VmCarousel {
public:
    explicit VmCarousel(DisplayCallback callback);

    /** Elimina todas las subpantallas y detiene la rotación. */
    void clear();

    /**
     * @brief Agrega una subpantalla.
     *
     * Las cadenas se recortan o rellenan con espacios a exactamente 16 chars.
     * @return false si ya se alcanzó CAROUSEL_MAX_SLIDES.
     */
    bool addSlide(const char* line1, const char* line2);

    /** Fuerza la emisión de la primera subpantalla e inicia el temporizador. */
    void start();

    /** Avanza inmediatamente a la siguiente subpantalla (tecla 'A'). */
    void advance();

    /**
     * @brief Debe llamarse en cada iteración de loop().
     *
     * Si el intervalo expiró, pasa a la siguiente subpantalla y emite el
     * display.  No bloquea.
     */
    void update();

    /** Retorna el índice de la subpantalla actualmente visible. */
    uint8_t currentIndex() const { return _current; }

    /** Número de subpantallas cargadas. */
    uint8_t count() const { return _count; }

private:
    DisplayCallback _callback;
    Slide           _slides[CAROUSEL_MAX_SLIDES];
    uint8_t         _count;
    uint8_t         _current;
    unsigned long   _lastChange;
    bool            _running;

    void emitCurrent();
    static void padLine(const char* src, char* dst);
};

#endif // VM_CAROUSEL_H
