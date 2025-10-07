// TODO: game-over scene
// TODO: levels (with ghost hunting you or something)
// TODO: multiplayer
// TODO: animations
// TODO: leaderboard

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
int button_pressed_last_frame = 0;
unsigned int random_timer = 0; // Increments every interrupt for random seed

// FOR TIMER TESTING
int test_seconds = 0;
int test_tick_counter = 0;

// test animation for game over box
// TODO: remove?
int box_width = 200;  // Current width (starts at full)
int animating_box = 1;  // Flag to start animation

// --- 7-Segment Display Functions ---
// task e - from oldlabinterrupts.c
void set_displays(int display_number, int value) {
    *((volatile int *)0x04000050 + (display_number * 4)) = value;
}

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
void draw_game_over_animated(void);  // test animation: draw only the animated box
void draw_pixel(int x, int y, uint8_t color);
void draw_rect(int x, int y, int width, int height, uint8_t color);
void set_displays(int display_number, int value);
void display_score(int score);

// --- Letter Drawing Functions (20x30 px each, data-driven) ---
void draw_letter(char letter, int x, int y, uint8_t color);

// --- 7-Segment Display Mapping ---
// Maps digits 0-9 to 7-segment display patterns
const int segment_map[10] = {
    0x40, // 0
    0x79, // 1
    0x24, // 2
    0x30, // 3
    0x19, // 4
    0x12, // 5
    0x02, // 6
    0x78, // 7
    0x00, // 8
    0x10  // 9
};

/**
 * @brief Interrupt Service Routine (State Machine Hub)
 * Routes timer and switch interrupts based on current game state.
 */
