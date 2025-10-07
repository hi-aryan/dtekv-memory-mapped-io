// 00 (both switches UP): Tries to move UP.
// 01 (SW0 DOWN, SW1 UP): Tries to move DOWN.
// 10 (SW0 UP, SW1 DOWN): Tries to move LEFT.
// 11 (both switches DOWN): Tries to move RIGHT.

#include <stdint.h> // For standard integer types

// --- External Assembly Functions ---
extern void enable_interrupt(void);
extern void enable_switch_interrupts(void);
extern void enable_timer_interrupts(void);

// --- Memory-Mapped I/O Addresses (from DTEK-V PDF) ---
volatile uint32_t * const TIMER_STATUS = (uint32_t *) 0x4000020;
volatile uint32_t * const TIMER_CONTROL = (uint32_t *) 0x4000024;
volatile uint32_t * const TIMER_PERIOD_L = (uint32_t *) 0x4000028;
volatile uint32_t * const TIMER_PERIOD_H = (uint32_t *) 0x400002C;

volatile uint32_t * const SWITCHES = (uint32_t *) 0x4000010;
volatile uint32_t * const BUTTONS = (uint32_t *) 0x40000d0;

// not used yet
volatile uint32_t * const LEDS = (uint32_t *) 0x4000000;

volatile uint32_t * const SWITCH_EDGECAPTURE = (uint32_t *) 0x400001C;
volatile uint32_t * const SWITCH_INTERRUPTMASK = (uint32_t *) 0x4000018;

volatile uint8_t * const VGA_BUFFER = (uint8_t *) 0x8000000;

// --- Screen Dimensions ---
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// --- Game Object Structures (OOP-style) ---
typedef struct {
    int x, y;
} Point;

typedef struct {
    Point body[SCREEN_WIDTH * SCREEN_HEIGHT];
    int length;
    Point direction; // e.g., {x:1, y:0} for right
} Snake;

// --- Random Number Generation (Simple LCG) ---
static unsigned int random_seed = 1;

unsigned int simple_rand(void) {
    random_seed = (random_seed * 1103515245 + 12345) & 0x7fffffff;
    return random_seed;
}

void seed_random(unsigned int seed) {
    random_seed = seed;
}

int random_int(int min, int max) {
    return min + (simple_rand() % (max - min + 1));
}

// --- Game State Machine (OOP Pattern) ---
typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_GAME_OVER
} GameState;

// --- Global Game State ---
GameState current_state = STATE_MENU;
GameState previous_state = STATE_PLAYING; // Track state changes
Snake snake;
Point food;
int tick_counter = 0;
const int TICKS_PER_MOVE = 10;
int button_pressed_last_frame = 0;

// --- Function Prototypes ---
void initialize_hardware(void);
void reset_game(void);
void update_game(void);
void read_input(void);
void check_button_input(void);
void clear_screen(uint8_t color);
void draw_menu(void);
void draw_game(void);
void draw_game_over(void);
void draw_pixel(int x, int y, uint8_t color);
void draw_rect(int x, int y, int width, int height, uint8_t color);

/**
 * @brief Interrupt Service Routine (State Machine Hub)
 * Routes timer and switch interrupts based on current game state.
 */
void handle_interrupt(unsigned cause) {
    if (cause == 16) { // Timer interrupt
        *TIMER_STATUS = 0;
        
        // Draw static screens only when state changes (prevents flickering)
        if (current_state != previous_state) {
            if (current_state == STATE_MENU) {
                draw_menu();
            } else if (current_state == STATE_GAME_OVER) {
                draw_game_over();
            }
            previous_state = current_state;
        }
        
        // State machine: different behavior per state
        switch (current_state) {
            case STATE_MENU:
                check_button_input();
                break;
                
            case STATE_PLAYING:
                tick_counter++;
                if (tick_counter >= TICKS_PER_MOVE) {
                    tick_counter = 0;
                    update_game();
                    draw_game();
                }
                break;
                
            case STATE_GAME_OVER:
                check_button_input();
                break;
        }
    } 
    else if (cause == 17) { // Switch interrupt
        *SWITCH_EDGECAPTURE = 0x3FF;
        
        // Only read input during gameplay
        if (current_state == STATE_PLAYING) {
            read_input();
        }
    }
}


/**
 * @brief Main entry point
 */
int main(void) {
    initialize_hardware();
    
    // Start in menu state, show menu immediately
    current_state = STATE_MENU;
    draw_menu();
    
    while (1) {
        // Everything handled by interrupts
    }
    return 0;
}

/**
 * @brief Sets up hardware: timer, interrupts, and peripherals.
 */
void initialize_hardware(void) {
    // Set up a timer to interrupt 30 times per second (30Hz)
    *TIMER_PERIOD_H = 0x000F;
    *TIMER_PERIOD_L = 0x423F;
    *TIMER_CONTROL = 0x7;
    
    *SWITCH_INTERRUPTMASK = 0x3; // Enable interrupts for SW0 and SW1
    
    enable_switch_interrupts();
    enable_timer_interrupts();
    enable_interrupt();
}

/**
 * @brief Resets game state for a new game.
 */
void reset_game(void) {
    // Reset snake to initial position
    snake.length = 3;
    snake.body[0] = (Point){40, 30};
    snake.body[1] = (Point){30, 30};
    snake.body[2] = (Point){20, 30};
    snake.direction = (Point){10, 0};
    
    // Place food at random position
    food.x = random_int(0, 31) * 10;
    food.y = random_int(0, 23) * 10;
    
    tick_counter = 0;
}

