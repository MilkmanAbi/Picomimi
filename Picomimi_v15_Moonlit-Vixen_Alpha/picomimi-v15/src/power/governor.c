/**
 * PICOMIMI Governor Implementation (Simplified)
 */
#include "power/governor.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"

// Frequency table (Hz)
static const uint32_t freq_table[CPU_PROFILE_COUNT] = {
    PICOMIMI_FREQ_ULTRA_LOW,
    PICOMIMI_FREQ_POWERSAVE,
    PICOMIMI_FREQ_BALANCED,
    PICOMIMI_FREQ_PERFORMANCE,
    PICOMIMI_FREQ_TURBO,
};

static const char* profile_names[CPU_PROFILE_COUNT] = {
    "ULTRA_LOW",
    "POWERSAVE",
    "BALANCED",
    "PERFORMANCE",
    "TURBO",
};

// Internal functions
static float read_temperature(void) {
    adc_select_input(4);  // Temperature sensor
    uint16_t raw = adc_read();
    float voltage = raw * 3.3f / 4096.0f;
    return 27.0f - (voltage - 0.706f) / 0.001721f;
}

static void apply_frequency(pm_cpu_profile_t profile) {
    if (profile >= CPU_PROFILE_COUNT) return;
    
    uint32_t freq_hz = freq_table[profile];
    
    // Set voltage first if increasing frequency
    if (profile >= CPU_PROFILE_PERFORMANCE) {
        vreg_set_voltage(VREG_VOLTAGE_1_10);
        sleep_ms(1);
    }
    
    // Set frequency (convert to kHz)
    set_sys_clock_khz(freq_hz / 1000, true);
    
    // Lower voltage if decreasing
    if (profile <= CPU_PROFILE_BALANCED) {
        vreg_set_voltage(VREG_VOLTAGE_DEFAULT);
    }
}

static pm_cpu_profile_t profile_from_load(uint8_t load) {
    if (load >= PM_LOAD_VERY_HIGH) return CPU_PROFILE_TURBO;
    if (load >= PM_LOAD_HIGH) return CPU_PROFILE_PERFORMANCE;
    if (load >= PM_LOAD_MEDIUM) return CPU_PROFILE_BALANCED;
    if (load >= PM_LOAD_LOW) return CPU_PROFILE_POWERSAVE;
    return CPU_PROFILE_ULTRA_LOW;
}

// ============================================================================
// PUBLIC API
// ============================================================================

pm_result_t pm_governor_init(pm_governor_state_t* gov) {
    if (!gov) return PM_ERROR_INVALID;
    
    gov->mode = GOV_MODE_ONDEMAND;
    gov->current_profile = CPU_PROFILE_BALANCED;
    gov->requested_profile = CPU_PROFILE_BALANCED;
    gov->min_profile = CPU_PROFILE_ULTRA_LOW;
    gov->max_profile = CPU_PROFILE_TURBO;
    
    gov->current_freq_khz = PICOMIMI_FREQ_BALANCED / 1000;
    gov->target_freq_khz = PICOMIMI_FREQ_BALANCED / 1000;
    
    gov->temperature = read_temperature();
    gov->temperature_peak = gov->temperature;
    gov->cpu_load = 0;
    gov->cpu_load_avg = 0;
    
    gov->check_interval_ms = PICOMIMI_GOV_CHECK_INTERVAL_MS;
    gov->last_check_ms = 0;
    gov->transition_count = 0;
    gov->throttle_count = 0;
    
    gov->enabled = true;
    gov->thermal_throttled = false;
    gov->turbo_available = true;
    
    apply_frequency(gov->current_profile);
    
    return PM_OK;
}

void pm_governor_deinit(pm_governor_state_t* gov) {
    if (gov) {
        gov->enabled = false;
        apply_frequency(CPU_PROFILE_BALANCED);
    }
}

pm_result_t pm_governor_set_mode(pm_governor_state_t* gov, pm_governor_mode_t mode) {
    if (!gov) return PM_ERROR_INVALID;
    gov->mode = mode;
    return PM_OK;
}

pm_governor_mode_t pm_governor_get_mode(pm_governor_state_t* gov) {
    return gov ? gov->mode : GOV_MODE_DISABLED;
}

pm_result_t pm_governor_set_profile(pm_governor_state_t* gov, pm_cpu_profile_t profile) {
    if (!gov || profile >= CPU_PROFILE_COUNT) return PM_ERROR_INVALID;
    
    gov->requested_profile = profile;
    
    if (gov->mode == GOV_MODE_MANUAL) {
        gov->current_profile = profile;
        apply_frequency(profile);
        gov->transition_count++;
    }
    
    return PM_OK;
}

pm_cpu_profile_t pm_governor_get_profile(pm_governor_state_t* gov) {
    return gov ? gov->current_profile : CPU_PROFILE_BALANCED;
}

