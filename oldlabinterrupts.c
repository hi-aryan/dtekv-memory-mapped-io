/* main.c

This file written 2024 by Artur Podobas and Pedro Antunes

For copyright and licensing, see file COPYING */


/* Below functions are external and found in other files. */
extern void print(const char*);
extern void print_dec(unsigned int);
extern void display_string(char*);
extern void time2string(char*,int);
extern void tick(int*);
extern void delay(int);
extern int nextprime( int );

extern void enable_interrupt(void); // task h

void display_time_on_hex(int time); // to fix warning

int mytime = 0x0; // change to 0000 ?
char textstring[] = "text, more text, and even more text!";

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

// pointers to timer stuff
volatile int *timer_status = (volatile int *) 0x4000020;
volatile int *timer_control = (volatile int *) 0x4000024;
volatile int *timer_periodl = (volatile int *) 0x4000028;
volatile int *timer_periodh = (volatile int *) 0x400002C;

int timeoutcount = 0;

int prime = 1234567;

volatile int *btn_interruptmask = (volatile int *) 0x040000d8; // Base + 8
volatile int *btn_edgecapture = (volatile int *) 0x040000dc; // Base + 12

/* Below is the function that will be called when an interrupt is triggered. */

void handle_interrupt(unsigned cause)
{
    // Check if the interrupt cause is the Timer (16)
    if (cause == 16) {
        // Acknowledge the timer interrupt
        *timer_status = 0;

        // Count 10 interrupts to make 1 second
        timeoutcount++;
        if (timeoutcount >= 10) {
            timeoutcount = 0;
            tick(&mytime);
        }
    }
    // Check if the interrupt cause is Button #0 (18)
    else if (cause == 18) {
        // As per the task, increase mytime by 2
        *btn_edgecapture = 0x1;
        tick(&mytime);
        // tick(&mytime);
        // mytime += 2;

        // Acknowledge the button interrupt by clearing its edge capture flag
    }

    /* -- TASK D: DISPLAY TIME -- */
    // The display should be updated on every interrupt to appear smooth,
    // even though the underlying 'mytime' variable only changes once per second.
    display_time_on_hex(mytime);
}

/* Add your code here for initializing interrupts. */
void labinit(void)
{
    // board runs on 30 MHz
    // -> 0.1 seconds = 3,000,000 cycles
    // actual period is 1 greater than value stored
    // set the timer period for 2,999,999 cycles).
    *timer_periodh = 0x2D; // 0x002D
    *timer_periodl = 0xC6BF;

    // 2. configure and start the timer.
    // We need to set the START bit (bit 2) and the CONT (continuous) bit (bit 1).
    // This gives a control value of 0b0110, which is 0x6.
    // The manual also implies it's good practice to STOP it first (bit 3, 0x8),
    // write the period, then start it. We can do it all in one go here for simplicity.
    *timer_control = 0x7;

    *btn_interruptmask = 0x1; // Enable interrupt for button 0 (bit 0)
    
    enable_interrupt();
}

// task c
void set_leds(int led_mask) { 
volatile int *led_ptr = (volatile int *) 0x04000000; 
*led_ptr = led_mask; 
}

// task e
void set_displays(int display_number, int value) {
    *((volatile int *)0x04000050 + (display_number * 4)) = value;
}

// task f
int get_sw(void) {
    // 1. Point to the switch memory-mapped I/O address.
    // use 'volatile' because the switch values can change at any time, independent of our program's execution.
    volatile int *switch_ptr = (volatile int *)0x04000010;

    // 2. Read the 32-bit integer value from the hardware.
    // This value contains the switch states in the lower 10 bits.
    int raw_value = *switch_ptr;

    // remove "garbage" data in the upper bits.
    int masked_value = raw_value & 0x3FF;

    return masked_value;
}

// task g
int get_btn(void) {
    // 1. Point to the button memory-mapped I/O address.
    volatile int *btn_ptr = (volatile int *)0x040000d0;

    // 2. Read the raw 32-bit value.
    int raw_value = *btn_ptr;
    // print("Button raw value: ");
    // print_dec(raw_value);
    // print("\n");

    return raw_value & 0x1;
}

