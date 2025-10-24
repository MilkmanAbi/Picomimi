// --- SNAKE GAME (UISocket Example) ---
// Save this as a new .ino file (e.g., snake_app.ino)
// and add it to your main kernel sketch.

// 1. Define a struct to hold the game's data
struct SnakeGame {
    UISocket ui; // The API to interact with the GUI system
    int16_t snake_x[50];
    int16_t snake_y[50];
    uint8_t snake_len;
    int8_t dir_x, dir_y;
    int16_t food_x, food_y;
    uint32_t score;
    bool game_over;
    bool has_focus; // Track if we are the active GUI
    uint32_t last_input_time;
};

// Global pointer for this app's data
SnakeGame* snake_game = NULL;

// 2. Create the INIT function (runs once when task is created)
void snake_init(uint32_t id) {
    // Allocate memory for the game state from the kernel
    snake_game = (SnakeGame*)kmalloc(sizeof(SnakeGame), id);
    if (!snake_game) {
        kout.println("[SNAKE] Alloc failed!");
        return; // The tick function will see the null pointer and terminate.
    }
    memset(snake_game, 0, sizeof(SnakeGame));

    // Register with the GUI system to get the API functions
    k_register_gui_app(&snake_game->ui);

    // Set up initial game state
    snake_game->snake_len = 3;
    snake_game->snake_x[0] = 160;
    snake_game->snake_y[0] = 120;
    snake_game->dir_x = 10;
    snake_game->dir_y = 0;
    snake_game->food_x = (random(1, 31)) * 10;
    snake_game->food_y = (random(1, 23)) * 10;
    
    klog(0, "SNAKE: Initialized");
}

// 3. Create the DEINIT function (runs when task is killed)
void snake_deinit() {
    if (snake_game) {
        // Check if we still have GUI focus and release it
        if (kernel.gui_focus_task_id == kernel.current_task) {
            snake_game->ui.release_focus(kernel.current_task);
        }
        // Free the memory we allocated
        kfree(snake_game);
        snake_game = NULL;
    }
    klog(0, "SNAKE: Deinitialized");
}

