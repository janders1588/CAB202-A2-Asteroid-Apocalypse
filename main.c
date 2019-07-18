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
#include "ram_utils.h"
#include "lcd.h"
#include "init.h"

// Bit values for uint8_t gamestate.
// Note: when cheated flag is set the pause flag is ignored in
// certain functions this sets up a more productive debugging
// environment.
#define CHEATED 0x01
#define START 0x02
#define OVER 0x03
#define OVER_CHOICE 0x04
#define INPUT 0x05
#define PAUSED 0x06
#define QUIT 0x07

// Movement directions
#define LEFT 0xD8
#define RIGHT 0xD1
#define NEUTRAL 0xD6

// Falling object state bits.
#define DRAWN 0x00
#define MOVING 0x01
#define BROKEN 0x02


// Object values, used for identification.
// The value also represents width of the object.
#define ASTEROID 0x07
#define BOULDER 0x05
#define FRAGMENT 0x03
#define SHIP 0x09

// The y coordinate for any unused objects to be placed.
#define PROJECTILE_POOL -15
#define POOL_COORDINATES -10

// Max values calculated from the initial 3 asteroids.
#define MAX_ASTEROID 3
#define MAX_BOULDER (MAX_ASTEROID*2)
#define MAX_FRAG (MAX_BOULDER*2)
#define MAX_PROJECTILE 30

// uint8_t is used wherever the value is guaranteed >=0 || <= 255

void setup_images(void);
void setup_gamestate(void);
void quit_screen(void);
void usb_serial_send(char * message);
void serial_input(int16_t char_code);
void erase_ship(void);
void draw_ship(void);
void draw_int( uint8_t x, uint8_t y, int value, colour_t colour );
void draw_double(uint8_t x, uint8_t y, double value, colour_t colour);
void fire_plasma_bolt(void);
int receive_serial_cheat();
int serial_to_int(char *base, char target[1][3]);

//Time stuff
double spawn_delay;
double wave_time;
double led_hz;
double return_manual;
double game_speed;

//Game stuff
uint8_t direction;
double game_time;

int status_screen=0;

int score;
int flash_led=0;
uint8_t led_state;

char in_buff[12];

int turret_override=0;
int speed_override=0;
int intro_screen=1;
int dim_lcd=0;

// Game state (see definition for bit values)
uint8_t gamestate;

//Stuff stuff
char * student_num = "n10318399";

//Shield and ship stuff
int shield_life;
uint8_t ship_x;


uint8_t falling_objects;

//Asteroid stuff
double ax[MAX_ASTEROID], ay[MAX_ASTEROID];
int asteroid_tick[MAX_ASTEROID];
uint8_t asteroid_state[MAX_ASTEROID];

//boulder stuff
double bx[MAX_BOULDER], by[MAX_BOULDER], bdx[MAX_BOULDER], bdy[MAX_BOULDER];
int boulder_tick[MAX_BOULDER];
uint8_t boulder_state[MAX_BOULDER];

//fragment stuff
double fx[MAX_FRAG], fy[MAX_FRAG], fdx[MAX_FRAG], fdy[MAX_FRAG];
int fragment_tick[MAX_FRAG];
uint8_t fragment_state[MAX_FRAG];

//turret stuff and projectile pool
double tx, ty;
double px[MAX_PROJECTILE],py[MAX_PROJECTILE],pdx[MAX_PROJECTILE],pdy[MAX_PROJECTILE];
int projectile_tick[MAX_PROJECTILE];
double fireTimer=0;
uint8_t projectile_state[MAX_PROJECTILE];
double projectile_heading[MAX_PROJECTILE];
int fired;

// ----------------------------------------------------------






// ---------------------------------------------------------
//	Timers and movement.
// ---------------------------------------------------------

#define FREQ 8000000.0
#define PRESCALE 64.0
#define TIMER_SCALE 256.0



uint8_t mask = 0b1111;
volatile uint8_t joy_state_count = 0;
volatile uint8_t joy_up_closed;
volatile uint8_t joy_down_closed;
volatile uint8_t joy_left_closed;
volatile uint8_t joy_right_closed;
volatile uint8_t joy_center_closed;
volatile uint8_t SW1_closed;
volatile uint8_t SW2_closed;
/***
*   Function for handling the de-bouncing for all input switches
*   called in the timer0 ISR running at 125kHz with a .002048s
*   overflow.
*/
void joy_click()
{
    if(BIT_IS_SET(PIND,1)||
            BIT_IS_SET(PINB,7)||
            BIT_IS_SET(PINB,1)||
            BIT_IS_SET(PIND,0)||
            BIT_IS_SET(PINB,0)||
            BIT_IS_SET(PINF,6)||
            BIT_IS_SET(PINF,5))
    {
        joy_state_count=((joy_state_count<<1)&mask)|1;
    }
    else
    {
        joy_state_count=((joy_state_count<<1)&mask)|0;
    }
    if(joy_state_count==0)
    {
        joy_up_closed=0;
        joy_down_closed=0;
        joy_left_closed=0;
        joy_right_closed=0;
        joy_center_closed=0;
        SW1_closed =0;
        SW2_closed=0;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PIND,1))
    {
        joy_up_closed=1;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PINB,7))
    {
        joy_down_closed=1;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PINB,1))
    {
        joy_left_closed=1;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PIND,0))
    {
        joy_right_closed=1;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PINB,0))
    {
        joy_center_closed=1;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PINF,6))
    {
        SW1_closed=1;
    }
    else if(joy_state_count==mask && BIT_IS_SET(PINF,5))
    {
        SW2_closed=1;
    }
}



uint8_t ship_tick=0;
uint8_t dynamic_ship_width;
uint8_t ship_speed=15;
/**
*   Function for handling the ships x offset, speed and making sure the ship
*   is and stays within screen bounds.
*/
void ship_movement()
{
    ship_tick++;
    if(ship_tick * ship_speed > 10)
    {
        // If the turret is at a heading >0 and the ship is in a position
        // where moving the turret will leave screen space then override the
        // left pot value and set the turret x position to fit int the screen.
        if(tx<=0)
            dynamic_ship_width=9;
        else
            dynamic_ship_width = 9+tx;
        if(ship_x+9+tx>83)
        {
            turret_override=1;
            tx=(int)83-(ship_x+9);
        }
        else
            turret_override=0;

        if(direction == RIGHT)
        {
            if(ship_x>83-dynamic_ship_width)
            {
                direction = NEUTRAL;
            }
            else
            {
                ship_x++;
            }
        }
        if(direction == LEFT)
        {
            if(ship_x<1)
            {
                direction = NEUTRAL;
            }
            else
            {
                ship_x--;
            }
        }
        ship_tick=0;
    }

}

