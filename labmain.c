// TODO: levels (with ghost hunting you or something)
// TODO: leaderboard
// TODO: add speedup switch for multiplayer mode

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
Snake snakes[2];  // Support up to 2 players
int num_snakes = 1;  // 1 for singleplayer, 2 for multiplayer
Point food;
int tick_counter = 0;
int button_pressed_last_frame = 0;
unsigned int random_timer = 0; // Increments every interrupt for random seed

// --- Menu Selection State ---
int menu_selection = 0;          // 0 = one player, 1 = two players (toggled by SW0)
int last_menu_selection = -1;    // Debouncing: track last value to prevent flicker
int game_mode = 0;               // 0 = singleplayer, 1 = multiplayer

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
void display_score_single(int score);
void display_score_multi(int score1, int score2);

// --- Helper Functions for Game Logic ---
int check_wall_collision(Point p);
int check_snake_collision(Point p, Snake* s);
void move_snake(Snake* s);
void update_snake_direction(Snake* s, uint32_t sw_bits);

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
                last_menu_selection = -1;  // Reset to force initial draw
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
                int speed = 10;  // Game speed (10 interrupts per move at 30Hz = 3 moves/sec)

                if (tick_counter >= speed) {
                    tick_counter = 0; // update game every speed interrupts
                    update_game();
                    draw_game();
                }
                
                // Timer only for singleplayer mode (displays 4-5)
                if (num_snakes == 1) {
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
        
        // State-specific switch handling
        if (current_state == STATE_PLAYING) {
            // Gameplay: SW0 and SW1 control direction
            read_input();
        } 
        else if (current_state == STATE_MENU) {
            // Menu: SW0 toggles difficulty selection
            int new_selection = (*SWITCHES & 0x1) ? 1 : 0;
            
            // Only redraw if selection actually changed (to stop flickering)
            if (new_selection != last_menu_selection) {
                menu_selection = new_selection;
                last_menu_selection = new_selection;
                draw_menu();
            }
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
    
    // Initially enable only SW0 for menu navigation
    *SWITCH_INTERRUPTMASK = 0x1;
    
    enable_switch_interrupts();
    enable_timer_interrupts();
    enable_interrupt();
}

/**
 * @brief Resets game state for a new game.
 */
void reset_game(void) {
    // Configure switch interrupts based on game mode
    if (num_snakes == 1) {
        // Singleplayer: Enable SW0-1 (bits 0-1)
        *SWITCH_INTERRUPTMASK = 0x3;
    } else {
        // Multiplayer: Enable SW0-1 and SW8-9 (bits 0-1, 8-9)
        *SWITCH_INTERRUPTMASK = 0x303;
    }
    
    // Initialize player 1 snake (top-left)
    snakes[0].length = 3;
    snakes[0].body[0] = (Point){40, 30};
    snakes[0].body[1] = (Point){40, 20};
    snakes[0].body[2] = (Point){40, 10};
    snakes[0].direction = (Point){0, 10};  // Moving down
    
    // Initialize player 2 snake (bottom-right) if multiplayer
    if (num_snakes == 2) {
        snakes[1].length = 3;
        snakes[1].body[0] = (Point){280, 210};
        snakes[1].body[1] = (Point){290, 210};
        snakes[1].body[2] = (Point){300, 210};
        snakes[1].direction = (Point){-10, 0};  // Moving left
    }

    // Initialize score display
    if (num_snakes == 1) {
        display_score_single(snakes[0].length - 3);
    } else {
        display_score_multi(snakes[0].length - 3, snakes[1].length - 3);
    }

    // Place food at random position
    food.x = random_int(0, 31) * 10;
    food.y = random_int(0, 23) * 10;

    tick_counter = 0;
    
    // Reset timer for singleplayer
    test_seconds = 0;
    test_tick_counter = 0;
}

/**
 * @brief Handles button input for state transitions.
 */
void check_button_input(void) {
    int button_pressed_now = (*BUTTONS & 0x1);
    
    // Detect button press edge (was not pressed, now pressed)
    if (button_pressed_now && !button_pressed_last_frame) {
        if (current_state == STATE_MENU) {
            // Store selected game mode (0=single, 1=multi)
            game_mode = menu_selection;
            num_snakes = (game_mode == 0) ? 1 : 2;
            
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
 * @brief Reads switches to determine snakes' next directions.
 */
void read_input(void) {
    uint32_t switches = *SWITCHES;
    
    // Player 1: SW0-1 (bits 0-1)
    update_snake_direction(&snakes[0], switches & 0b11);
    
    // Player 2: SW8-9 (bits 8-9) if multiplayer
    if (num_snakes == 2) {
        update_snake_direction(&snakes[1], (switches >> 8) & 0b11);
    }
}


/**
 * @brief Updates snake position, checks for collisions and food.
 */
void update_game(void) {
    // Calculate new head positions for all snakes
    Point new_heads[2];
    for (int i = 0; i < num_snakes; i++) {
        new_heads[i].x = snakes[i].body[0].x + snakes[i].direction.x;
        new_heads[i].y = snakes[i].body[0].y + snakes[i].direction.y;
    }
    
    // Check head-to-head collision first (multiplayer only)
    if (num_snakes == 2) {
        if (new_heads[0].x == new_heads[1].x && new_heads[0].y == new_heads[1].y) {
            current_state = STATE_GAME_OVER;
            return;
        }
    }
    
    // Check collisions for all snakes
    for (int i = 0; i < num_snakes; i++) {
        // Check wall collision
        if (check_wall_collision(new_heads[i])) {
            current_state = STATE_GAME_OVER;
            return;
        }
        
        // Check self-collision
        if (check_snake_collision(new_heads[i], &snakes[i])) {
            current_state = STATE_GAME_OVER;
            return;
        }
        
        // Check collision with other snakes (multiplayer only)
        if (num_snakes == 2) {
            int other = 1 - i;  // Other snake index
            // Check collision with other snake's entire body
            for (int j = 0; j < snakes[other].length; j++) {
                if (new_heads[i].x == snakes[other].body[j].x && 
                    new_heads[i].y == snakes[other].body[j].y) {
                    current_state = STATE_GAME_OVER;
                    return;
                }
            }
        }
    }
    
    // All moves are safe - update all snakes
    for (int i = 0; i < num_snakes; i++) {
        move_snake(&snakes[i]);
    }
    
    // Check food collision for all snakes
    for (int i = 0; i < num_snakes; i++) {
        if (snakes[i].body[0].x == food.x && snakes[i].body[0].y == food.y) {
            // Grow snake
            Point old_tail_pos = snakes[i].body[snakes[i].length - 1];
            snakes[i].length++;
            snakes[i].body[snakes[i].length - 1] = old_tail_pos;
            
            // Update score display
            if (num_snakes == 1) {
                display_score_single(snakes[0].length - 3);
            } else {
                display_score_multi(snakes[0].length - 3, snakes[1].length - 3);
            }
            
            // Relocate food to new random position
            int attempts = 0;
            const int MAX_ATTEMPTS = 100;
            
            do {
                food.x = random_int(0, 31) * 10;
                food.y = random_int(0, 23) * 10;
                attempts++;
                
                // Check if food position conflicts with any snake body
                int collision = 0;
                for (int s = 0; s < num_snakes && !collision; s++) {
                    for (int j = 0; j < snakes[s].length && !collision; j++) {
                        if (food.x == snakes[s].body[j].x && food.y == snakes[s].body[j].y) {
                            collision = 1;
                        }
                    }
                }
                
                if (!collision) break;
            } while (attempts < MAX_ATTEMPTS);
            
            break;  // Only one snake can eat per frame
        }
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
    
    draw_letter('P', 20, 120, 0xFF);
    draw_letter('R', 45, 120, 0xFF);
    draw_letter('E', 70, 120, 0xFF);
    draw_letter('S', 95, 120, 0xFF);
    draw_letter('S', 120, 120, 0xFF);
    
    draw_letter('B', 30, 160, 0xFF);
    draw_letter('U', 55, 160, 0xFF);
    draw_letter('T', 80, 160, 0xFF);
    draw_letter('T', 105, 160, 0xFF);
    draw_letter('O', 130, 160, 0xFF);
    draw_letter('N', 155, 160, 0xFF);

    draw_letter('T', 10, 200, 0xFF);
    draw_letter('O', 35, 200, 0xFF);

    draw_letter('S', 70, 200, 0xFF);
    draw_letter('E', 95, 200, 0xFF);
    draw_letter('L', 120, 200, 0xFF);
    draw_letter('E', 145, 200, 0xFF);
    draw_letter('C', 170, 200, 0xFF);
    draw_letter('T', 195, 200, 0xFF);

    // --- Game Mode Selection (SW0 toggles) ---
    // ONE P - highlighted if menu_selection == 0
    uint8_t one_p_color = (menu_selection == 0) ? 0x1D : 0x24;  // Bright green if selected, dim if not
    draw_letter('O', 215, 90, one_p_color);
    draw_letter('N', 240, 90, one_p_color);
    draw_letter('E', 265, 90, one_p_color);
    
    draw_letter('P', 295, 90, one_p_color);
    
    // TWO P - highlighted if menu_selection == 1
    uint8_t two_p_color = (menu_selection == 1) ? 0xE1 : 0x24;  // Bright red if selected, dim if not
    draw_letter('T', 215, 130, two_p_color);
    draw_letter('W', 240, 130, two_p_color);
    draw_letter('O', 265, 130, two_p_color);
    
    draw_letter('P', 295, 130, two_p_color);

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
    
    // Draw player 1 snake (cyan/blue)
    for (int i = 0; i < snakes[0].length; i++) {
        draw_rect(snakes[0].body[i].x, snakes[0].body[i].y, 10, 10, 0x1F);
    }
    
    // Draw player 2 snake (red) if multiplayer
    if (num_snakes == 2) {
        for (int i = 0; i < snakes[1].length; i++) {
            draw_rect(snakes[1].body[i].x, snakes[1].body[i].y, 10, 10, 0xE0);
        }
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

    // Score display
    if (num_snakes == 1) {
        // Single player: show one score
        int score_x = 100;
        int score_y = 140;
        int score = snakes[0].length - 3;
        for (int i = 0; i < score && i < 20; i++) {
            draw_rect(score_x + (i * 6), score_y, 4, 4, 0x1F); // Cyan dots
        }
    } else {
        // Multiplayer: show both scores
        // Player 1 (cyan)
        int score1_x = 80;
        int score1_y = 120;
        int score1 = snakes[0].length - 3;
        for (int i = 0; i < score1 && i < 15; i++) {
            draw_rect(score1_x + (i * 6), score1_y, 4, 4, 0x1F); // Cyan dots
        }
        
        // Player 2 (red)
        int score2_x = 80;
        int score2_y = 160;
        int score2 = snakes[1].length - 3;
        for (int i = 0; i < score2 && i < 15; i++) {
            draw_rect(score2_x + (i * 6), score2_y, 4, 4, 0xE0); // Red dots
        }
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
 * @brief Displays the score for single player on 7-segment displays 0-3.
 * @param score The score to display (already adjusted)
 */
void display_score_single(int score) {
    // Extract individual digits (thousands, hundreds, tens, ones)
    int thousands = (score / 1000) % 10;
    int hundreds  = (score / 100) % 10;
    int tens      = (score / 10) % 10;
    int ones      = score % 10;

    // Display on 7-segment displays (rightmost is display 0)
    set_displays(0, segment_map[ones]);
    set_displays(1, segment_map[tens]);
    set_displays(2, segment_map[hundreds]);
    set_displays(3, segment_map[thousands]);
}

/**
 * @brief Displays scores for both players in multiplayer mode.
 * Player 1 on displays 0-1 (rightmost), Player 2 on displays 4-5 (leftmost).
 * @param score1 Player 1 score (already adjusted)
 * @param score2 Player 2 score (already adjusted)
 */
void display_score_multi(int score1, int score2) {
    // Player 1 (rightmost switches): displays 0-1 (rightmost)
    int p1_tens = (score1 / 10) % 10;
    int p1_ones = score1 % 10;
    set_displays(0, segment_map[p1_ones]);
    set_displays(1, segment_map[p1_tens]);
    
    // Displays 2-3: show zeros (unused)
    set_displays(2, segment_map[0]);
    set_displays(3, segment_map[0]);
    
    // Player 2 (leftmost switches): displays 4-5 (leftmost)
    int p2_tens = (score2 / 10) % 10;
    int p2_ones = score2 % 10;
    set_displays(4, segment_map[p2_ones]);
    set_displays(5, segment_map[p2_tens]);
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
// GAME LOGIC HELPER FUNCTIONS (OOP approach to avoid code duplication)
// ============================================================================

/**
 * @brief Checks if a point collides with a wall.
 * @param p Point to check
 * @return 1 if collision, 0 otherwise
 */
int check_wall_collision(Point p) {
    return (p.x < 0 || p.x >= SCREEN_WIDTH || p.y < 0 || p.y >= SCREEN_HEIGHT);
}

/**
 * @brief Checks if a point collides with a snake's body (excluding head).
 * @param p Point to check
 * @param s Snake to check against
 * @return 1 if collision, 0 otherwise
 */
int check_snake_collision(Point p, Snake* s) {
    for (int i = 1; i < s->length; i++) {
        if (p.x == s->body[i].x && p.y == s->body[i].y) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Moves a snake forward in its current direction.
 * @param s Snake to move
 */
void move_snake(Snake* s) {
    Point new_head = {
        s->body[0].x + s->direction.x,
        s->body[0].y + s->direction.y
    };
    
    // Shift body segments
    for (int i = s->length - 1; i > 0; i--) {
        s->body[i] = s->body[i - 1];
    }
    s->body[0] = new_head;
}

/**
 * @brief Updates snake direction based on switch input.
 * @param s Snake to update
 * @param sw_bits Two-bit switch value (00, 01, 10, 11)
 */
void update_snake_direction(Snake* s, uint32_t sw_bits) {
    // Only change direction if not opposite to current direction
    if (sw_bits == 0b00 && s->direction.y == 0) { 
        s->direction = (Point){0, -10};  // Up
    } 
    else if (sw_bits == 0b01 && s->direction.y == 0) { 
        s->direction = (Point){0, 10};   // Down
    } 
    else if (sw_bits == 0b10 && s->direction.x == 0) {
        s->direction = (Point){-10, 0};  // Left
    } 
    else if (sw_bits == 0b11 && s->direction.x == 0) {
        s->direction = (Point){10, 0};   // Right
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