// 4. Create the TASK function (this is the main app loop)
void snake_task(void* arg) {
    // Check if init failed
    if (!snake_game) {
        brutal_task_kill(kernel.current_task);
        task_sleep(10000); // Give scheduler time to kill us
        return;
    }

    // --- Focus Management ---
    // If we don't have focus (e.g., first run), we must request it
    if (!snake_game->has_focus) {
        if (!snake_game->ui.request_focus(kernel.current_task)) {
             // Another app is running and we couldn't take over
             kout.println("[SNAKE] Could not get focus, terminating.");
             brutal_task_kill(kernel.current_task);
             task_sleep(10000);
             return;
        }
        // We got focus!
        snake_game->has_focus = true;
        snake_game->ui.clear_screen();
    }

    // Double-check we *still* have focus.
    // The user could have pressed the ONOFF button to cycle away.
    if (kernel.gui_focus_task_id != kernel.current_task) {
        snake_game->has_focus = false; // Mark that we lost focus
        task_sleep(200); // Wait until we (maybe) get focus back
        return;
    }
    
    // --- Game Over State ---
    if (snake_game->game_over) {
        snake_game->ui.draw_text(160, 100, "Game Over", ILI9341_RED, 3, true);
        char score_buf[20];
        snprintf(score_buf, sizeof(score_buf), "Score: %d", snake_game->score);
        snake_game->ui.draw_text(160, 140, score_buf, ILI9341_WHITE, 2, true);
        snake_game->ui.draw_text(160, 180, "Press any key to exit", ILI9341_YELLOW, 1, true);

        // Check for any button press to exit
        if (snake_game->ui.get_button_state(UI_BTN_TOP) ||
            snake_game->ui.get_button_state(UI_BTN_BOTTOM) ||
            snake_game->ui.get_button_state(UI_BTN_LEFT) ||
            snake_game->ui.get_button_state(UI_BTN_RIGHT)) {
            
            // This will call snake_deinit, release GUI focus, and free memory.
            brutal_task_kill(kernel.current_task);
            return;
        }
        task_sleep(50);
        return;
    }

    // --- Input Handling ---
    uint32_t now = get_time_ms();
    if (now - snake_game->last_input_time > 80) { // Debounce input
        bool moved = false;
        if (snake_game->ui.get_button_state(UI_BTN_TOP) && snake_game->dir_y == 0) {
            snake_game->dir_x = 0;
            snake_game->dir_y = -10; moved = true;
        } else if (snake_game->ui.get_button_state(UI_BTN_BOTTOM) && snake_game->dir_y == 0) {
            snake_game->dir_x = 0;
            snake_game->dir_y = 10; moved = true;
        } else if (snake_game->ui.get_button_state(UI_BTN_LEFT) && snake_game->dir_x == 0) {
            snake_game->dir_x = -10;
            snake_game->dir_y = 0; moved = true;
        } else if (snake_game->ui.get_button_state(UI_BTN_RIGHT) && snake_game->dir_x == 0) {
            snake_game->dir_x = 10;
            snake_game->dir_y = 0; moved = true;
        }
        if (moved) {
            snake_game->last_input_time = now;
        }
    }


    // --- Game Logic ---
    // Erase tail
    snake_game->ui.fill_rect(snake_game->snake_x[snake_game->snake_len - 1],
                             snake_game->snake_y[snake_game->snake_len - 1],
                             10, 10, ILI9341_BLACK);
    // Move body
    for (int i = snake_game->snake_len - 1; i > 0; i--) {
        snake_game->snake_x[i] = snake_game->snake_x[i-1];
        snake_game->snake_y[i] = snake_game->snake_y[i-1];
    }
    // Move head
    snake_game->snake_x[0] += snake_game->dir_x;
    snake_game->snake_y[0] += snake_game->dir_y;

    // Check for food
    if (snake_game->snake_x[0] == snake_game->food_x && snake_game->snake_y[0] == snake_game->food_y) {
        snake_game->score++;
        if (snake_game->snake_len < 49) snake_game->snake_len++;
        snake_game->food_x = (random(1, 31)) * 10;
        snake_game->food_y = (random(1, 23)) * 10;
    }

    // Check for collision (walls)
    if (snake_game->snake_x[0] < 0 || snake_game->snake_x[0] > 310 ||
        snake_game->snake_y[0] < 0 || snake_game->snake_y[0] > 230) {
        snake_game->game_over = true;
    }
    // Check for collision (self)
    for (int i = 1; i < snake_game->snake_len; i++) {
        if (snake_game->snake_x[0] == snake_game->snake_x[i] && snake_game->snake_y[0] == snake_game->snake_y[i]) {
            snake_game->game_over = true;
        }
    }


    // --- Drawing ---
    // Draw food
    snake_game->ui.fill_rect(snake_game->food_x, snake_game->food_y, 10, 10, ILI9341_RED);
    // Draw snake
    for (int i = 0; i < snake_game->snake_len; i++) {
        snake_game->ui.fill_rect(snake_game->snake_x[i], snake_game->snake_y[i], 10, 10,
                                 i == 0 ? ILI9341_GREEN : ILI9341_YELLOW);
    }
    // Draw score
    char score_buf[20];
    snprintf(score_buf, sizeof(score_buf), "Score: %d", snake_game->score);
    snake_game->ui.fill_rect(0, 0, 80, 10, ILI9341_BLACK); // Clear old score
    snake_game->ui.draw_text(2, 2, score_buf, ILI9341_WHITE, 1, false);

    task_sleep(150); // Game speed
}

// 5. Create the Callbacks struct to link functions to the kernel
ModuleCallbacks snake_callbacks = {
    .init = snake_init,
    .tick = snake_task,
    .deinit = snake_deinit
};

// 6. Create the Spawner function (this is what the shell will call)
void snake_spawn() {
    uint32_t tid = task_create("snake", NULL, NULL, 6,
                               TASK_TYPE_APPLICATION, TASK_FLAG_ONESHOT, 0,
                               OOM_PRIORITY_NORMAL, 4 * 1024, &snake_callbacks,
                               "Snake game (OOM=NORM)");
    if (tid > 0) {
        kout.println("Snake game started");
    }
}