void pm_governor_tick(pm_governor_state_t* gov) {
    if (!gov || !gov->enabled) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - gov->last_check_ms < gov->check_interval_ms) return;
    gov->last_check_ms = now;
    
    // Read temperature
    gov->temperature = read_temperature();
    if (gov->temperature > gov->temperature_peak) {
        gov->temperature_peak = gov->temperature;
    }
    
    // Thermal throttling
    if (gov->temperature > PICOMIMI_THERMAL_LIMIT) {
        if (!gov->thermal_throttled) {
            gov->thermal_throttled = true;
            gov->throttle_count++;
        }
        if (gov->current_profile > CPU_PROFILE_POWERSAVE) {
            gov->current_profile = (pm_cpu_profile_t)(gov->current_profile - 1);
            apply_frequency(gov->current_profile);
            gov->transition_count++;
        }
        return;
    } else {
        gov->thermal_throttled = false;
    }
    
    // Skip if not in auto mode
    if (gov->mode != GOV_MODE_ONDEMAND) return;
    
    // Determine target profile based on load
    pm_cpu_profile_t target = profile_from_load(gov->cpu_load);
    
    // Clamp to min/max
    if (target < gov->min_profile) target = gov->min_profile;
    if (target > gov->max_profile) target = gov->max_profile;
    
    // Apply if changed
    if (target != gov->current_profile) {
        gov->current_profile = target;
        apply_frequency(target);
        gov->transition_count++;
    }
    
    gov->current_freq_khz = freq_table[gov->current_profile] / 1000;
}

void pm_governor_update_load(pm_governor_state_t* gov, uint8_t load) {
    if (!gov) return;
    gov->cpu_load = load;
    gov->cpu_load_avg = (uint8_t)((gov->cpu_load_avg * 7 + load) / 8);
}

float pm_governor_read_temp(pm_governor_state_t* gov) {
    if (!gov) return 0.0f;
    gov->temperature = read_temperature();
    return gov->temperature;
}

uint32_t pm_governor_get_freq(pm_governor_state_t* gov) {
    return gov ? gov->current_freq_khz : 0;
}

bool pm_governor_is_throttled(pm_governor_state_t* gov) {
    return gov ? gov->thermal_throttled : false;
}

void pm_governor_get_stats(pm_governor_state_t* gov, pm_governor_stats_t* stats) {
    if (!gov || !stats) return;
    stats->transitions = gov->transition_count;
    stats->thermal_throttles = gov->throttle_count;
    stats->avg_temperature = gov->temperature;
    stats->avg_load = gov->cpu_load_avg;
}

void pm_governor_reset_stats(pm_governor_state_t* gov) {
    if (!gov) return;
    gov->transition_count = 0;
    gov->throttle_count = 0;
    gov->temperature_peak = gov->temperature;
}

const char* pm_governor_profile_name(pm_cpu_profile_t profile) {
    if (profile >= CPU_PROFILE_COUNT) return "UNKNOWN";
    return profile_names[profile];
}

// ============================================================================
// INPUT BOOST AND TURBO REQUESTS
// ============================================================================

void pm_governor_input_boost(void) {
    pm_governor_state_t* gov = &g_kernel.governor;
    
    if (!gov->enabled) return;
    if (gov->mode != GOV_MODE_ONDEMAND) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    // Extend existing boost
    if (gov->input_boost_active) {
        gov->input_boost_start_ms = now;
        return;
    }
    
    // Start new boost
    gov->input_boost_active = true;
    gov->input_boost_start_ms = now;
    
    // Boost to at least BALANCED
    if (gov->current_profile < CPU_PROFILE_BALANCED) {
        apply_profile(CPU_PROFILE_BALANCED);
        gov->current_profile = CPU_PROFILE_BALANCED;
    }
}

void pm_governor_request_turbo(uint32_t duration_ms) {
    pm_governor_state_t* gov = &g_kernel.governor;
    
    if (!gov->enabled) return;
    if (!gov->turbo_available) return;
    if (gov->thermal_throttled) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    gov->turbo_active = true;
    gov->turbo_start_ms = now;
    gov->requested_profile = CPU_PROFILE_TURBO;
    
    // Apply turbo immediately
    apply_profile(CPU_PROFILE_TURBO);
    gov->current_profile = CPU_PROFILE_TURBO;
    
    (void)duration_ms;  // TODO: implement timed turbo
}

void pm_governor_request_instant_turbo(void) {
    pm_governor_state_t* gov = &g_kernel.governor;
    
    if (!gov->enabled) return;
    if (!gov->turbo_available) return;
    if (gov->thermal_throttled) return;
    
    gov->instant_turbo_pending = true;
    
    // Apply turbo immediately
    if (gov->current_profile < CPU_PROFILE_TURBO) {
        apply_profile(CPU_PROFILE_TURBO);
        gov->current_profile = CPU_PROFILE_TURBO;
        gov->turbo_active = true;
        gov->turbo_start_ms = to_ms_since_boot(get_absolute_time());
    }
}
