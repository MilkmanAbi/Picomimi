/**
 * PICOMIMI Display and Input Driver Implementation
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * This provides the driver registration and app-facing API.
 * Actual drivers (ST7789, ILI9341, etc.) implement the interfaces
 * and register with Picomimi_RegisterDisplay/Input().
 */

#include "drivers/display.h"
#include "drivers/input.h"
#include "api/picomimi_kernel.h"
#include "power/governor.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// External kernel state
extern pm_kernel_state_t g_kernel;

// ============================================================================
// DISPLAY DRIVER STATE
// ============================================================================

static pm_display_driver_t* g_display = NULL;

// ============================================================================
// DISPLAY DRIVER REGISTRATION
// ============================================================================

void Picomimi_RegisterDisplay(pm_display_driver_t* driver) {
    g_display = driver;
    
    if (driver && driver->init) {
        driver->init();
        driver->initialized = true;
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Display driver registered");
    }
}

pm_display_driver_t* Picomimi_GetDisplay(void) {
    return g_display;
}

bool Picomimi_HasDisplay(void) {
    return (g_display != NULL && g_display->initialized);
}

// ============================================================================
// APP-FACING DISPLAY API
// ============================================================================

void Pico_DisplayInit(void) {
    if (g_display && g_display->init) {
        g_display->init();
        g_display->initialized = true;
    }
}

void Pico_DisplayDeinit(void) {
    if (g_display && g_display->deinit) {
        g_display->deinit();
        g_display->initialized = false;
    }
}

void Pico_DisplayClear(uint16_t color) {
    if (g_display && g_display->clear) {
        g_display->clear(color);
    }
}

void Pico_DisplayPixel(int16_t x, int16_t y, uint16_t color) {
    if (g_display && g_display->set_pixel) {
        g_display->set_pixel(x, y, color);
    }
}

void Pico_DisplayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    if (g_display && g_display->draw_line) {
        g_display->draw_line(x0, y0, x1, y1, color);
    }
}

void Pico_DisplayRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_display && g_display->draw_rect) {
        g_display->draw_rect(x, y, w, h, color);
    } else if (g_display && g_display->draw_line) {
        // Fallback: draw 4 lines
        g_display->draw_line(x, y, x + w - 1, y, color);
        g_display->draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
        g_display->draw_line(x + w - 1, y + h - 1, x, y + h - 1, color);
        g_display->draw_line(x, y + h - 1, x, y, color);
    }
}

void Pico_DisplayFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (g_display && g_display->fill_rect) {
        g_display->fill_rect(x, y, w, h, color);
    }
}

void Pico_DisplayCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (g_display && g_display->draw_circle) {
        g_display->draw_circle(x, y, r, color);
    }
}

void Pico_DisplayFillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
    if (g_display && g_display->fill_circle) {
        g_display->fill_circle(x, y, r, color);
    }
}

void Pico_DisplayChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
    if (g_display && g_display->draw_char) {
        g_display->draw_char(x, y, c, color, bg, size);
    }
}

void Pico_DisplayText(int16_t x, int16_t y, const char* text, uint16_t color, uint16_t bg, uint8_t size) {
    if (g_display && g_display->draw_text) {
        g_display->draw_text(x, y, text, color, bg, size);
    }
}

void Pico_DisplayPrintf(int16_t x, int16_t y, uint16_t color, uint16_t bg, uint8_t size, const char* fmt, ...) {
    if (!g_display || !g_display->draw_text) return;
    
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    g_display->draw_text(x, y, buf, color, bg, size);
}

void Pico_DisplayBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, uint16_t color) {
    if (g_display && g_display->draw_bitmap) {
        g_display->draw_bitmap(x, y, bitmap, w, h, color);
    }
}

void Pico_DisplayRGBBitmap(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h) {
    if (g_display && g_display->draw_rgb_bitmap) {
        g_display->draw_rgb_bitmap(x, y, bitmap, w, h);
    }
}

void Pico_DisplayFlush(void) {
    if (g_display && g_display->flush) {
        g_display->flush();
    }
}

