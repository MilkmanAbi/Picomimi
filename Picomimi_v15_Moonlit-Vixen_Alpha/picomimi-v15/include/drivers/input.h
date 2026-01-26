/**
 * PICOMIMI Input Driver Interface
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Apps use Pico_Input*() functions, which call into the registered driver.
 * Input drivers implement this interface and register with Picomimi_RegisterInput().
 */
#ifndef PICOMIMI_INPUT_H
#define PICOMIMI_INPUT_H

#include "config/picomimi_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// INPUT EVENT TYPES
// ============================================================================

typedef enum {
    PM_INPUT_NONE = 0,
    PM_INPUT_BUTTON_DOWN,     // Button pressed
    PM_INPUT_BUTTON_UP,       // Button released
    PM_INPUT_BUTTON_HOLD,     // Button held (repeated)
    PM_INPUT_BUTTON_CLICK,    // Short click
    PM_INPUT_BUTTON_DOUBLE,   // Double click
    PM_INPUT_BUTTON_LONG,     // Long press
    PM_INPUT_TOUCH_DOWN,      // Touch started
    PM_INPUT_TOUCH_UP,        // Touch ended
    PM_INPUT_TOUCH_MOVE,      // Touch moved
    PM_INPUT_ENCODER_CW,      // Rotary encoder clockwise
    PM_INPUT_ENCODER_CCW,     // Rotary encoder counter-clockwise
    PM_INPUT_ENCODER_CLICK,   // Encoder button click
    PM_INPUT_GESTURE_SWIPE_UP,
    PM_INPUT_GESTURE_SWIPE_DOWN,
    PM_INPUT_GESTURE_SWIPE_LEFT,
    PM_INPUT_GESTURE_SWIPE_RIGHT,
    PM_INPUT_GESTURE_TAP,
    PM_INPUT_GESTURE_DOUBLE_TAP,
    PM_INPUT_GESTURE_PINCH,
    PM_INPUT_GESTURE_ZOOM
} pm_input_type_t;

// ============================================================================
// INPUT EVENT STRUCTURE
// ============================================================================

typedef struct {
    pm_input_type_t type;     // Event type
    uint8_t button_id;        // Which button (0-7)
    uint8_t gesture_id;       // Gesture identifier
    uint8_t _reserved;
    int16_t x;                // Touch X coordinate
    int16_t y;                // Touch Y coordinate
    int16_t dx;               // Delta X (for move/swipe)
    int16_t dy;               // Delta Y (for move/swipe)
    uint32_t timestamp;       // Event timestamp (ms)
    uint16_t pressure;        // Touch pressure (if supported)
    uint16_t duration;        // Press duration (ms)
} pm_input_event_t;

// ============================================================================
// INPUT CALLBACK TYPE
// ============================================================================

typedef void (*pm_input_callback_t)(pm_input_event_t* event);

// ============================================================================
// INPUT DRIVER STRUCTURE
// ============================================================================

typedef struct pm_input_driver {
    // Driver info
    bool initialized;
    uint8_t num_buttons;      // Number of buttons (0-8)
    bool has_touch;           // Has touch screen
    bool has_encoder;         // Has rotary encoder
    bool has_gestures;        // Supports gesture detection
    
    // Button names (optional)
    const char* button_names[PICOMIMI_MAX_INPUT_BUTTONS];
    
    // Core functions (driver must implement these)
    void (*init)(void);
    void (*deinit)(void);
    bool (*poll)(pm_input_event_t* event);    // Returns true if event available
    bool (*button_pressed)(uint8_t id);       // Direct button state (true = pressed)
    bool (*button_held)(uint8_t id);          // True if held for >500ms
    
    // Touch functions (if has_touch)
    bool (*touch_pressed)(void);              // True if screen touched
    void (*get_touch)(int16_t* x, int16_t* y);// Get current touch coordinates
    uint16_t (*get_pressure)(void);           // Get touch pressure
    
    // Encoder functions (if has_encoder)
    int32_t (*get_encoder)(void);             // Get encoder position
    void (*reset_encoder)(void);              // Reset encoder to 0
    
    // Callback registration
    void (*set_callback)(pm_input_callback_t cb);
    
    // Advanced
    void (*set_debounce)(uint8_t ms);         // Set button debounce time
    void (*set_hold_time)(uint16_t ms);       // Time for hold detection
    void (*enable_gestures)(bool enable);     // Enable/disable gesture detection
    void (*sleep)(void);                      // Enter low-power mode
    void (*wake)(void);                       // Wake from low-power mode
} pm_input_driver_t;

// ============================================================================
// INPUT DRIVER REGISTRATION
// ============================================================================

/**
 * Register an input driver with Picomimi
 * @param driver Pointer to driver structure (must persist)
 */
void Picomimi_RegisterInput(pm_input_driver_t* driver);

/**
 * Get the currently registered input driver
 * @return Driver pointer or NULL if none registered
 */
pm_input_driver_t* Picomimi_GetInput(void);

/**
 * Check if input is available
 */
bool Picomimi_HasInput(void);

// ============================================================================
// APP-FACING INPUT API (Pico_Input*)
// ============================================================================

// Initialization
void Pico_InputInit(void);
void Pico_InputDeinit(void);

// Event polling
bool Pico_InputPoll(pm_input_event_t* event);
bool Pico_InputPeek(pm_input_event_t* event);
void Pico_InputFlush(void);

// Button state
bool Pico_ButtonPressed(uint8_t id);
bool Pico_ButtonHeld(uint8_t id);
bool Pico_AnyButtonPressed(void);
uint8_t Pico_GetPressedButtons(void);  // Bitmask of pressed buttons

// Touch state
bool Pico_TouchPressed(void);
void Pico_GetTouch(int16_t* x, int16_t* y);
uint16_t Pico_GetTouchPressure(void);

// Encoder
int32_t Pico_GetEncoder(void);
void Pico_ResetEncoder(void);

// Callback
void Pico_SetInputCallback(pm_input_callback_t cb);

// Info
uint8_t Pico_InputNumButtons(void);
bool Pico_InputHasTouch(void);
bool Pico_InputHasEncoder(void);
bool Pico_InputReady(void);

// ============================================================================
// GUI FOCUS MANAGEMENT
// ============================================================================

#define PM_MAX_GUI_APPS 4

/**
 * Register a task as a GUI app (for focus management)
 */
void Picomimi_RegisterGUIApp(pm_task_id_t task_id);

/**
 * Request GUI focus for current task
 * @return true if focus granted
 */
bool Pico_RequestFocus(void);

/**
 * Release GUI focus
 */
void Pico_ReleaseFocus(void);

/**
 * Check if current task has focus
 */
bool Pico_HasFocus(void);

/**
 * Get task ID with current focus
 * @return Task ID or PM_INVALID_TASK if none
 */
pm_task_id_t Picomimi_GetFocusTask(void);

/**
 * Cycle focus to next GUI app
 */
void Picomimi_CycleFocus(void);

// ============================================================================
// INPUT BOOST (for governor integration)
// ============================================================================

/**
 * Called on input events to boost CPU for UI responsiveness
 */
void Picomimi_InputBoost(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_INPUT_H