void handle_interrupt(unsigned cause) {
    if (cause == 16) { // Timer interrupt
        *TIMER_STATUS = 0;
        random_timer++; // Always increment for random seed entropy
        
        // Draw static screens only when state changes (prevents flickering)
        if (current_state != previous_state) {
            if (current_state == STATE_MENU) {
                draw_menu();
            } else if (current_state == STATE_GAME_OVER) {
                box_width = 200;  // Reset animation on state entry
                animating_box = 1;
                draw_game_over();  // Initial full draw
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
                // speed is new ticks_per_move
                int speed = 10;
                // If switch nr 5 (from right to left) is up, use fast speed
                if (*SWITCHES & (1 << 4)) {
                    speed = 5;
                }

                if (tick_counter >= speed) {
                    tick_counter = 0; // update game every speed interrupts
                    update_game();
                    draw_game();
                }
                test_tick_counter++;
                if (test_tick_counter >= 30) {  // 30 interrupts = 1 second at 30Hz
                    test_tick_counter = 0;
                    test_seconds++;
                    if (test_seconds >= 60) {
                        test_seconds = 0;
                    }
                    // Update displays 4-5 with current seconds
                    int tens = (test_seconds / 10) % 10;
                    int ones = test_seconds % 10;
                    set_displays(4, segment_map[ones]);
                    set_displays(5, segment_map[tens]);
                }
                break;
                
            case STATE_GAME_OVER:
                check_button_input();

                // for test box animation
                if (animating_box && box_width > 0) {
                    box_width -= 1;  // Shrink by 1 pixel per frame (adjust for speed)
                    if (box_width <= 0) {
                        box_width = 0;
                        animating_box = 0;  // Stop animation when it hits 0
                    }
                    // Update only the animated box
                    draw_game_over_animated();
                }

                break;
        }
    } 
    else if (cause == 17) { // Switch interrupt
        *SWITCH_EDGECAPTURE = 0x3FF;
        
        // Only read input during gameplay
        if (current_state == STATE_PLAYING) {
            // movement input
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

    // Initialize score display (shows 0)
    display_score(snake.length);

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
            // Seed with timer value - different each time button is pressed
            seed_random(random_timer);
            reset_game();
            current_state = STATE_PLAYING;
        }
        // reset game on game over
        // TODO: make sure it stores highscores and stuff later!
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

        // Update score display on 7-segment displays
        display_score(snake.length);

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
    
    draw_letter('S', 80, 40, 0x1C);
    draw_letter('N', 105, 40, 0xE1);
    draw_letter('A', 130, 40, 0xD3);
    draw_letter('K', 155, 40, 0x33);
    draw_letter('E', 180, 40, 0xF1);
    
    draw_letter('P', 40, 120, 0xFF);
    draw_letter('R', 65, 120, 0xFF);
    draw_letter('E', 90, 120, 0xFF);
    draw_letter('S', 115, 120, 0xFF);
    draw_letter('S', 140, 120, 0xFF);
    
    draw_letter('B', 50, 160, 0xFF);
    draw_letter('U', 75, 160, 0xFF);
    draw_letter('T', 100, 160, 0xFF);
    draw_letter('T', 125, 160, 0xFF);
    draw_letter('O', 150, 160, 0xFF);
    draw_letter('N', 175, 160, 0xFF);
    
    // FOR TEST ALL LETTERS
    
    // int x_pos = 10;
    // int y_pos = 20;
    // for (char c = 'A'; c <= 'Z'; c++) {
    //     draw_letter(c, x_pos, y_pos, 0xFF);
    //     x_pos += 25;  // 20px letter + 5px spacing
    //     if (x_pos > 300) {  // Wrap to next line if needed
    //         x_pos = 10;
    //         y_pos += 35;
    //     }
    // }
    
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
    clear_screen(0x00); // black background

    draw_rect(35, 75, 200, 5, 0xE0);  // Initial full width

    // title
    draw_letter('G', 35, 40, 0xE0);
    draw_letter('A', 60, 40, 0xE1);
    draw_letter('M', 85, 40, 0xE2);
    draw_letter('E', 110, 40, 0xF1);

    draw_letter('O', 140, 40, 0x1C);
    draw_letter('V', 165, 40, 0xE1);
    draw_letter('E', 190, 40, 0xD3);
    draw_letter('R', 215, 40, 0x33);

    draw_rect(240, 67, 3, 3, 0xE0); // red box
    draw_rect(248, 67, 3, 3, 0xE0); // red box

    // score indicator (simple visual representation)
    // Draw small squares to represent score (snake length)
    int score_x = 100;
    int score_y = 140;
    for (int i = 0; i < snake.length && i < 20; i++) {
        draw_rect(score_x + (i * 6), score_y, 4, 4, 0x1C); // Green dots
    }
}

/**
 * @brief Draws only the animated box for game over (no clearing or static elements).
 */
void draw_game_over_animated(void) {
    // Erase the old box area by drawing black over it (to handle shrinking)
    draw_rect(35, 75, 200, 5, 0x00);  // Clear the max width area
    // Draw the new animated box
    draw_rect(35, 75, box_width, 5, 0xE0);
}

/**
 * @brief Displays the score on the 7-segment displays.
 * Shows a 4-digit number (padded with leading zeros if needed).
 * @param score The score to display (snake length - initial length)
 */
void display_score(int score) {
    // Calculate actual score (initial length was 3, so subtract 3)
    int actual_score = score - 3;

    // Extract individual digits (thousands, hundreds, tens, ones)
    int thousands = (actual_score / 1000) % 10;
    int hundreds  = (actual_score / 100) % 10;
    int tens      = (actual_score / 10) % 10;
    int ones      = actual_score % 10;

    // Display on 7-segment displays (rightmost is display 0)
    set_displays(0, segment_map[ones]);
    set_displays(1, segment_map[tens]);
    set_displays(2, segment_map[hundreds]);
    set_displays(3, segment_map[thousands]);

    // set_displays(4, segment_map[0]); // Always show 0 for now
    // set_displays(5, segment_map[0]); // Always show 0 for now
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

// ============================================================================
// LETTER DRAWING SYSTEM (20x30 pixels each, data-driven approach)
// ============================================================================

/**
 * @brief Defines a single stroke (rectangle) for drawing a letter.
 */
typedef struct {
    int8_t x_offset;   // X offset from letter origin
    int8_t y_offset;   // Y offset from letter origin
    uint8_t width;     // Rectangle width
    uint8_t height;    // Rectangle height
} Stroke;

// Letter data: Each letter is an array of strokes
static const Stroke LETTER_A[] = {{0,0,3,30}, {17,0,3,30}, {0,0,20,3}, {0,13,20,3}};
static const Stroke LETTER_B[] = {{0,0,3,30}, {0,0,17,3}, {0,13,17,3}, {0,27,20,3}, {14,3,3,10}, {17,16,3,11}};
static const Stroke LETTER_C[] = {{0,0,3,30}, {0,0,20,3}, {0,27,20,3}};
static const Stroke LETTER_D[] = {{0,0,3,30}, {0,0,17,3}, {0,27,17,3}, {17,3,3,24}};
static const Stroke LETTER_E[] = {{0,0,3,30}, {0,0,20,3}, {0,13,17,3}, {0,27,20,3}};
static const Stroke LETTER_F[] = {{0,0,3,30}, {0,0,20,3}, {0,13,17,3}};
static const Stroke LETTER_G[] = {{0,0,3,30}, {0,0,20,3}, {0,27,20,3}, {17,13,3,17}, {10,13,10,3}};
static const Stroke LETTER_H[] = {{0,0,3,30}, {17,0,3,30}, {0,13,20,3}};
static const Stroke LETTER_I[] = {{0,0,20,3}, {8,0,3,30}, {0,27,20,3}};
static const Stroke LETTER_J[] = {{0,0,20,3}, {14,0,3,27}, {0,27,17,3}, {0,20,3,7}};
static const Stroke LETTER_K[] = {{0,0,3,30}, {17,0,3,13}, {3,13,14,3}, {17,16,3,14}};
static const Stroke LETTER_L[] = {{0,0,3,30}, {0,27,20,3}};
static const Stroke LETTER_M[] = {{0,0,3,30}, {17,0,3,30}, {3,0,7,3}, {10,0,7,3}, {8,3,3,10}};
static const Stroke LETTER_N[] = {{0,0,3,30}, {17,0,3,30}, {3,7,6,3}, {9,10,3,6}, {12,16,6,3}};
static const Stroke LETTER_O[] = {{0,0,3,30}, {17,0,3,30}, {0,0,20,3}, {0,27,20,3}};
static const Stroke LETTER_P[] = {{0,0,3,30}, {0,0,17,3}, {0,13,17,3}, {14,3,3,10}};
static const Stroke LETTER_Q[] = {{0,0,3,27}, {17,0,3,30}, {0,0,20,3}, {0,24,17,3}, {10,20,7,3}};
static const Stroke LETTER_R[] = {{0,0,3,30}, {0,0,17,3}, {0,13,17,3}, {14,3,3,10}, {17,16,3,14}};
static const Stroke LETTER_S[] = {{0,0,20,3}, {0,0,3,16}, {0,13,20,3}, {17,13,3,17}, {0,27,20,3}};
static const Stroke LETTER_T[] = {{0,0,20,3}, {8,0,3,30}};
static const Stroke LETTER_U[] = {{0,0,3,30}, {17,0,3,30}, {0,27,20,3}};
static const Stroke LETTER_V[] = {{0,0,3,24}, {17,0,3,24}, {3,24,5,3}, {11,24,6,3}, {8,27,3,3}};
static const Stroke LETTER_W[] = {{0,0,3,27}, {17,0,3,27}, {3,27,5,3}, {11,27,6,3}, {8,17,3,10}};
static const Stroke LETTER_X[] = {{0,0,3,10}, {17,0,3,10}, {8,13,3,3}, {0,19,3,11}, {17,19,3,11}, {3,10,5,3}, {11,10,6,3}, {3,16,5,3}, {11,16,6,3}};
static const Stroke LETTER_Y[] = {{0,0,3,13}, {17,0,3,13}, {8,16,3,14}, {3,13,5,3}, {11,13,6,3}};
static const Stroke LETTER_Z[] = {{0,0,20,3}, {14,3,3,7}, {10,10,4,3}, {6,13,4,3}, {3,16,3,11}, {0,27,20,3}};

// Lookup table: maps letter index to stroke data
static const struct {
    const Stroke* strokes;
    uint8_t count;
} LETTER_DATA[26] = {
    {LETTER_A, 4}, {LETTER_B, 6}, {LETTER_C, 3}, {LETTER_D, 4}, {LETTER_E, 4}, {LETTER_F, 3},
    {LETTER_G, 5}, {LETTER_H, 3}, {LETTER_I, 3}, {LETTER_J, 4}, {LETTER_K, 4}, {LETTER_L, 2},
    {LETTER_M, 5}, {LETTER_N, 5}, {LETTER_O, 4}, {LETTER_P, 4}, {LETTER_Q, 5}, {LETTER_R, 5},
    {LETTER_S, 5}, {LETTER_T, 2}, {LETTER_U, 3}, {LETTER_V, 5}, {LETTER_W, 5}, {LETTER_X, 9},
    {LETTER_Y, 5}, {LETTER_Z, 6}
};

/**
 * @brief Draws a letter by reading stroke data.
 * @param letter The letter to draw ('A'-'Z')
 * @param x X position (top-left corner)
 * @param y Y position (top-left corner)
 * @param color Color to use
 */
void draw_letter(char letter, int x, int y, uint8_t color) {
    if (letter < 'A' || letter > 'Z') return;
    
    int idx = letter - 'A';
    const Stroke* strokes = LETTER_DATA[idx].strokes;
    uint8_t count = LETTER_DATA[idx].count;
    
    for (int i = 0; i < count; i++) {
        draw_rect(x + strokes[i].x_offset, 
                  y + strokes[i].y_offset,
                  strokes[i].width,
                  strokes[i].height,
                  color);
    }
}