/**
*   A function designed to randomly shuffle an array of uint8_t values
*
*   Parameters:
*           array: a pointer that leads to the array to be shuffled
*
*   Note: no null check in place so the for loop is set at a static value
*         indicative of the MAX_ASTEROID value, in the future a null check
*         should be added and the second argument in the for loop changed
*         to < length of array to make it a more dynamic function.
*/
void shuffle_array(uint8_t * array)
{
    uint8_t i,j,tmp;

    for(i=0; i<MAX_ASTEROID; i++)
    {
        j= rand()%3;
        tmp=array[i];
        array[i]=array[j];
        array[j]=tmp;
    }
}

/**
*   Returns the number of objects moving on screen
*
*   Return: A uint8_t value that represents then number of
*           falling objects currently in play.
*
*   Note: this value does not return the projectile count in
*         the total count.
**/
uint8_t asteroid_count, boulder_count, frag_count, projectile_count;
uint8_t spawn_check()
{
    asteroid_count =boulder_count =frag_count=projectile_count = 0;
    for(uint8_t i=0; i<MAX_ASTEROID; i++)
    {
        if(BIT_IS_SET(asteroid_state[i],MOVING))
            asteroid_count++;
    }
    for(uint8_t i=0; i<MAX_BOULDER; i++)
    {
        if(BIT_IS_SET(boulder_state[i],MOVING))
            boulder_count++;
    }
    for(uint8_t i=0; i<MAX_FRAG; i++)
    {
        if(BIT_IS_SET(fragment_state[i],MOVING))
            frag_count++;
    }
    for(uint8_t i=0; i<MAX_FRAG; i++)
    {
        if(BIT_IS_SET(projectile_state[i],MOVING))
            projectile_count++;
    }

    return  asteroid_count+boulder_count+frag_count;
}


/**
*   A function for flashing LED0 and 1 output at a specified Hz value.
*
*   Note: To get the second value of Hz: seconds = 1 / Hz.
*         To get the Hz representation of a second value: Hz = 1 / seconds.
*/
void led_flash(double hz)
{
    if(flash_led)
    {
        led_hz = 0;
        if(!BIT_IS_SET(gamestate,OVER))
        {
            uint8_t tmp;

            // Checks the middle asteroid to see if it is to the left or right
            // of center screen,
            if(ax[1]+3>LCD_X/2)
                tmp = 3;
            else
                tmp = 2;
            SET_BIT(PORTB,tmp);
        }
        else
        {
            SET_BIT(PORTB,3);
            SET_BIT(PORTB,2);
        }

        flash_led =0;

    }
    if(led_hz >= 0.5)
    {
        CLEAR_BIT(PORTB,3);
        CLEAR_BIT(PORTB,2);
    }

}



/***
*   A function for holding all of the timers that gets called in the
*   ISR function.
**/
void timers()
{
    if(fireTimer>1)
        fireTimer=0;
    fireTimer += TIMER_SCALE * PRESCALE / FREQ;
    if(spawn_delay >20)
        spawn_delay =0;
    spawn_delay += TIMER_SCALE * PRESCALE / FREQ;
    if(wave_time> 10)
        wave_time=0;
    wave_time += TIMER_SCALE * PRESCALE / FREQ;
    if(led_hz>40)
        led_hz=0;
    led_hz += TIMER_SCALE * PRESCALE / FREQ;

    // Handles the manual override return switching for the pots
    return_manual += TIMER_SCALE * PRESCALE / FREQ;
    if(return_manual>1)
    {
        turret_override=0;
        speed_override=0;
        return_manual=0;
    }
}


double lcd_led_value=1023;
/**
*   A function to  dim the the LCD LED
*
*   Note: dimming is controlled by th global variable
*         dim_lcd, when set the lcd will dim over 2 seconds
          (0.002048 overflow * 1023), when not set it will
          return from current value to 0 at the same rate.
*/
void lcd_dimmer()
{
    if(dim_lcd)
    {
        lcd_led_value++;

    }
    else
        lcd_led_value--;

    if(lcd_led_value>1023)
        lcd_led_value=1023;
    if(lcd_led_value<0)
        lcd_led_value=0;
}

uint8_t wave_started=0;
uint8_t array_pos[] = {0,1,2};
uint8_t count=0;
double rand_delay;
ISR(TIMER0_OVF_vect)
{
    joy_click();
    int on_screen = spawn_check();

    timers();

    lcd_dimmer();

    if(!BIT_IS_SET(gamestate,PAUSED))
    {
        game_time += TIMER_SCALE * PRESCALE / FREQ;
        if(game_speed >0)
        {
            //If no object are on screen then start the wave
            if(on_screen==0 && !wave_started)
            {
                wave_time=0;
                wave_started=1;
                shuffle_array(array_pos);
                rand_delay = (double)(rand()% 15+1)/10;
                flash_led=1;
            }

            //Wave time delayed before start
            if(wave_time>2)
            {
                led_flash(0.5);


                //Delay between asteroids
                if ( spawn_delay >= rand_delay)
                {
                    rand_delay = (double)(rand()% 15+1)/10;
                    if(wave_started && !BIT_IS_SET(asteroid_state[array_pos[count]],(DRAWN|MOVING)))
                    {
                        asteroid_state[array_pos[count]] = (1<<DRAWN)|(1<<MOVING);
                        count++;
                    }
                    if(count>=3)
                    {
                        wave_started=0;
                        count =0;
                    }
                    spawn_delay = 0;
                }
            }
        }

    }

    if(!BIT_IS_SET(gamestate,PAUSED)|| BIT_IS_SET(gamestate,CHEATED))
    {
        if(fireTimer >= 0.2 )
        {
            fired=0;
            fireTimer=0;
        }
    }
}

/**
*   Functions responsible for deciding what direction to shift to
*   based on input and current direction
*/
void move_left()
{
    if(direction == RIGHT)
    {
        direction=NEUTRAL;
    }
    else
    {
        direction = LEFT;

    }
}
void move_right()
{
    if(direction == LEFT)
    {
        direction=NEUTRAL;
    }
    else
    {
        direction = RIGHT;
    }
}



// ----------------------------------------------------------

// ---------------------------------------------------------
//	Serial input/output and game status.
// ---------------------------------------------------------


char serial_out_buffer[30];
/**
*   Function for sending a string containing an integer value and a sting back
*   to the serial console.
*
*   Parameters:
*           int_in: An integer to be sent suffixed to the message
*           message: A string prefixing the integer value
*/
void usb_serial_sent_int(int16_t int_in, char * message)
{
    snprintf( serial_out_buffer, sizeof(serial_out_buffer),"%s%d\r\n", message, int_in );
    usb_serial_send( serial_out_buffer );
}

