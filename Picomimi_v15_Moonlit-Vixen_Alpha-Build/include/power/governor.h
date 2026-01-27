/**
 * PICOMIMI Governor Header (Simplified)
 */
#ifndef PICOMIMI_GOVERNOR_H
#define PICOMIMI_GOVERNOR_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// LOAD THRESHOLDS
// ============================================================================

#define PM_LOAD_VERY_LOW        20
#define PM_LOAD_LOW             35
#define PM_LOAD_MEDIUM          50
#define PM_LOAD_HIGH            70
#define PM_LOAD_VERY_HIGH       85

// ============================================================================
// GOVERNOR STATISTICS
// ============================================================================

typedef struct {
    uint32_t time_in_profile[CPU_PROFILE_COUNT];
    uint32_t transitions;
    uint32_t thermal_throttles;
    uint32_t load_samples;
    float avg_temperature;
    float avg_load;
} pm_governor_stats_t;

// ============================================================================
// API FUNCTIONS
// ============================================================================

pm_result_t pm_governor_init(pm_governor_state_t* gov);
void pm_governor_deinit(pm_governor_state_t* gov);

pm_result_t pm_governor_set_mode(pm_governor_state_t* gov, pm_governor_mode_t mode);
pm_governor_mode_t pm_governor_get_mode(pm_governor_state_t* gov);

pm_result_t pm_governor_set_profile(pm_governor_state_t* gov, pm_cpu_profile_t profile);
pm_cpu_profile_t pm_governor_get_profile(pm_governor_state_t* gov);

void pm_governor_tick(pm_governor_state_t* gov);
void pm_governor_update_load(pm_governor_state_t* gov, uint8_t load);

float pm_governor_read_temp(pm_governor_state_t* gov);
uint32_t pm_governor_get_freq(pm_governor_state_t* gov);
bool pm_governor_is_throttled(pm_governor_state_t* gov);

void pm_governor_get_stats(pm_governor_state_t* gov, pm_governor_stats_t* stats);
void pm_governor_reset_stats(pm_governor_state_t* gov);

const char* pm_governor_profile_name(pm_cpu_profile_t profile);

// Temperature reading
float pm_governor_read_temp(pm_governor_state_t* gov);

// Input boost for UI responsiveness
void pm_governor_input_boost(void);

// Turbo request
void pm_governor_request_turbo(uint32_t duration_ms);
void pm_governor_request_instant_turbo(void);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_GOVERNOR_H