void Pico_DisplayRotation(uint8_t r) {
    if (g_display && g_display->set_rotation) {
        g_display->set_rotation(r);
        g_display->rotation = r;
    }
}

void Pico_DisplayBrightness(uint8_t b) {
    if (g_display && g_display->set_brightness) {
        g_display->set_brightness(b);
    }
}

void Pico_DisplaySleep(void) {
    if (g_display && g_display->sleep) {
        g_display->sleep();
        g_display->sleeping = true;
    }
}

void Pico_DisplayWake(void) {
    if (g_display && g_display->wake) {
        g_display->wake();
        g_display->sleeping = false;
    }
}

void Pico_DisplayInvert(bool invert) {
    if (g_display && g_display->invert) {
        g_display->invert(invert);
    }
}

uint16_t Pico_DisplayWidth(void) {
    return g_display ? g_display->width : 0;
}

uint16_t Pico_DisplayHeight(void) {
    return g_display ? g_display->height : 0;
}

uint8_t Pico_DisplayGetRotation(void) {
    return g_display ? g_display->rotation : 0;
}

bool Pico_DisplayReady(void) {
    return Picomimi_HasDisplay();
}

// ============================================================================
// INPUT DRIVER STATE
// ============================================================================

static pm_input_driver_t* g_input = NULL;
static pm_input_callback_t g_input_callback = NULL;

// GUI focus management
static pm_task_id_t g_gui_app_task_ids[PM_MAX_GUI_APPS];
static uint32_t g_gui_app_count = 0;
static int32_t g_current_gui_focus_index = -1;

// ============================================================================
// INPUT DRIVER REGISTRATION
// ============================================================================

void Picomimi_RegisterInput(pm_input_driver_t* driver) {
    g_input = driver;
    
    if (driver && driver->init) {
        driver->init();
        driver->initialized = true;
        pm_klog(PICOMIMI_LOG_LEVEL_INFO, "Input driver registered");
    }
}

pm_input_driver_t* Picomimi_GetInput(void) {
    return g_input;
}

bool Picomimi_HasInput(void) {
    return (g_input != NULL && g_input->initialized);
}

// ============================================================================
// APP-FACING INPUT API
// ============================================================================

void Pico_InputInit(void) {
    if (g_input && g_input->init) {
        g_input->init();
        g_input->initialized = true;
    }
}

void Pico_InputDeinit(void) {
    if (g_input && g_input->deinit) {
        g_input->deinit();
        g_input->initialized = false;
    }
}

bool Pico_InputPoll(pm_input_event_t* event) {
    if (!g_input || !g_input->poll || !event) return false;
    
    bool has_event = g_input->poll(event);
    
    if (has_event) {
        // Trigger input boost for UI responsiveness
        Picomimi_InputBoost();
        
        // Call user callback if registered
        if (g_input_callback) {
            g_input_callback(event);
        }
    }
    
    return has_event;
}

bool Pico_InputPeek(pm_input_event_t* event) {
    // For now, same as poll (would need queue for proper peek)
    return Pico_InputPoll(event);
}

void Pico_InputFlush(void) {
    pm_input_event_t event;
    while (Pico_InputPoll(&event)) {
        // Discard events
    }
}

bool Pico_ButtonPressed(uint8_t id) {
    if (!g_input || !g_input->button_pressed) return false;
    return g_input->button_pressed(id);
}

bool Pico_ButtonHeld(uint8_t id) {
    if (!g_input || !g_input->button_held) return false;
    return g_input->button_held(id);
}

bool Pico_AnyButtonPressed(void) {
    if (!g_input || !g_input->button_pressed) return false;
    
    for (uint8_t i = 0; i < g_input->num_buttons; i++) {
        if (g_input->button_pressed(i)) return true;
    }
    return false;
}

uint8_t Pico_GetPressedButtons(void) {
    if (!g_input || !g_input->button_pressed) return 0;
    
    uint8_t mask = 0;
    for (uint8_t i = 0; i < g_input->num_buttons && i < 8; i++) {
        if (g_input->button_pressed(i)) {
            mask |= (1 << i);
        }
    }
    return mask;
}