/**
*   Function for sending a string to the serial console.
*
*   Parameters:
*           message: A string representing the message to be sent to
*               the serial console.
*/
void usb_serial_send(char * message)
{
    // Cast to avoid "error: pointer targets in passing argument 1
    //	of 'usb_serial_write' differ in signedness"
    usb_serial_write((uint8_t *) message, strlen(message));
}

/**
*   Function responsible for initialising methods required to set up the serial
*   connection, the device will not go any further than this function until the
*   teensy has successfully connected with a device.
*/
void setup_usb_serial(void)
{
    // Set up LCD and display message
    new_lcd_init(LCD_DEFAULT_CONTRAST);
    lcd_clear();
    draw_string(10, 10, "Connect USB...", FG_COLOUR);
    show_screen();

    usb_init();

    while ( !usb_configured() )
    {
        // Block until USB is ready.
    }
}


/**
*   Function responsible for sending the game status to the serial console when
*   called.
*/
void status_to_serial()
{
    usb_serial_sent_int((int)game_time,"\r\nGame Time:");
    usb_serial_sent_int((int)shield_life,"Shield Life Remaining:");
    usb_serial_sent_int((int)score,"Score:");
    usb_serial_sent_int((int)asteroid_count,"Asteroid Count:");
    usb_serial_sent_int((int)boulder_count,"Boulder Count:");
    usb_serial_sent_int((int)frag_count,"Fragment Count:");
    usb_serial_sent_int((int)projectile_count,"Projectile Count:");
    usb_serial_sent_int((int)tx*20,"Turret angle:");
    usb_serial_sent_int((int)game_speed*10,"Game Speed:");
}

/**
*   Function for showing controls in the serial console
*
*   Note: Function not implemented due to it causing stack collision,
*       this was the last function to be written in the program.
*/
//void show_help()
//{
//    usb_serial_send("********Controls********");
//    usb_serial_send("'a' – move spaceship left \r\n 'd' – move spaceship right");
//    usb_serial_send("'w' – fire plasma bolts \r\n 's' – send and display game status");
//    usb_serial_send("'r' – start/reset game \r\n 'p' – pause game");
//    usb_serial_send("'q' – quit \r\n 't' – set aim of the turret");
//    usb_serial_send("'m' – set the speed of the game \r\n 'l' – set the remaining useful life of the deflector shield");
//    usb_serial_send("'g' – set the score \r\n 'h' – move spaceship to coordinate");
//    usb_serial_send("'j' – place asteroid at coordinate \r\n 'k' – place boulder at coordinate");
//    usb_serial_send("'i' – place fragment at coordinate \r\n '?' – show this screen");
//}

/**
*   Function responsible for sending the game status to the teensy screen when
*   called.
*/
void status_to_screen()
{
    draw_string(0,0,"Time: ",FG_COLOUR);
    draw_int(30,0,(int)game_time,FG_COLOUR);
    draw_string(0,10,"Life: ",FG_COLOUR);
    draw_int(30,10,shield_life,FG_COLOUR);
    draw_string(0,20,"Score: ",FG_COLOUR);
    draw_int(30,20,score,FG_COLOUR);
}

// ---------------------------------------------------------
//	Console cheats
// ---------------------------------------------------------

uint8_t new_x,new_y;
/**
*   Function responsible for checking the new_x, new_y input for all
*   object moving cheats to see if they are withing the screen bounds
*   if not the value is modified.
*
*   Parameters:
*           object: The object name defined that represents the object
*                  being checked
*
*   Notes: The value assigned to the object name is the width value of the
*           object, so it is used in this instance as an easier way to
*           define a new position when the object is greater than the screen
*           bounds.
*/
void boundry_check(uint8_t object)
{
    if(new_x <0)
        new_x=0;
    if(new_x > LCD_X)
        new_x = LCD_X - object;
    if(new_y <0)
        new_y=0;
    if(new_y > 39)
        new_y = 39 - object;
}

/**
*   Function responsible for handling all cheats that require an object to be
*   moved to a new position on the screen.
*
*   Parameters:
*           object_mask: The defined name that represents the object being moved.
*/
void do_move_object(uint8_t object_mask)
{
    // When called the serial console requests an x,y coordinate
    // if its the ship it will only ask for an x.
    usb_serial_send("Input x coordinates: - (0-84)\r\n");
    gamestate |= (1<<INPUT);
    while(BIT_IS_SET(gamestate,INPUT))
    {
        new_x = receive_serial_cheat();
    }

    if(object_mask!=SHIP)
    {
        usb_serial_send("Input y coordinates: - (0-39)\r\n");
        gamestate |= (1<<INPUT);
        while(BIT_IS_SET(gamestate,INPUT))
        {
            new_y = receive_serial_cheat();
        }
    }
    // All images on the screen are sent back to pool and
    // the single object is move to the requested position
    // on screen.
    setup_images();
    if(object_mask == ASTEROID)
    {
        boundry_check(ASTEROID);
        ax[1] = new_x;
        ay[1]= new_y;
        usb_serial_send("Asteroid moved\r\n");
        asteroid_state[1] = (1<<DRAWN)|(0<<MOVING);
    }
    if(object_mask == BOULDER)
    {
        boundry_check(BOULDER);
        bx[1] = new_x;
        by[1]= new_y;
        boulder_state[1] = (1<<DRAWN)|(0<<MOVING);
        usb_serial_send("Boulder moved\r\n");
    }
    if(object_mask == FRAGMENT)
    {
        boundry_check(FRAGMENT);
        fx[1] = new_x;
        fy[1]= new_y;
        fragment_state[1] = (1<<DRAWN)|(0<<MOVING);
        usb_serial_send("Fragment moved\r\n");
    }
    gamestate |= (1<<CHEATED) | (1<<PAUSED);
    if(object_mask==SHIP)
    {
        boundry_check(SHIP);
        ship_x = new_x;
        direction = NEUTRAL;
        usb_serial_send("ship moved\r\n");
        gamestate &= (0<<CHEATED) | (0<<PAUSED);
    }
}

/**
*   Function that handle all overriding of values when called on
*   for debugging.
*
*   Parameters:
*           in_char: The character pressed sent from there serial console.
*/
void do_override(char in_char)
{
    int override_tmp;
    usb_serial_send("Input new value: \r\n");
    gamestate |= (1<<INPUT);

    while(BIT_IS_SET(gamestate,INPUT))
    {
        override_tmp = receive_serial_cheat();
    }
    if(override_tmp < 0)
        override_tmp=0;
    if(in_char =='g')
    {
        score = override_tmp;
    }
    if(in_char =='l')
    {
        shield_life = override_tmp;
    }
    if(in_char == 'm')
    {
        // Sets the override timer to zero and starts the 1 seconds count
        speed_override=1;
        if(override_tmp>100)
            override_tmp=100;
        game_speed = override_tmp/10;
        return_manual=0;
    }
    gamestate |= (1<<CHEATED);
}

