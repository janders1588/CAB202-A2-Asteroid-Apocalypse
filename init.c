#include <stdint.h>
#include <math.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <cpu_speed.h>
#include <macros.h>
#include <graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <lcd_model.h>
#include "usb_serial.h"
#include "cab202_adc.h"


#define OVERFLOW_TOP (1023)
#define BIT(x) (1 << (x))
void new_lcd_init(uint8_t contrast)
{
    // Set up the pins connected to the LCD as outputs
    SET_OUTPUT(DDRD, SCEPIN); // Chip select -- when low, tells LCD we're sending data
    SET_OUTPUT(DDRB, RSTPIN); // Chip Reset
    SET_OUTPUT(DDRB, DCPIN);  // Data / Command selector
    SET_OUTPUT(DDRB, DINPIN); // Data input to LCD
    SET_OUTPUT(DDRF, SCKPIN); // Clock input to LCD

    CLEAR_BIT(PORTB, RSTPIN); // Reset LCD
    SET_BIT(PORTD, SCEPIN);   // Tell LCD we're not sending data.
    SET_BIT(PORTB, RSTPIN);   // Stop resetting LCD

    LCD_CMD(lcd_set_function, lcd_instr_extended);
    LCD_CMD(lcd_set_contrast, contrast);
    LCD_CMD(lcd_set_temp_coeff, 0);
    LCD_CMD(lcd_set_bias, 3);

    LCD_CMD(lcd_set_function, lcd_instr_basic);
    LCD_CMD(lcd_set_display_mode, lcd_display_normal);
    LCD_CMD(lcd_set_x_addr, 0);
    LCD_CMD(lcd_set_y_addr, 0);
}

void teensy_init()
{
    set_clock_speed(CPU_8MHz);
    adc_init();
    lcd_init(LCD_DEFAULT_CONTRAST);
    // Enable centre LED for output
    SET_BIT(DDRB, 6);
    // Enable LED0
    SET_BIT(DDRB, 2); // LED0
    SET_BIT(DDRB, 3); //LED1
    SET_BIT(DDRD, 1); //Joy Up
    SET_BIT(DDRB, 7); //Joy Down
    SET_BIT(DDRB, 1); //Joy Left
    SET_BIT(DDRD, 0); //Joy Right
    SET_BIT(DDRB, 0); //Joy Center

    SET_BIT(DDRF, 5); //SW2
    SET_BIT(DDRF, 6); //SW1
    DIDR0 = 0x03;
    PORTB = 0b0;

    //Clear bits
    CLEAR_BIT(DDRD,1);
    CLEAR_BIT(DDRB,7);
    CLEAR_BIT(DDRB,1);
    CLEAR_BIT(DDRD,0);
    CLEAR_BIT(DDRB,0);


    TC4H = OVERFLOW_TOP >> 0x8;
    OCR4C = OVERFLOW_TOP & 0xff;

    // Set Timer 0 to overflow approx 122 times per second.

    TCCR0B |= 3;
    TIMSK0 = 1;

    //Set PWM
    TCCR4A = BIT(COM4A1) | BIT(PWM4A);
    SET_BIT(DDRC, 7); //LCD LED
    // (c)	Set pre-scale to "no pre-scale"
    TCCR4B = BIT(CS42) | BIT(CS41) | BIT(CS40);
    TCCR4D = 0;

    // Enable interrupts.
    sei();

}
