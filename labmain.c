// 00 (both switches UP): Tries to move UP.
// 01 (SW0 DOWN, SW1 UP): Tries to move DOWN.
// 10 (SW0 UP, SW1 DOWN): Tries to move LEFT.
// 11 (both switches DOWN): Tries to move RIGHT.

#include <stdint.h> // For standard integer types

// --- External Assembly Functions ---
extern void enable_interrupt(void);

// --- Memory-Mapped I/O Addresses (from DTEK-V PDF) ---
volatile uint32_t * const TIMER_STATUS = (uint32_t *) 0x4000020;
volatile uint32_t * const TIMER_CONTROL = (uint32_t *) 0x4000024;
volatile uint32_t * const TIMER_PERIOD_L = (uint32_t *) 0x4000028;
volatile uint32_t * const TIMER_PERIOD_H = (uint32_t *) 0x400002C;

volatile uint32_t * const SWITCHES = (uint32_t *) 0x4000010;
volatile uint32_t * const BUTTONS = (uint32_t *) 0x40000d0;

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

// --- Global Game State ---
Snake snake;
Point food;
int game_over = 0;
int tick_counter = 0; // For controlling game speed
const int TICKS_PER_MOVE = 10; // Slower game speed

// --- Function Prototypes ---
void initialize_game(void);
void update_game(void);
void read_input(void);
void draw_graphics(void);
void draw_pixel(int x, int y, uint8_t color);
void draw_rect(int x, int y, int width, int height, uint8_t color);

/**
 * @brief Interrupt Service Routine
 * This is the heart of the game. It gets called by the hardware timer.
 */
void handle_interrupt(unsigned cause) {
    *TIMER_STATUS = 0; // Acknowledge the interrupt

    if (game_over) {
        return; // Stop the game if it's over
    }

    tick_counter++;
    if (tick_counter >= TICKS_PER_MOVE) {
        tick_counter = 0;
        update_game();
        draw_graphics();
    }
}

/**
 * @brief Main entry point
 */
int main() {
    initialize_game();
    while (1) {
        // The main loop does nothing. Everything is handled by interrupts.
    }
    return 0;
}

/**
 * @brief Sets up the timer, interrupts, and initial game state.
 */
void initialize_game(void) {
    // Set up a timer to interrupt 30 times per second (30Hz)
    // Clock is 30MHz. 30,000,000 / 30 = 1,000,000 cycles per interrupt.
    // Period value is N-1. So, 999,999.
    // 999,999 in hex is 0xF423F
    *TIMER_PERIOD_H = 0x000F;
    *TIMER_PERIOD_L = 0x423F;
    
    // Start timer with continuous mode and interrupt enabled
    *TIMER_CONTROL = 0x7; // CONT=1, ITO=1, START=1

    // Initialize snake
    snake.length = 3;
    snake.body[0] = (Point){40, 30};
    snake.body[1] = (Point){30, 30};
    snake.body[2] = (Point){20, 30};
    snake.direction = (Point){10, 0}; // Moving right

    // Initialize food (pseudo-random)
    food.x = 100;
    food.y = 100;

    // Draw the initial screen
    draw_graphics();

    // Enable interrupts globally
    enable_interrupt();
}

// TODO: make movement use first 4 SW instead of first 2?
/**
 * @brief Reads switches to determine snake's next direction.
 */
void read_input(void) {
    uint32_t sw_val = *SWITCHES & 0b11; // Use only the first two switches
    
    // sw_val 00: up, 01: down, 10: left, 11: right
    if (sw_val == 0b00 && snake.direction.y == 0) { // UP
        snake.direction = (Point){0, -10};
    } else if (sw_val == 0b01 && snake.direction.y == 0) { // DOWN
        snake.direction = (Point){0, 10};
    } else if (sw_val == 0b10 && snake.direction.x == 0) { // LEFT
        snake.direction = (Point){-10, 0};
    } else if (sw_val == 0b11 && snake.direction.x == 0) { // RIGHT
        snake.direction = (Point){10, 0};
    }
}


/**
 * @brief Updates snake position, checks for collisions and food.
 */
void update_game(void) {
    read_input();

    // First, calculate the potential new head position
    Point new_head = {snake.body[0].x + snake.direction.x, snake.body[0].y + snake.direction.y};

    // --- FIX START: Check for collisions BEFORE moving ---
    // Check wall collision
    if (new_head.x < 0 || new_head.x >= SCREEN_WIDTH || new_head.y < 0 || new_head.y >= SCREEN_HEIGHT) {
        game_over = 1;
        return; // End the game here
    }
    // (We will add the self-collision check here later)
    // --- FIX END ---

    // If we're here, the move is safe. Now we update the snake's body.
    for (int i = snake.length - 1; i > 0; i--) {
        snake.body[i] = snake.body[i - 1];
    }
    snake.body[0] = new_head;
    
    // Check food collision
    if (new_head.x == food.x && new_head.y == food.y) {
        // --- FIX START: Initialize the new tail segment immediately ---
        // Get the position of the old tail before we increment length
        Point old_tail_pos = snake.body[snake.length - 1]; 
        snake.length++; // Now increase the length
        // The new tail segment is at the new end of the array. Give it a valid position.
        snake.body[snake.length - 1] = old_tail_pos;
        // --- FIX END ---

        // Relocate food
        food.x = ((food.x * 5 + 17) % (SCREEN_WIDTH/10)) * 10;
        food.y = ((food.y * 3 + 29) % (SCREEN_HEIGHT/10)) * 10;
    }
}

/**
 * @brief Draws the entire game screen.
 */
void draw_graphics(void) {
    // 1. Clear the screen (draw background)
    // A simple way is to use a nested loop
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            draw_pixel(x, y, 0x00); // Black
        }
    }
    
    // 2. Draw the snake
    for (int i = 0; i < snake.length; i++) {
        draw_rect(snake.body[i].x, snake.body[i].y, 10, 10, 0xE0); // White
    }
    
    // 3. Draw the food
    draw_rect(food.x, food.y, 10, 10, 0x1C); // Green
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