/**
*   Function responsible for overriding the potentiometer value for
*   the turret control.
*/
void overrride_turret()
{
    usb_serial_send("Enter turret heading: - (-60 to 60)\r\n");
    gamestate |= (1<<INPUT);
    int tmp;
    while(BIT_IS_SET(gamestate,INPUT))
    {
        tmp = receive_serial_cheat();

    }

    turret_override=1;
    if(tmp < -60)
        tmp=-60;
    if(tmp > 60)
        tmp=60;
    tx = (int)tmp/30;
    usb_serial_send("Turret heading changed\r\n");
    // Sets the override timer to zero and starts the 1 seconds count
    return_manual=0;

    gamestate |= (1<<PAUSED);
}

// ----------------------------------------------------------

/**
*   Function for handling the serial key presses that act as the secondary
*   and debug controls.
*/
void serial_input(int16_t char_code)
{
    if(char_code == 'a')
    {
        move_left();
    }
    if(char_code == 'd')
    {
        move_right();
    }
    if(char_code == 'w')
    {
        fire_plasma_bolt();
    }
    if(char_code == 's')
    {
        if(BIT_IS_SET(gamestate,PAUSED))
        {
            status_screen=!status_screen;
        }
        status_to_serial();
    }
    if(char_code == 'r')
    {
        if(!intro_screen)
        {
            // If previous task was a debug cheat then reset the game in a
            // paused state and do not start.
            if(BIT_IS_SET(gamestate,CHEATED))
            {
                setup_gamestate();
                gamestate |= (1<<PAUSED);
                gamestate &= (0<<START);
            }
            // If the gamestate START is set then the current screen has come after
            // the intro screen and the press will register as start not restart.
            if(!BIT_IS_SET(gamestate,CHEATED) && BIT_IS_SET(gamestate,START))
            {
                gamestate ^= (1<<PAUSED);
                gamestate &= (0<<START);
            }
            else
            {
                setup_gamestate();
            }
        }

        intro_screen=0;
    }
    if(char_code == 'p')
    {
        if(BIT_IS_SET(gamestate,CHEATED))
        {
            setup_gamestate();
            gamestate &= (0<<CHEATED);
        }
        gamestate ^= (1<<PAUSED);
    }
    if(char_code == 'q')
    {
        gamestate |= (1<<QUIT);
    }
    if(char_code == 'o')
    {
        overrride_turret();
    }
    if(char_code == 'm')
    {
        do_override('m');
    }
    if(char_code == 'l')
    {
        do_override(char_code);
    }
    if(char_code == 'g')
    {
        do_override('g');
    }
    if(char_code == '?')
    {
        //show_help();
    }
    if(char_code == 'h')
    {
        do_move_object(SHIP);
    }
    if(char_code == 'j')
    {
        do_move_object(ASTEROID);
    }
    if(char_code == 'k')
    {
        do_move_object(BOULDER);
    }
    if(char_code == 'i')
    {
        do_move_object(FRAGMENT);
    }
}

/**
*   Function for determining the heading needed to travel from point a to b
*
*   Parameters:
*           ax: The x position of the starting point
*           ay: The y position of the starting point
*           bx: The x position of the finish point
*           by: The y position of the finish point
*
*   Note: This is recycled code from Assignment 1
*/
double get_angle(double ax,double ay,double bx,double by)
{
    double x1 = ax-(1/2);
    double y1 = ay-(1/2);
    double x2 = bx-(1/2);
    double y2 = by-(1/2);
    double y = (y2-y1);
    double x = (x2-x1);
    double angle = atan2(y,x);
    return angle;
}

/**
*   Function responsible for setting the position and heading of the plasma
*   bold when fired, it also handles sets the the state fired to true which
*   will only be reset to 0 every 0.2 seconds giving the cannon a rate of fire
*   of no more than 3 bolts per second.
*/
void fire_plasma_bolt()
{
    for(uint8_t i=0; i<MAX_PROJECTILE; i++)
    {
        if(!BIT_IS_SET(projectile_state[i],DRAWN)&&!fired)
        {
            px[i]=(ship_x+8)+tx;
            py[i]=ty;
            projectile_heading[i]=get_angle(ship_x+7,44, (ship_x+7)+tx, ty);
            projectile_state[i] = (1<<DRAWN)|(1<<MOVING);
            fired=1;
        }
    }
}
// ----------------------------------------------------------

// ---------------------------------------------------------
//	Teensy input.
// ---------------------------------------------------------
/**
*   Function that ties in with the previous debouncing function for the switched
*   this handles all teensy input and assigns appropriate functions to their
*   respective inputs.
*/
void peripheral_input()
{
    static uint8_t joy_up_prevState = 0;
    if(joy_up_closed != joy_up_prevState)
    {
        joy_up_prevState = joy_up_closed;
        if(joy_up_prevState==1)
        {
            fire_plasma_bolt();
        }
    }
    static uint8_t joy_down_prevState = 0;
    if(joy_down_closed != joy_down_prevState)
    {
        joy_down_prevState = joy_down_closed;
        if(joy_down_prevState==1)
        {
            if(BIT_IS_SET(gamestate,PAUSED))
            {
                status_screen=!status_screen;
            }
            status_to_serial();
        }
    }
    static uint8_t joy_left_prevState = 0;
    if(joy_left_closed != joy_left_prevState)
    {
        joy_left_prevState = joy_left_closed;
        if(joy_left_prevState==1)
        {
            move_left();
        }
    }
    static uint8_t joy_right_prevState = 0;
    if(joy_right_closed != joy_right_prevState)
    {
        joy_right_prevState = joy_right_closed;
        if(joy_right_prevState==1)
        {
            move_right();
        }
    }
    static uint8_t joy_center_prevState = 0;
    if(joy_center_closed != joy_center_prevState)
    {
        joy_center_prevState = joy_center_closed;
        if(joy_center_prevState==1)
        {
            gamestate ^= (1<<PAUSED);
        }
    }
    static uint8_t SW1_prevState = 0;
    if(SW1_closed != SW1_prevState)
    {
        SW1_prevState = SW1_closed;
        if(SW1_prevState==1)
        {
            if(!intro_screen)
            {
                if(BIT_IS_SET(gamestate,CHEATED))
                {
                    setup_gamestate();
                }
                if(BIT_IS_SET(gamestate,START) && !BIT_IS_SET(gamestate,CHEATED))
                {
                    gamestate ^= (1<<PAUSED);
                    gamestate &= (0<<START);
                }
                else
                {
                    setup_gamestate();
                }
            }

            intro_screen=0;
        }
    }
    static uint8_t SW2_prevState = 0;
    if(SW2_closed != SW2_prevState)
    {
        SW2_prevState = SW2_closed;
        if(SW2_prevState==1)
        {
            usb_serial_send("SW2 - quit \r\n");
            gamestate |= (1<<QUIT);
        }
    }
}
// ----------------------------------------------------------

