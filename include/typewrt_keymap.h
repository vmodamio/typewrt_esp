#pragma once

#include <stdint.h>

#define KEYDOWN_MASK (1 << 7)
#define MOD_MASK (1 << 6)
#define KEY_MASK ((1 << 6) -1 )

/* *  Keyboard modifier events are send as: 
 *
 *  MODIFIER EVENTS = MSB [ EVENT |  1  |  RIGHT  | CAPS | LALT | LCMD | LCTRL | LSHIFT ] LSB
 *
 *  And the key code for those modifiers would be, including the extra (1 << 6):
 *  LSHIFT = 64 + 1      = 65
 *  LCTRL  = 64 + 2      = 66 
 *  LCMD   = 64 + 4      = 68
 *  RCMD   = 64 + 4 + 32 = 100
 *  LALT   = 64 + 8      = 72
 *  RALT   = 64 + 8 + 32 = 104
 *  RSHIFT = 64 + 1 + 32 = 97
 *  RCTRL  = 64 + 2 + 32 = 98 
 *
 *  The KEYS LEFT RIGHT are treated differently (like RALT is AltGr), but the CTRL and SHIFT
 *  keys are doubled in the keyboard. To avoid problems the modifier bit refers to the LEFT version,
 *  while the right version of the same key is mapped setting the 5th bit (right), in order to avoid 
 *  having the same key in multiple states.
 *
 *  The CAPSLOCK bit is reserved in the event format, but the embedded Nextvi
 *  path does not use a separate keyboard-side modifier latch.
 *  
 *  There are two extra special keys: LANG and SYS. Neither is a modifier.
 *  Keyboard layouts are switched in Nextvi with Alt + {e, s, i, n, g, f, t}
 *  for English, Spanish, Italian, Norwegian, German, French, and Turkish.
 *
 *  CAPSLOCK isnt a modifier too. Its a special key that latch.
 *
 *  SYS: Is a normal key. Pressed alone pops a bottom status bar with some info 
 *       (Battery, Date-time, memory, saved*, synch). With the CMD modifier enters in the system menu
 *       to access the filesystem, the bluetooth, etc...
 *
 *  The bottom KEYBOARD ROW AS FOLLOW
 *    [ LCTRL |  CMD  |  LALT  |          SPACE          |  RALT  |  SYS  |  LANG  | RCTRL  ]
 */  

/* The KBD map results as follows, once we resort the non-mod keys to be contiguous */
static const uint8_t KBDMAP[] =  {  1 ,  3 ,  5 ,  7 ,  8 , 10 , 12 , 14 , 
                                    2 ,  4 ,  6 , 20 ,  9 , 11 , 13 ,  0 , 
                                   15 , 17 , 19 , 21 , 22 , 24 , 25 , 27 , 
                                   16 , 18 , 33 , 35 , 23 , 38 , 26 , 28 , 
                                   29 , 31 , 32 , 34 , 36 , 37 , 39 , 41 , 
                                   30 , 42 , 44 , 46 , 48 , 50 , 40 , 97 , 
                                   65 , 43 , 45 , 47 , 49 , 51 , 52 ,  0 , 
                                   66 , 68 , 72 , 53 ,104 ,100 , 55 , 98 };
