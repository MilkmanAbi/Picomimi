/**
 * PICOMIMI OOM (Out-Of-Memory) Killer
 * Ported from v14.3.1 "Quiet Otter"
 * 
 * Features:
 * - Velocity tracking for allocation patterns
 * - Abusive allocator detection
 * - Victim selection with scoring algorithm
 * - Graceful cleanup via callbacks
 * - Resource-aware victim scoring
 * - Prevention before killing (cache flush, compaction)
 */
#ifndef PICOMIMI_OOM_H
#define PICOMIMI_OOM_H

#include "config/picomimi_config.h"
#include "api/picomimi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// OOM CONFIGURATION
// ============================================================================

#define OOM_MAX_HANDLERS                8       // Max OOM handler registrations
#define OOM_VELOCITY_WINDOW_SIZE        8       // Samples for velocity tracking
#define OOM_ABUSIVE_ALLOC_VELOCITY      50000   // Bytes/sec threshold for abuse
#define OOM_ABUSIVE_ALLOC_SIZE          65536   // Single alloc size threshold
#define OOM_REQUEST_TIMEOUT_MS          500     // Graceful cleanup timeout
#define OOM_PREVENTION_MIN_FREE_KB      4       // Minimum free KB to maintain

// ============================================================================
// TYPES
// ============================================================================

/**
 * OOM callback function type
 * Called when a task is selected as victim, allowing graceful cleanup
 * @param bytes_requested How much memory the allocator needs
 */
typedef void (*pm_oom_callback_t)(uint32_t bytes_requested);

/**
 * OOM victim information
 */
typedef struct {
    pm_task_id_t task_id;           // Victim task ID
    uint32_t memory_used;           // Memory used by victim
    uint32_t resource_count;        // Hardware resources owned
    int32_t score;                  // Victim score (higher = more killable)
    uint8_t oom_priority;           // Task's OOM priority (0 = unkillable)
    bool has_handler;               // Has graceful cleanup handler
} pm_oom_victim_t;

/**
 * OOM cleanup request (for graceful shutdown)
 */
typedef struct {
    pm_task_id_t allocating_task_id;    // Task that triggered OOM
    pm_task_id_t target_task_id;        // Victim task ID
    uint32_t bytes_requested;           // How much allocator needs
    uint32_t request_time_ms;           // When request was sent
    bool request_sent;                  // Request is active
    bool task_complied;                 // Victim freed memory
} pm_oom_request_t;

/**
 * OOM statistics
 */
typedef struct {
    uint32_t total_events;              // Total OOM events
    uint32_t prevention_count;          // Prevented via cache/compact
    uint32_t requests_sent;             // Graceful cleanup requests
    uint32_t voluntary_releases;        // Tasks that freed memory
    uint32_t forced_kills;              // Hard kills
    uint32_t abusive_kills;             // Killed for abusive allocation
    uint32_t total_bytes_reclaimed;     // Memory freed by OOM killer
} pm_oom_stats_t;

/**
 * Allocation velocity tracker
 */
typedef struct {
    uint32_t samples[OOM_VELOCITY_WINDOW_SIZE];
    uint32_t timestamps[OOM_VELOCITY_WINDOW_SIZE];
    uint8_t index;
    uint32_t total_bytes;
    uint32_t velocity;                  // Bytes/second
} pm_oom_velocity_t;

/**
 * Task OOM handler registration
 */
typedef struct {
    pm_task_id_t task_id;
    pm_oom_callback_t callback;
    bool active;
} pm_oom_handler_t;

// ============================================================================
// OOM KILLER API
// ============================================================================

/**
 * Initialize OOM killer subsystem
 */
pm_result_t pm_oom_init(void);

/**
 * Main OOM killer entry point
 * Called when memory allocation fails
 * @param bytes_needed How much memory the allocator needs
 * @return true if caller should retry allocation, false if already handled
 */
bool pm_oom_killer(size_t bytes_needed);

/**
 * Try to prevent OOM before killing
 * Attempts to free memory via caches, compaction, etc.
 * @param bytes_needed How much memory we need
 * @return true if prevention succeeded (enough memory now free)
 */
bool pm_oom_prevent(size_t bytes_needed);

/**
 * Select the best victim task to kill
 * @param bytes_needed How much memory we need
 * @return Victim information (task_id = 0xFFFFFFFF if no suitable victim)
 */
pm_oom_victim_t pm_oom_select_victim(size_t bytes_needed);

/**
 * Request graceful cleanup from a victim
 * @param victim Victim information
 * @param bytes_needed How much memory we need
 * @return true if request sent and waiting for response
 */
bool pm_oom_request_cleanup(pm_oom_victim_t* victim, size_t bytes_needed);

/**
 * Called by victim task when it has freed memory
 * @param task_id Task that freed memory
 * @param bytes_freed How much was freed
 */
void pm_oom_cleanup_done(pm_task_id_t task_id, uint32_t bytes_freed);

// ============================================================================
// HANDLER REGISTRATION
// ============================================================================

/**
 * Register an OOM handler for a task
 * Handler will be called before the task is killed
 * @param task_id Task to register handler for
 * @param callback Function to call on OOM
 */
void pm_oom_register_handler(pm_task_id_t task_id, pm_oom_callback_t callback);

/**
 * Unregister OOM handler for a task
 * @param task_id Task to unregister
 */
void pm_oom_unregister_handler(pm_task_id_t task_id);

/**
 * Get OOM handler for a task
 * @param task_id Task to query
 * @return Handler callback or NULL if none
 */
pm_oom_callback_t pm_oom_get_handler(pm_task_id_t task_id);

// ============================================================================
// VELOCITY TRACKING
// ============================================================================

/**
 * Update allocation velocity for a task
 * Called on every allocation
 * @param task_id Task that allocated
 * @param bytes How many bytes allocated
 */
void pm_oom_track_alloc(pm_task_id_t task_id, size_t bytes);

/**
 * Get allocation velocity for a task
 * @param task_id Task to query
 * @return Velocity in bytes/second
 */
uint32_t pm_oom_get_velocity(pm_task_id_t task_id);

/**
 * Check if a task is an abusive allocator
 * @param task_id Task to check
 * @return true if task is allocating too fast
 */
bool pm_oom_is_abusive(pm_task_id_t task_id);

// ============================================================================
// STATISTICS
// ============================================================================

/**
 * Get OOM statistics
 * @return Pointer to stats structure
 */
const pm_oom_stats_t* pm_oom_get_stats(void);

/**
 * Print OOM statistics to kernel output
 */
void pm_oom_print_stats(void);

/**
 * Reset OOM statistics
 */
void pm_oom_reset_stats(void);

// ============================================================================
// SCORING FUNCTIONS (for debugging/tuning)
// ============================================================================

/**
 * Calculate victim score for a task
 * Higher score = more likely to be killed
 * @param task_id Task to score
 * @param memory_used Memory used by task
 * @return Score value
 */
int32_t pm_oom_calculate_score(pm_task_id_t task_id, uint32_t memory_used);

#ifdef __cplusplus
}
#endif

#endif // PICOMIMI_OOM_H