// ---------------------------------------------------------
//	Draw stuff.
// ---------------------------------------------------------
uint8_t ship[8] =
{
    0x18,
    0x3C,
    0x3C,
    0x7F,
    0x7E,
    0xDB,
    0xC3
};
uint8_t ship_direct[8];

uint8_t asteroid[8] =
{
    0x10,
    0x38,
    0x7C,
    0xFE,
    0x7C,
    0x38,
    0x10
};
uint8_t asteroid_direct[8];

uint8_t boulder[8] =
{
    0x20,
    0x70,
    0xF8,
    0x70,
    0x20,
};
uint8_t boulder_direct[8];

uint8_t fragment[8] =
{
    0x40,
    0xE0,
    0x40
};
uint8_t fragment_direct[8];

/**
*   Function to setup the images flipping them around so that when used in
*   a for loop with WRITE_BIT to draw to screen everything is oriented the
*   right way.
*/
void format_images()
{
    for (uint8_t i = 0; i < 8; i++)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint8_t bit_val = BIT_VALUE(ship[j], (7 - i));
            WRITE_BIT(ship_direct[i], j, bit_val);
        }
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint8_t bit_val = BIT_VALUE(asteroid[j], (7 - i));
            WRITE_BIT(asteroid_direct[i], j, bit_val);
        }
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint8_t bit_val = BIT_VALUE(boulder[j], (7 - i));
            WRITE_BIT(boulder_direct[i], j, bit_val);
        }
    }
    for (uint8_t i = 0; i < 8; i++)
    {
        for (uint8_t j = 0; j < 8; j++)
        {
            uint8_t bit_val = BIT_VALUE(fragment[j], (7 - i));
            WRITE_BIT(fragment_direct[i], j, bit_val);
        }
    }
}

/**
*   Function responsible for the initial asteroid setup
*
*   Parameters:
*            i: The integer representing the asteroid position in the array.
*/
void setup_asteroid(uint8_t i)
{
    //Split the screen into thirds and grab the rand range for each asteroid
    uint8_t upper = (uint8_t)((i+1) * 24);
    uint8_t lower = (uint8_t)(i* (LCD_X /3));
    ax[i]= (rand() % (upper - lower + 1)) + lower;

    asteroid_state[i] = (0<<DRAWN) | (0<<MOVING);
}

/**
*   Function for setting up any child objects of destroyed parent objects.
*
*   Parameters:
*           i: An integer representing the position in the given  array
*           x[]: A double precision floating point representing the array of
*                 x positions for the given object type.
*           y[]: A double precision floating point representing the array of
*                 y positions for the given object type.
*           dx[]: A double precision floating point representing the array of
*                 x offset values for the given object type.
*           dy[]: A double precision floating point representing the array of
*                 y offset values for the given object type.
*           px: A double precision floating point representing the x value of
*                the parent object associated with these objects.
*           py: A double precision floating point representing the y value of
*                the parent object associated with these objects.
*
*   Notes:
*      Working behind creating an object pool for all
*      falling objects, this way each asteroid can have
*      two boulders and each boulder can have two fragments
*      assigned to it. By multiplying by i*2 and (i*2)+1 the
*      array position of the two objects assigned to the parent
*      can be determined. To get the parent position from the array
*      position of the child object (int)floor(i/2) is used.
*      ********Map of array positions for objects*******
*            0           1           2       <-- Asteroid
*          /   \       /   \       /   \
*         0     1     2     3     4     5    <-- Boulder
*        / \   / \   / \   / \   / \   / \
*       0   1 2   3 4   5 6   7 8   9 10 11  <-- Fragment
*      This method can handle the positioning of objects more efficiently
*      as it can cherry pick the needed child object from the pool rather
*      than iterating through an finding the first object that is not in play.
*/
void setup_child_object(int i, double x[], double y[], double dx[], double dy[], double px, double py)
{
    // Seeings that the heading is clamped between 60-90 and 90-120 it is safe to use
    // a uint8_t in place of an int to save some space.
    uint8_t randHead1 = rand()%(90-60 +1)+60;
    uint8_t randHead2 = rand()%(120 - 90 +1)+90;
    y[i]= py;
    y[i+1]= py;
    x[i]= px;
    x[i+1] = px;
    dy[i] = (double)(rand()% (15-2 +1)+2)/10;
    dy[i+1] = (double)(rand()% (15-2 +1)+2)/10;
    dx[i] = 0.8 * cos(randHead2);
    dx[i+1] = 0.8 * cos(randHead1);
}

/**
*   Function responsible for setting/resetting the state and position of the
*   objects.
*
*   Note: On setup and reset all object are send to a pool off screen at -15
*       this is know as object pooling in game design and is generally used
*       to offset the cost of instantiating objects on the fly, it does not
*       have that effect in this case but is used for the convenience of moving
*       objects around rather than removing and replacing them.
*/
void setup_images(void)
{
    format_images();
    count = 0;
    wave_started=0;
    for(uint8_t j=0; j<MAX_ASTEROID; j++)
    {
        setup_asteroid(j);
    }

    for(uint8_t y =0; y<MAX_ASTEROID; y++)
    {
        ay[y]=POOL_COORDINATES;
        asteroid_state[y]=0;
    }
    for(uint8_t k=0; k<MAX_BOULDER; k++)
    {
        by[k]=POOL_COORDINATES;
        boulder_state[k]=0;
    }
    for(uint8_t f =0; f<MAX_FRAG; f++)
    {
        fy[f]=POOL_COORDINATES;
        fragment_state[f]=0;
    }
    for(uint8_t i=0; i<MAX_PROJECTILE; i++)
    {
        py[i]=PROJECTILE_POOL;
        projectile_state[i]=0;
    }

}

/**
*   Function responsible for drawing all objects in game
*
*   Parameters:
*           top_left_x: An integer representing the x position that is the top left
*                       position of the image.
*           top_left_y: An integer representing the y position that is the top left
*                       position of the image.
*           pixels[]: An array representing the formatted image to transfered to the screen
*                     buffer.
*           width: An integer representing the width of the object.
*           height: An integer representing the height of the object.
*/
void draw_object(int top_left_x, int top_left_y, uint8_t pixels[], int width, int height)
{
    for ( uint8_t i = 0; i < width; i++ )
    {
        uint8_t pixel_data = pixels[i];
        for ( uint8_t j = 0; j < height; j++ )
        {
            draw_pixel(top_left_x + i, top_left_y + j, (pixel_data & (1 << j)) >> j);
        }
    }
}