// task h (helper function to display time on displays, using BCD format!!)
void display_time_on_hex(int time) {
    // --- Unpack BCD digits ---
    // Instead of treating 'seconds' as one number, we treat it as two 4-bit digits.
    int sec_ones = time & 0xF;          // Lowest 4 bits are the seconds' ones digit
    int sec_tens = (time >> 4) & 0xF;   // Next 4 bits are the seconds' tens digit

    int min_ones = (time >> 8) & 0xF;   // ...and so on for minutes
    int min_tens = (time >> 12) & 0xF;

    int hour_ones = (time >> 16) & 0xF; // ...and hours
    int hour_tens = (time >> 20) & 0xF; // Note: Original 'tick' doesn't go this high, but it's good practice.

    // --- Display digits ---
    set_displays(0, segment_map[sec_ones]);
    set_displays(1, segment_map[sec_tens]);

    set_displays(2, segment_map[min_ones]);
    set_displays(3, segment_map[min_tens]);

    set_displays(4, segment_map[hour_ones]);
    set_displays(5, segment_map[hour_tens]);
}

int main ( void ) {
    labinit();
    while (1) {
        print ("Prime: ");
        prime = nextprime( prime );
        print_dec( prime );
        print("\n");
    }
}

// OLD MAIN FUNCTION
/*
int main() {
    labinit();

    int previous_time = -1; // Initialize to an invalid time to ensure the first update occurs.

    while (1) {
        // TODO: how does this work?
        if (*timer_status & 0x1) {
            
            // --- RESET THE TIMER FLAG ---
            // Writing any value to the status register clears the TO bit.
            *timer_status = 0; 
            

            timeoutcount++;
            
            // 3. Check if 10 events have occurred (meaning 1 full second has passed).
            if (timeoutcount >= 10) {
                
                // 4. Reset our software counter to start counting the next second.
                timeoutcount = 0;
                
                // 5. Call tick() ONCE PER SECOND.
                tick(&mytime);
            }

            // --- DO THE ONCE-PER-100MS TASKS ---
            // This code now only runs 10 times per second.
            // time2string(textstring, mytime);
            // display_string(textstring);
            // delay(2);
        //  tick(&mytime); // This will now make the clock run 10x faster! We fix this in part c.
            
            // display_time_on_hex(mytime);
        }

        // to not run these on EVERY loop iteration (unnecessary cause it'd write 00:00:01 like a million times before next second)
        if (mytime != previous_time) {
            
            // Update the physical 7-segment displays
            display_time_on_hex(mytime);
            
            // Update the string in memory for printing
            time2string(textstring, mytime);
            
            // Print the new time to the terminal. This is the ONLY print
            // in the main loop now.
            display_string(textstring);
            
            // Update our copy so we don't print again until the next tick.
            previous_time = mytime;
        }

        if (get_btn()) { // BCD version
            // print("Button pressed!\n");
            int sw_val = get_sw();

            // Mode from switches 9 and 8 (correct)
            int mode = (sw_val >> 8) & 0x3;

            // New value from switches 5-0. We need to convert this to BCD.
            // For example, decimal 25 should become 0x25.
            int decimal_value = sw_val & 0x3F;
            int bcd_value = ((decimal_value / 10) << 4) | (decimal_value % 10);

            // --- CORRECT BCD MANIPULATION ---
            if (mode == 1) { // 01: modify seconds
                // Keep the hours and minutes, replace the seconds.
                // (mytime & 0xFFFF00) clears the bottom 8 bits (seconds).
                // Then we OR in the new BCD value.
                mytime = (mytime & 0xFFFF00) | bcd_value;
            }
            if (mode == 2) { // 10: modify minutes
                // Keep hours and seconds, replace minutes.
                // (mytime & 0xFF00FF) clears the middle 8 bits (minutes).
                // Then we OR in the new BCD value, shifted to the minute position.
                mytime = (mytime & 0xFF00FF) | (bcd_value << 8);
            }
            if (mode == 3) { // 11: modify hours
                // Keep minutes and seconds, replace hours.
                // (mytime & 0x00FFFF) clears the top 8 bits (hours).
                // Then we OR in the new BCD value, shifted to the hour position.
                mytime = (mytime & 0x00FFFF) | (bcd_value << 16);
            }

        }

        // --- 3. HANDLE OUTPUT ---
        // Display the current time on the 7-segment displays.
        display_time_on_hex(mytime);
            
    }
}
*/