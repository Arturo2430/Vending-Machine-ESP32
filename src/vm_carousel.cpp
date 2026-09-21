/**
 * @file vm_carousel.cpp
 * @brief Implementación del gestor de carrusel LCD no bloqueante.
 */

#include "vm_carousel.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

VmCarousel::VmCarousel(DisplayCallback callback)
    : _callback(callback),
      _count(0),
      _current(0),
      _lastChange(0),
      _running(false) {}

// ---------------------------------------------------------------------------
// API pública
// ---------------------------------------------------------------------------

void VmCarousel::clear() {
    _count   = 0;
    _current = 0;
    _running = false;
}

bool VmCarousel::addSlide(const char* line1, const char* line2, const char* line3, const char* line4) {
    if (_count >= CAROUSEL_MAX_SLIDES) return false;
    padLine(line1, _slides[_count].line1);
    padLine(line2, _slides[_count].line2);
    padLine(line3, _slides[_count].line3);
    padLine(line4, _slides[_count].line4);
    _count++;
    return true;
}

void VmCarousel::start() {
    if (_count == 0) return;
    _current    = 0;
    _lastChange = millis();
    _running    = true;
    emitCurrent();
}

void VmCarousel::advance() {
    if (!_running || _count == 0) return;
    _current    = (_current + 1) % _count;
    _lastChange = millis();
    emitCurrent();
}

void VmCarousel::update() {
    if (!_running || _count <= 1) return;
    if ((millis() - _lastChange) >= VM_CAROUSEL_INTERVAL_MS) {
        advance();
    }
}

// ---------------------------------------------------------------------------
// Helpers privados
// ---------------------------------------------------------------------------

void VmCarousel::emitCurrent() {
    if (_callback && _count > 0) {
        _callback(_slides[_current].line1, _slides[_current].line2, _slides[_current].line3, _slides[_current].line4);
    }
}

void VmCarousel::padLine(const char* src, char* dst) {
    // Rellenar con espacios a LCD_LINE_LEN y añadir terminador.
    uint8_t i = 0;
    if (src) {
        while (i < LCD_LINE_LEN && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    while (i < LCD_LINE_LEN) dst[i++] = ' ';
    dst[LCD_LINE_LEN] = '\0';
}