/**
*   Function responsible for drawing a barrier at the bottom of the screen
*/
void draw_barrier()
{
    for(int i=0; i<84; i++)
    {
        if(i%2==0)
        {
            draw_pixel(i,39,FG_COLOUR);
        }
        else
        {
            draw_pixel(i,39,BG_COLOUR);
        }
    }
}

/**
*   Function responsible for determining whether the object is about to leave
*   screen space and sending it on it opposite x direction if it is.
*
*   Parameters:
*           i: An integer representing the array position of the object.
*           x[]: An array of x positions associated with the current object type.
*           dx[]: An array of x offsets associated with the current object type.
*/
void bounce(int i, double x[], double dx[])
{
    uint8_t new_x = round(x[i] + dx[i]) - 5/2;

    if(new_x <= 0 || new_x + 5 >= 84)
    {
        dx[i] = -dx[i];
    }
}

/**
*   Function responsible for the moving and setting projectile state
*
*   Parameters:
*           i: An integer representing the position of the projectile
*               in the projectile array.
*/
void draw_projectile(int i)
{
    // Cheat flag is used to allow the ship to fire while paused only if
    // a debug input has been used.
    if(!BIT_IS_SET(gamestate,PAUSED)|| BIT_IS_SET(gamestate,CHEATED))
    {
        projectile_tick[i]++;
        if(projectile_tick[i]*ship_speed>10)
        {
            if(BIT_IS_SET(projectile_state[i],MOVING))
            {
                pdx[i]=cos(projectile_heading[i]);
                pdy[i]=sin(projectile_heading[i]);
                py[i] += pdy[i];
                px[i] += pdx[i];
            }
            projectile_tick[i]=0;
        }
        if(py[i] < 0 || px[i] > 84 || px[i] < 0)
        {
            projectile_state[i] = (0<<DRAWN) | (0<<MOVING);
        }
    }

    if(BIT_IS_SET(projectile_state[i],DRAWN))
    {
        for(uint8_t j =0; j<2; j++)
        {
            for(uint8_t k =0; k<2; k++)
            {
                draw_pixel(px[i]+j, py[i]+k, 1);
            }
        }
    }
}

/**
*   Function responsible for the moving and setting fragment state
*   also detects collisions with projectiles.
*
*   Parameters:
*           i: An integer representing the position of the fragment
*               in the fragment array.
*
*   Notes:
*       Function handles everything to do with the fragment, it sets
*       the rate at which each fragment moves, heading it moves in,
*       checks for any collision with projectiles, and whether the
*       fragment has collided with the shield.
*/
void draw_fragment(int i)
{
    if(!BIT_IS_SET(gamestate,PAUSED)|| BIT_IS_SET(gamestate,CHEATED))
    {
        fragment_tick[i]++;
        if(fragment_tick[i]*game_speed>10)
        {
            if(BIT_IS_SET(fragment_state[i],MOVING))
            {
                bounce(i,fx,fdx);
                fy[i] += fdy[i];
                fx[i] += fdx[i];
            }
            fragment_tick[i]=0;
        }
    }

    // Check if any projectiles have collided
    for(uint8_t j=0; j<MAX_PROJECTILE; j++)
    {
        if(px[j] >= fx[i] && px[j] <= fx[i]+3 && py[j] >= by[i] && py[j] <= fy[i]+3&& fy[i]>0)
        {
            fragment_state[i] = (1<<BROKEN);
            px[j]=py[j]= PROJECTILE_POOL;
            projectile_state[j] = (0<<DRAWN) | (0<<MOVING);
            score += 4;
        }
    }

    // If collided with shield or projectile turn off and move to pool
    if(fy[i]>39-3 || BIT_IS_SET(fragment_state[i],BROKEN))
    {
        // If collided with shield reduce shield life
        if(fy[i]>39-3)
        {
            shield_life--;
        }

        fy[i]=-10;
        fragment_state[i] = (0<<MOVING)|(0<<DRAWN);
    }

    if(!BIT_IS_SET(fragment_state[i],BROKEN))
    {
        draw_object(fx[i],fy[i],fragment_direct,3,3);
    }

}

/**
*   Function responsible for the moving and setting boulder state
*   also detects collisions with projectiles.
*
*   Parameters:
*           i: An integer representing the position of the boulder
*               in the boulder array.
*
*   Notes:
*       Function handles everything to do with the boulder, it sets
*       the rate at which each boulder moves, heading it moves in,
*       checks for any collision with projectiles, when collided with
*       a projectile it sets the fragments associated with the boulder
*       up at the place the boulder was destroyed, and whether the
*       boulder has collided with the shield.
*/
void draw_boulder(int i)
{
    if(!BIT_IS_SET(gamestate,PAUSED)|| BIT_IS_SET(gamestate,CHEATED))
    {
        boulder_tick[i]++;
        if(boulder_tick[i]*game_speed>10)
        {
            if(BIT_IS_SET(boulder_state[i],MOVING))
            {
                bounce(i,bx,bdx);
                by[i] += bdy[i];
                bx[i] += bdx[i];
            }
            boulder_tick[i]=0;
        }

    }

    // Check if any projectiles have collided
    for(uint8_t j=0; j<MAX_PROJECTILE; j++)
    {
        if(((px[j] >= bx[i] && px[j] <= bx[i]+5) && (py[j] >= by[i] && py[j] <= by[i]+5))&& by[i]>0)
        {
            boulder_state[i] = (1<<BROKEN);
            px[j]=py[j]= PROJECTILE_POOL;
            projectile_state[j] = (0<<DRAWN) | (0<<MOVING);
            score += 2;
        }
    }

    // If collided with shield or projectile turn off and move to pool
    if(by[i]>39-5 ||BIT_IS_SET(boulder_state[i],BROKEN))
    {

        // If collided with projectile set up child objects.
        if(BIT_IS_SET(boulder_state[i],BROKEN))
        {
            setup_child_object((i*2),fx,fy,fdx,fdy,bx[i],by[i]);
            fragment_state[i*2] = (0<<BROKEN)|(1<<DRAWN)|(1<<MOVING);
            fragment_state[(i*2)+1] =(0<<BROKEN)|(1<<DRAWN)|(1<<MOVING);
            boulder_state[i] = (0<<BROKEN);
        }
        // If collided with shield reduce shield life
        if(by[i]>39-5)
        {
            shield_life--;
        }

        by[i]=-10;

        boulder_state[i] = (0<<MOVING) | (0<<DRAWN);

    }

    if(!BIT_IS_SET(boulder_state[i],BROKEN))
    {
        draw_object(bx[i],by[i],boulder_direct,5,5);
    }
}