bool Pico_TouchPressed(void) {
    if (!g_input || !g_input->has_touch || !g_input->touch_pressed) return false;
    return g_input->touch_pressed();
}

void Pico_GetTouch(int16_t* x, int16_t* y) {
    if (!g_input || !g_input->has_touch || !g_input->get_touch) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }
    g_input->get_touch(x, y);
}

uint16_t Pico_GetTouchPressure(void) {
    if (!g_input || !g_input->has_touch || !g_input->get_pressure) return 0;
    return g_input->get_pressure();
}

int32_t Pico_GetEncoder(void) {
    if (!g_input || !g_input->has_encoder || !g_input->get_encoder) return 0;
    return g_input->get_encoder();
}

void Pico_ResetEncoder(void) {
    if (g_input && g_input->has_encoder && g_input->reset_encoder) {
        g_input->reset_encoder();
    }
}

void Pico_SetInputCallback(pm_input_callback_t cb) {
    g_input_callback = cb;
    
    if (g_input && g_input->set_callback) {
        g_input->set_callback(cb);
    }
}

uint8_t Pico_InputNumButtons(void) {
    return g_input ? g_input->num_buttons : 0;
}

bool Pico_InputHasTouch(void) {
    return (g_input && g_input->has_touch);
}

bool Pico_InputHasEncoder(void) {
    return (g_input && g_input->has_encoder);
}

bool Pico_InputReady(void) {
    return Picomimi_HasInput();
}

// ============================================================================
// GUI FOCUS MANAGEMENT
// ============================================================================

void Picomimi_RegisterGUIApp(pm_task_id_t task_id) {
    if (g_gui_app_count >= PM_MAX_GUI_APPS) return;
    
    // Check if already registered
    for (uint32_t i = 0; i < g_gui_app_count; i++) {
        if (g_gui_app_task_ids[i] == task_id) return;
    }
    
    g_gui_app_task_ids[g_gui_app_count++] = task_id;
    
    // Give focus to first registered app
    if (g_current_gui_focus_index < 0) {
        g_current_gui_focus_index = 0;
        g_kernel.gui_focus_task_id = task_id;
    }
}

bool Pico_RequestFocus(void) {
    pm_task_id_t current = g_kernel.current_task;
    
    // Check if current task is a GUI app
    for (uint32_t i = 0; i < g_gui_app_count; i++) {
        if (g_gui_app_task_ids[i] == current) {
            g_current_gui_focus_index = i;
            g_kernel.gui_focus_task_id = current;
            return true;
        }
    }
    
    return false;
}

void Pico_ReleaseFocus(void) {
    pm_task_id_t current = g_kernel.current_task;
    
    if (g_kernel.gui_focus_task_id == current) {
        g_kernel.gui_focus_task_id = -1;
        g_current_gui_focus_index = -1;
    }
}

bool Pico_HasFocus(void) {
    return (g_kernel.gui_focus_task_id == (int32_t)g_kernel.current_task);
}

pm_task_id_t Picomimi_GetFocusTask(void) {
    if (g_current_gui_focus_index < 0) return PM_INVALID_TASK;
    return g_gui_app_task_ids[g_current_gui_focus_index];
}

void Picomimi_CycleFocus(void) {
    if (g_gui_app_count == 0) return;
    
    g_current_gui_focus_index = (g_current_gui_focus_index + 1) % g_gui_app_count;
    g_kernel.gui_focus_task_id = g_gui_app_task_ids[g_current_gui_focus_index];
}

// ============================================================================
// INPUT BOOST (GOVERNOR INTEGRATION)
// ============================================================================

void Picomimi_InputBoost(void) {
    pm_governor_input_boost();
}

// ============================================================================
// INPUT TASK (called periodically from kernel)
// ============================================================================

void pm_input_task(void* arg) {
    (void)arg;
    
    if (!g_input || !g_input->initialized) {
        pm_task_sleep(100);
        return;
    }
    
    // Poll for input events
    pm_input_event_t event;
    while (Pico_InputPoll(&event)) {
        // Events are handled via callback
    }
    
    pm_task_sleep(10);  // Poll every 10ms
}
