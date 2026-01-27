/**
 * PICOMIMI Display Driver Interface
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Apps use Pico_Display*() functions, which call into the registered driver.
 * Display drivers implement this interface and register with Picomimi_RegisterDisplay().
 */
#ifndef PICOMIMI_DISPLAY_H
#define PICOMIMI_DISPLAY_H

#include "config/picomimi_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// COMMON COLOR DEFINITIONS (RGB565)
// ============================================================================

#define PM_COLOR_BLACK       0x0000
#define PM_COLOR_WHITE       0xFFFF
#define PM_COLOR_RED         0xF800
#define PM_COLOR_GREEN       0x07E0
#define PM_COLOR_BLUE        0x001F
#define PM_COLOR_CYAN        0x07FF
#define PM_COLOR_MAGENTA     0xF81F
#define PM_COLOR_YELLOW      0xFFE0
#define PM_COLOR_ORANGE      0xFD20
#define PM_COLOR_GRAY        0x8410
#define PM_COLOR_DARKGRAY    0x4208

// Convert RGB888 to RGB565
#define PM_RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

// ============================================================================
// DISPLAY DRIVER STRUCTURE
// ============================================================================

typedef struct pm_display_driver {
    // Display info
    uint16_t width;
    uint16_t height;
    uint8_t bpp;              // Bits per pixel (typically 16 for RGB565)
    uint8_t rotation;         // Current rotation (0-3)
    bool initialized;
    bool sleeping;
    
    // Core drawing functions (driver must implement these)
    void (*init)(void);
    void (*deinit)(void);
    void (*clear)(uint16_t color);
    void (*set_pixel)(int16_t x, int16_t y, uint16_t color);
    void (*fill_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void (*draw_char)(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
    void (*draw_text)(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t bg, uint8_t size);
    void (*draw_line)(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void (*draw_rect)(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void (*draw_circle)(int16_t x, int16_t y, int16_t r, uint16_t color);
    void (*fill_circle)(int16_t x, int16_t y, int16_t r, uint16_t color);
    void (*draw_bitmap)(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t color);
    void (*draw_rgb_bitmap)(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h);
    
    // Display control
    void (*flush)(void);              // For buffered displays
    void (*set_rotation)(uint8_t r);  // 0-3 for 0°, 90°, 180°, 270°
    void (*set_brightness)(uint8_t b);// 0-255
    void (*invert)(bool invert);      // Invert colors
    void (*sleep)(void);              // Enter sleep mode
    void (*wake)(void);               // Wake from sleep
    
    // Advanced (optional)
    void (*set_window)(int16_t x, int16_t y, int16_t w, int16_t h);
    void (*write_pixels)(const uint16_t* data, uint32_t len);
    void (*scroll)(int16_t dx, int16_t dy);
} pm_display_driver_t;

// ============================================================================
// DISPLAY DRIVER REGISTRATION
// ============================================================================

/**
 * Register a display driver with Picomimi
 * @param driver Pointer to driver structure (must persist)
 */
void Picomimi_RegisterDisplay(pm_display_driver_t* driver);

/**
 * Get the currently registered display driver
 * @return Driver pointer or NULL if none registered
 */
pm_display_driver_t* Picomimi_GetDisplay(void);

/**
 * Check if a display is available
 */
bool Picomimi_HasDisplay(void);

// ============================================================================
// APP-FACING DISPLAY API (Pico_Display*)
// ============================================================================

// Initialization
void Pico_DisplayInit(void);
void Pico_DisplayDeinit(void);

// Basic drawing
void Pico_DisplayClear(uint16_t color);
void Pico_DisplayPixel(int16_t x, int16_t y, uint16_t color);
void Pico_DisplayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void Pico_DisplayRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void Pico_DisplayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void Pico_DisplayCircle(int16_t x, int16_t y, int16_t r, uint16_t color);
void Pico_DisplayFillCircle(int16_t x, int16_t y, int16_t r, uint16_t color);

// Text
void Pico_DisplayChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void Pico_DisplayText(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t bg, uint8_t size);
void Pico_DisplayPrintf(int16_t x, int16_t y, uint16_t color, uint16_t bg, uint8_t size, const char* fmt, ...);

// Bitmaps
void Pico_DisplayBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t color);
void Pico_DisplayRGBBitmap(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h);

// Control
void Pico_DisplayFlush(void);
void Pico_DisplayRotation(uint8_t r);
void Pico_DisplayBrightness(uint8_t b);
void Pico_DisplaySleep(void);
void Pico_DisplayWake(void);
void Pico_DisplayInvert(bool invert);

// Info
uint16_t Pico_DisplayWidth(void);
uint16_t Pico_DisplayHeight(void);
uint8_t Pico_DisplayRotation(void);
bool Pico_DisplayReady(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_DISPLAY_H