/**
*   Function responsible for the moving and setting asteroid state
*   also detects collisions with projectiles.
*
*   Parameters:
*           i: An integer representing the position of the asteroid
*               in the asteroid array.
*
*   Notes:
*       Function handles everything to do with the asteroid, it sets
*       the rate at which each asteroid moves, heading it moves in,
*       checks for any collision with projectiles, when collided with
*       a projectile it sets the boulders associated with the asteroid
*       up at the place the asteroid was destroyed, and whether the
*       asteroid has collided with the shield.
*/
void draw_asteroid(int i)
{

    if(!BIT_IS_SET(gamestate,PAUSED)|| BIT_IS_SET(gamestate,CHEATED))
    {
        asteroid_tick[i]++;
        if(asteroid_tick[i]*game_speed>10)
        {
            if(BIT_IS_SET(asteroid_state[i],MOVING))
            {
                ay[i]++;
            }
            asteroid_tick[i]=0;
        }
    }

    // If collided with projectile set up child objects.
    for(uint8_t j=0; j<MAX_PROJECTILE; j++)
    {
        if(px[j] >= ax[i] && px[j] <= ax[i]+7 && py[j] >= ay[i] && py[j] <= ay[i]+7&& ay[i]>0)
        {
            asteroid_state[i] = (1<<BROKEN);
            px[j]=py[j]= PROJECTILE_POOL;
            projectile_state[j] = (0<<DRAWN) | (0<<MOVING);
            score += 1;
        }
    }

    // If collided with shield or projectile turn off and move to pool
    if(ay[i]>39-7 ||BIT_IS_SET(asteroid_state[i],BROKEN))
    {
        // If collided with projectile set up child objects.
        if(BIT_IS_SET(asteroid_state[i],BROKEN))
        {

            setup_child_object((i*2),bx,by,bdx,bdy,ax[i],ay[i]);

            boulder_state[i*2] = (0<<BROKEN) |(1<<DRAWN) |(1<<MOVING);
            boulder_state[(i*2)+1] = (0<<BROKEN) |(1<<DRAWN) |(1<<MOVING);
        }
        // If collided with shield reduce shield life
        if(ay[i]>39-7 )
        {
            shield_life--;
        }

        ay[i]=-10;
        setup_asteroid(i);

        asteroid_state[i] = (0<<DRAWN) | (0<<MOVING);
    }

    if(!BIT_IS_SET(asteroid_state[i],BROKEN))
    {
        draw_object(ax[i],ay[i],asteroid_direct,7,7);
    }
}

/**
*   Function responsible for updating all images on each cycle
*/
void draw_update()
{
    draw_barrier();
    for(uint8_t i=0; i<MAX_ASTEROID; i++)
    {
        draw_asteroid(i);

    }
    for(uint8_t j=0; j<MAX_BOULDER; j++)
    {
        draw_boulder(j);
    }
    for(uint8_t k=0; k<MAX_FRAG; k++)
    {
        draw_fragment(k);
    }
    for(uint8_t l=0; l<MAX_PROJECTILE; l++)
    {
        draw_projectile(l);
    }

    // Draw the ship
    draw_object(ship_x,41,ship_direct,8,8);

    // Draw the turret
    draw_line(ship_x+7,44, (ship_x+7)+tx, ty, FG_COLOUR );
    draw_line(ship_x+8,44, (ship_x+8)+tx, ty, FG_COLOUR );
}
// ----------------------------------------------------------


void get_pot_values()
{
    if(!turret_override)
    {
        int left_acd = adc_read(0);
        double tmp = (double)(left_acd*7/1024);
        tmp = tmp-3;
        tx= tmp;
    }
    if(!speed_override)
    {
        int right_acd = adc_read(1);
        double tmp = (double)(right_acd*11/1024);
        game_speed = tmp;
    }
}
void set_duty_cycle(int duty_cycle)
{
    TC4H = duty_cycle >> 8;

    // (b)	Set bits 0..7 of Output Compare Register 4A.
    OCR4A = duty_cycle & 0xff;
}


/**
*   Function responsible for parsing the input from the serial console
*
*   Return: An integer representing the parsed integer from the serial input
*
*   Notes:
*       This has been set up in a while loop intentionally to pause the game
*       wile input is being received, this stopped issues that arose when
*       the game would end while debugging commands where being input.
*       Not Complete, nothing in place to stop input once buffer is full
*       buffer is set up as a ring buffer for the moment.
*/
int receive_serial_cheat()
{
    uint16_t in_char;
    char * line1 = "Receiving";
    char * line2 = "Input";
    uint8_t pos = 0;

    // Loop parser while input is set.
    while(BIT_IS_SET(gamestate,INPUT))
    {
        //gamestate |= (1<<CHEATED) | (1<<PAUSED);
        draw_string((LCD_X/2)-((strlen(line1)*5)/2),(LCD_Y/2)-10,line1,FG_COLOUR);
        draw_string((LCD_X/2)-((strlen(line2)*5)/2),(LCD_Y/2),line2,FG_COLOUR);
        show_screen();

        if(usb_serial_available())
        {
            in_char = usb_serial_getchar();

            // if character not enter add to buffer
            if(in_char!=13)
            {
                in_buff[pos]=in_char;
                if(pos>3)
                    pos=0;
                pos++;
            }


            if(in_char==13)
            {
                gamestate &= (0<<INPUT);
                //gamestate |= (1<<CHEATED) | (1<<PAUSED);
            }
        }
    }

    // Take the string
    char arr[1][3];
    int n, ret;
    n=serial_to_int(in_buff,arr);
    int t = atoi(arr[0]);
    ret=t;

    return t;
}



void input()
{
    int16_t char_code = usb_serial_getchar();

    if ( char_code >= 0 )
    {
        serial_input(char_code);
    }
    peripheral_input();
}

int serial_to_int(char *base, char target[1][3])
{
    int n=0,i,j=0;

    for(uint8_t i=0; 1; i++)
    {
        if(base[i]!=' ')
        {
            target[n][j++]=base[i];
        }
        else
        {
            target[n][j++]='\0';//insert NULL
            n++;
            j=0;
        }
        if(base[i]=='\0')
            break;
    }
    return n;

}

void debug_draw()
{

}