/**
 * @brief Handles button input for state transitions.
 */
void check_button_input(void) {
    int button_pressed_now = (*BUTTONS & 0x1);
    
    // Detect button press edge (was not pressed, now pressed)
    if (button_pressed_now && !button_pressed_last_frame) {
        if (current_state == STATE_MENU) {
            // Seed random with timer value for entropy
            seed_random(*TIMER_STATUS ^ tick_counter);
            reset_game();
            current_state = STATE_PLAYING;
        }
        else if (current_state == STATE_GAME_OVER) {
            current_state = STATE_MENU;
        }
    }
    
    button_pressed_last_frame = button_pressed_now;
}

/**
 * @brief Reads switches to determine snake's next direction.
 */
void read_input(void) {
    uint32_t sw_val = *SWITCHES & 0b11;
    
    // Only change direction if not opposite
    if (sw_val == 0b00 && snake.direction.y == 0) { 
        snake.direction = (Point){0, -10};
    } 
    else if (sw_val == 0b01 && snake.direction.y == 0) { 
        snake.direction = (Point){0, 10};
    } 
    else if (sw_val == 0b10 && snake.direction.x == 0) {
        snake.direction = (Point){-10, 0};
    } 
    else if (sw_val == 0b11 && snake.direction.x == 0) {
        snake.direction = (Point){10, 0};
    }
}


/**
 * @brief Updates snake position, checks for collisions and food.
 */
void update_game(void) {
    // Calculate new head position
    Point new_head = {
        snake.body[0].x + snake.direction.x, 
        snake.body[0].y + snake.direction.y
    };

    // Check wall collision
    if (new_head.x < 0 || new_head.x >= SCREEN_WIDTH || 
        new_head.y < 0 || new_head.y >= SCREEN_HEIGHT) {
        current_state = STATE_GAME_OVER;
        return;
    }

    // Check self-collision
    for (int i = 1; i < snake.length; i++) {
        if (new_head.x == snake.body[i].x && new_head.y == snake.body[i].y) {
            current_state = STATE_GAME_OVER;
            return;
        }
    }

    // If we're here, the move is safe. Now we update the snake's body.
    for (int i = snake.length - 1; i > 0; i--) {
        snake.body[i] = snake.body[i - 1];
    }
    snake.body[0] = new_head;
    
    // Check food collision
    if (new_head.x == food.x && new_head.y == food.y) {
        // Initialize the new tail segment immediately ---
        // Get the position of the old tail before we increment length
        Point old_tail_pos = snake.body[snake.length - 1]; 
        snake.length++; // Now increase the length
        // The new tail segment is at the new end of the array. Give it a valid position.
        snake.body[snake.length - 1] = old_tail_pos;

        // Relocate food to new random position
        // Avoid placing food on snake body by checking collisions
        int attempts = 0;
        const int MAX_ATTEMPTS = 100;

        do {
            food.x = random_int(0, 31) * 10;
            food.y = random_int(0, 23) * 10;
            attempts++;

            // Check if food position conflicts with snake body
            int collision = 0;
            for (int i = 0; i < snake.length && !collision; i++) {
                if (food.x == snake.body[i].x && food.y == snake.body[i].y) {
                    collision = 1;
                }
            }

            if (!collision) break;
        } while (attempts < MAX_ATTEMPTS);

        // If we couldn't find a safe spot after many attempts, just place it anyway
        // This prevents infinite loops in edge cases
    }
}

/**
 * @brief Clears the entire screen to a solid color.
 */
void clear_screen(uint8_t color) {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        VGA_BUFFER[i] = color;
    }
}

/**
 * @brief Draws the menu screen.
 * Edit this function to customize the menu appearance.
 */
void draw_menu(void) {
    clear_screen(0x03); // Dark blue background
    
    // Draw title box
    draw_rect(60, 60, 200, 50, 0x1C); // Green box
    
    // Draw "Press Button" message box
    draw_rect(80, 130, 160, 30, 0xE0); // White box
}

/**
 * @brief Draws the gameplay screen.
 */
void draw_game(void) {
    clear_screen(0x00); // Black background
    
    // Draw snake
    for (int i = 0; i < snake.length; i++) {
        draw_rect(snake.body[i].x, snake.body[i].y, 10, 10, 0xE0); // White
    }
    
    // Draw food
    draw_rect(food.x, food.y, 10, 10, 0x1C); // Green
}

/**
 * @brief Draws the game over screen.
 * Edit this function to customize the game over appearance.
 */
void draw_game_over(void) {
    clear_screen(0xE0); // Red background
    
    // Draw "Game Over" box
    draw_rect(70, 80, 180, 40, 0x00); // Black box
    
    // Draw score indicator (simple visual representation)
    // Draw small squares to represent score (snake length)
    int score_x = 100;
    int score_y = 140;
    for (int i = 0; i < snake.length && i < 20; i++) {
        draw_rect(score_x + (i * 6), score_y, 4, 4, 0x1C); // Green dots
    }
    
    // Draw "Press Button" message
    draw_rect(80, 160, 160, 20, 0x00); // Black box
}

/**
 * @brief Draws a single pixel at (x, y)
 */
void draw_pixel(int x, int y, uint8_t color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        *(VGA_BUFFER + (y * SCREEN_WIDTH) + x) = color;
    }
}

/**
 * @brief Draws a filled rectangle.
 */
void draw_rect(int x_start, int y_start, int width, int height, uint8_t color) {
    for (int y = y_start; y < y_start + height; y++) {
        for (int x = x_start; x < x_start + width; x++) {
            draw_pixel(x, y, color);
        }
    }
}