uint8_t print=0;
void game_over_stuff()
{
    set_duty_cycle(lcd_led_value);
    char *game_over_message = "GAME OVER";
    char *restart_message = "SW1 - Restart";
    char *quit_message = "SW2 - Quit";

    if(BIT_IS_SET(gamestate,OVER)&&!BIT_IS_SET(gamestate,OVER_CHOICE))
    {
        dim_lcd=1;
        gamestate |= (1<<PAUSED);

        if(lcd_led_value>=1022)
        {
            SET_BIT(PORTB,2);
            SET_BIT(PORTB,3);
            clear_screen();
            draw_string(LCD_X/2-((strlen(game_over_message)*5)/2),LCD_Y/2-5,game_over_message,FG_COLOUR);
            show_screen();
            _delay_ms(2000);
            CLEAR_BIT(PORTB,2);
            CLEAR_BIT(PORTB,3);
            //shield_life=1;
            gamestate |= (1<<OVER) | (1<<OVER_CHOICE);
            dim_lcd =0;

            print=1;
        }
    }
    if(BIT_IS_SET(gamestate,OVER)&&BIT_IS_SET(gamestate,OVER_CHOICE))
    {
        if(print)
        {
            //gamestate = (1<<PAUSED)|(1<<OVER_CHOICE)|(0<<OVER);
            usb_serial_send("*********GAME OVER**********\r\n");
            status_to_serial();
            print=!print;
        }

//            status_to_serial();
        draw_string(LCD_X/2 - (8*5),LCD_Y/2-10,restart_message,FG_COLOUR);
        draw_string(LCD_X/2 - (8*5),LCD_Y/2,quit_message,FG_COLOUR);

    }
    //gamestate |= (1<<PAUSED)|(1<<OVER_CHOICE)|(0<<OVER);
    //show_screen();

}

// Was implemented but taken out due to not having been able to figure out direct
// drawing the turret in time.
//void direct_draw_ship()
//{
//    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
//    LCD_CMD(lcd_set_x_addr, ship_x);
//    LCD_CMD(lcd_set_y_addr, 40 / 8);
//
//    for (int i = 0; i < 9; i++) {
//        LCD_DATA(ship_direct[i]);
//    }
//}
//
//void clear_last_bank()
//{
//    LCD_CMD(lcd_set_function, lcd_instr_basic | lcd_addr_horizontal);
//    LCD_CMD(lcd_set_x_addr, 0);
//    LCD_CMD(lcd_set_y_addr, 40 / 8);
//
//    for (int i = 0; i < LCD_X; i++) {
//        LCD_DATA(0);
//    }
//}

uint8_t x,y;
void intro()
{
    set_duty_cycle(lcd_led_value);

    char * title1 = "SPACE PEW PEW";

    if(y>LCD_Y)
    {
        x=rand()%LCD_X;
        y=-10;
    }
    y++;
    draw_object(x,y,asteroid,7,7);

    draw_string(LCD_X/2-(strlen(student_num)/2*5),10,student_num,FG_COLOUR);
    draw_string(LCD_X/2-(strlen(title1)/2*5),20,title1,FG_COLOUR);
}

//void display_gamestates()
//{
//    draw_int(0,20,BIT_IS_SET(gamestate,CHEATED),FG_COLOUR);
//    draw_int(10,20,BIT_IS_SET(gamestate,START),FG_COLOUR);
//    draw_int(20,20,BIT_IS_SET(gamestate,OVER),FG_COLOUR);
//    draw_int(30,20,BIT_IS_SET(gamestate,OVER_CHOICE),FG_COLOUR);
//    draw_int(40,20,BIT_IS_SET(gamestate,INPUT),FG_COLOUR);
//    draw_int(50,20,BIT_IS_SET(gamestate,PAUSED),FG_COLOUR);
//    draw_int(60,20,BIT_IS_SET(gamestate,QUIT),FG_COLOUR);
//    draw_int(0,30,turret_override,FG_COLOUR);
//    draw_int(10,30,speed_override,FG_COLOUR);
//    draw_int(20,30,(int)return_manual,FG_COLOUR);
//}

void process(void)
{

    input();

    clear_screen();
    if(intro_screen)
    {
        intro();
    }
    else
    {
        get_pot_values();
        draw_update();
        if(BIT_IS_SET(gamestate,PAUSED) && status_screen)
        {
            status_to_screen();
        }
        //display_gamestates();
        game_over_stuff();
    }
    show_screen();

    if(!BIT_IS_SET(gamestate,PAUSED))
    {
        ship_movement();
    }
    if(shield_life<1 &&!BIT_IS_SET(gamestate,OVER_CHOICE))
    {
        gamestate |= (1<<OVER)|(1<<PAUSED);
    }

    //clear_last_bank();
    //direct_draw_ship();
}
// ----------------------------------------------------------

int rand_seed()
{
    int seed1, seed2;
    seed1=adc_read(0);
    seed2=adc_read(1);
    return seed1+124*seed2+698;
}

void quit_screen()
{
    uint8_t i,j;
    clear_screen();
    for(i=0; i<LCD_X; i++)
        for(j=0; j<LCD_Y; j++)
        {
            draw_pixel(i, j, 1);
        }


    draw_string(LCD_X/2-(strlen(student_num)*5 /2),(LCD_Y/2)-2,student_num,BG_COLOUR);
    show_screen();
    input();
}

// ----------------------------------------------------------
//	Setup
// ----------------------------------------------------------


void setup(void)
{
    teensy_init();
    setup_usb_serial();
    setup_images();
    setup_gamestate();
}

void setup_gamestate(void)
{
    gamestate = (0<<CHEATED)| (1<<START)| (0<<OVER)| (0<<OVER_CHOICE)|(0<<INPUT) | (1<<PAUSED) | (0<<QUIT);
    status_screen=0;
    score=0;
    flash_led=0;
    shield_life=5;
    ship_x=42-4;
    game_time=0;

    if(rand()% 10 +1 > 5)
        direction = LEFT;
    else
        direction = RIGHT;
    ty=41;

    // Placed in here rather than setup so that this can be called separately without
    // reinitializing the teensy and causing a bunch of issues.
    setup_images();
}


// ----------------------------------------------------------

int main(void)
{
    srand(rand_seed());
    setup();

    for ( ;; )
    {
        if(!BIT_IS_SET(gamestate,QUIT))
        {
            process();
        }
        else
            quit_screen();

    }
}

char buffer2[5];
void draw_int(uint8_t x, uint8_t y, int value, colour_t colour)
{
    snprintf(buffer2, sizeof(buffer2), "%d", value);
    draw_string(x, y, buffer2, colour);
}
void draw_double(uint8_t x, uint8_t y, double value, colour_t colour)
{
    snprintf(buffer2, sizeof(buffer2), "%f", value);
    draw_string(x, y, buffer2, colour);
}
