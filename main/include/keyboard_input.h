/*
 *  Input key event codes modified from the linux kernel. They are 7-bit allowing 
 *  up to 128-keys. The 8th bit represents key pressed (1) or released (0).
 *  The 7th bit is used for modifier-keys, that are digested before hand.
 *
 *  KEY_EVENT   MSB [  PRESS |  MODKEY  | 5 .. 0  ] LSB
 */

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
 *  The CAPSLOCK (bit 5) is not produced by a modifier-key event (or maybe I should include it here), but
 *  the bit in the KBD_MODS is toggled with the key pressed.
 *  
 *  There are two extra special keys: LANG and SYS. Neither is a modifier.
 *  LANG is a deadkey: Pressing LANG + {e, u, i, n} changes the keyboard layout to Spanish, 
 *       Uk, Italian, Norwegian, etc
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

#define KEY(r, c) ((r << 3) + c)

static uint8_t KBD_MODS = 0;

/* The KBD map results as follows, once we resort the non-mod keys to be contiguous */
static const uint8_t KBDMAP[] =  {  1 ,  3 ,  5 ,  7 ,  8 , 10 , 12 , 14 , 
                                    2 ,  4 ,  6 , 20 ,  9 , 11 , 13 ,  0 , 
                                   15 , 17 , 19 , 21 , 22 , 24 , 25 , 27 , 
                                   16 , 18 , 33 , 35 , 23 , 38 , 26 , 28 , 
                                   29 , 31 , 32 , 34 , 36 , 37 , 39 , 41 , 
                                   30 , 42 , 44 , 46 , 48 , 50 , 40 , 97 , 
                                   65 , 43 , 45 , 47 , 49 , 51 , 52 ,  0 , 
                                   66 , 68 , 72 , 53 ,104 , 54 , 55 , 98 };
 

#define VKCHAROFFSET 28   /* Offset between the key definitions and the character font/encoding arrays */

typedef enum {
  VK_NONE,            /**< No character (marks the first virtual key) */
  VK_ESC,             /**< ESC */
  VK_INSERT,          /**< INS */
  VK_DELETE,          /**< DEL */
  VK_BACKSPACE,       /**< Backspace */
  VK_HOME,            /**< HOME */
  VK_END,             /**< END */
  VK_CAPSLOCK,        /**< CAPSLOCK */
  VK_TAB,             /**< TAB */
  VK_ENTER,           /**< RETURN */
  VK_PAGEUP,          /**< PAGEUP */
  VK_PAGEDOWN,        /**< PAGEDOWN */
  VK_UP,              /**< Cursor UP */
  VK_DOWN,            /**< Cursor DOWN */
  VK_LEFT,            /**< Cursor LEFT */
  VK_RIGHT,           /**< Cursor RIGHT */
  VK_F1,              /** Function F1 */
  VK_F2,              /** Function F2 */
  VK_F3,              /** Function F3 */
  VK_F4,              /** Function F4 */
  VK_F5,              /** Function F5 */
  VK_F6,              /** Function F6 */
  VK_F7,              /** Function F7 */
  VK_F8,              /** Function F8 */
  VK_F9,              /** Function F9 */
  VK_F10,             /** Function F10 */
  VK_SYS,             /* KEYBOAD special to access system */
  VK_LANG,            /* KEYBOARD special to change LANG */


VK_SPACE,           /**< Space */
VK_EXCLAIM,         /**< Exclamation mark: ! */
VK_QUOTEDBL,        /**< Double quote: " */
VK_HASH,            /**< Hash: # */
VK_DOLLAR,          /**< Dollar: $ */
VK_PERCENT,         /**< Percent: % */
VK_AMPERSAND,       /**< Ampersand: & */
VK_APOSTROPHE,     /**< Acute accent: ´ */
VK_LEFTPAREN,       /**< Left parenthesis: ( */
VK_RIGHTPAREN,      /**< Right parenthesis: ) */
VK_ASTERISK,        /**< Asterisk: * */
VK_PLUS,            /**< Plus: + */
VK_COMMA,           /**< Comma: , */
VK_MINUS,           /**< Minus: - */
VK_DOT,             /**< Period: . */
VK_SLASH,           /**< Slash: / */
VK_0,               /**< Number 0 */
VK_1,               /**< Number 1 */
VK_2,               /**< Number 2 */
VK_3,               /**< Number 3 */
VK_4,               /**< Number 4 */
VK_5,               /**< Number 5 */
VK_6,               /**< Number 6 */
VK_7,               /**< Number 7 */
VK_8,               /**< Number 8 */
VK_9,               /**< Number 9 */
VK_COLON,           /**< Colon: : */
VK_SEMICOLON,       /**< Semicolon: ; */
VK_LESS,            /**< Less: < */
VK_EQUAL,           /**< Equals: = */
VK_GREATER,         /**< Greater: > */
VK_QUESTION,        /**< Question mark: ? */
VK_AT,              /**< At: @ */
VK_A,               /**< Upper case letter 'A' */
VK_B,               /**< Upper case letter 'B' */
VK_C,               /**< Upper case letter 'C' */
VK_D,               /**< Upper case letter 'D' */
VK_E,               /**< Upper case letter 'E' */
VK_F,               /**< Upper case letter 'F' */
VK_G,               /**< Upper case letter 'G' */
VK_H,               /**< Upper case letter 'H' */
VK_I,               /**< Upper case letter 'I' */
VK_J,               /**< Upper case letter 'J' */
VK_K,               /**< Upper case letter 'K' */
VK_L,               /**< Upper case letter 'L' */
VK_M,               /**< Upper case letter 'M' */
VK_N,               /**< Upper case letter 'N' */
VK_O,               /**< Upper case letter 'O' */
VK_P,               /**< Upper case letter 'P' */
VK_Q,               /**< Upper case letter 'Q' */
VK_R,               /**< Upper case letter 'R' */
VK_S,               /**< Upper case letter 'S' */
VK_T,               /**< Upper case letter 'T' */
VK_U,               /**< Upper case letter 'U' */
VK_V,               /**< Upper case letter 'V' */
VK_W,               /**< Upper case letter 'W' */
VK_X,               /**< Upper case letter 'X' */
VK_Y,               /**< Upper case letter 'Y' */
VK_Z,               /**< Upper case letter 'Z' */
VK_LEFTBRACKET,     /**< Left bracket: [ */
VK_BACKSLASH,       /**< Backslash: \ */
VK_RIGHTBRACKET,    /**< Right bracket: ] */
VK_CARET,           /**< Caret: ^ */
VK_UNDERSCORE,      /**< Underscore: _ */
VK_GRAVEACCENT,     /**< Grave accent: ` */
VK_a,               /**< Lower case letter 'a' */
VK_b,               /**< Lower case letter 'b' */
VK_c,               /**< Lower case letter 'c' */
VK_d,               /**< Lower case letter 'd' */
VK_e,               /**< Lower case letter 'e' */
VK_f,               /**< Lower case letter 'f' */
VK_g,               /**< Lower case letter 'g' */
VK_h,               /**< Lower case letter 'h' */
VK_i,               /**< Lower case letter 'i' */
VK_j,               /**< Lower case letter 'j' */
VK_k,               /**< Lower case letter 'k' */
VK_l,               /**< Lower case letter 'l' */
VK_m,               /**< Lower case letter 'm' */
VK_n,               /**< Lower case letter 'n' */
VK_o,               /**< Lower case letter 'o' */
VK_p,               /**< Lower case letter 'p' */
VK_q,               /**< Lower case letter 'q' */
VK_r,               /**< Lower case letter 'r' */
VK_s,               /**< Lower case letter 's' */
VK_t,               /**< Lower case letter 't' */
VK_u,               /**< Lower case letter 'u' */
VK_v,               /**< Lower case letter 'v' */
VK_w,               /**< Lower case letter 'w' */
VK_x,               /**< Lower case letter 'x' */
VK_y,               /**< Lower case letter 'y' */
VK_z,               /**< Lower case letter 'z' */
VK_LEFTBRACE,       /**< Left brace: { */
VK_VERTICALBAR,     /**< Vertical bar: | */
VK_RIGHTBRACE,      /**< Right brace: } */
VK_TILDE,           /**< Tilde: ~ */
VK_BULLET,           /* bullet */
VK_GRAVE_a,         /**< Grave a: à */
VK_GRAVE_e,         /**< Grave e: è */
VK_GRAVE_i,         /**< Grave i: ì */
VK_GRAVE_o,         /**< Grave o: ò */
VK_GRAVE_u,         /**< Grave u: ù */
VK_ACUTE_a,         /**< Acute a: á */
VK_ACUTE_e,         /**< Acute e: é */
VK_ACUTE_i,         /**< Acute i: í */
VK_ACUTE_o,         /**< Acute o: ó */
VK_ACUTE_u,         /**< Acute u: ú */
VK_ACUTE_y,         /**< Acute y: ý */
VK_GRAVE_A,		      /**< Grave A: À */
VK_GRAVE_E,		      /**< Grave E: È */
VK_GRAVE_I,		      /**< Grave I: Ì */
VK_GRAVE_O,		      /**< Grave O: Ò */
VK_GRAVE_U,		      /**< Grave U: Ù */
VK_ACUTE_A,		      /**< Acute A: Á */
VK_ACUTE_E,		      /**< Acute E: É */
VK_ACUTE_I,		      /**< Acute I: Í */
VK_ACUTE_O,		      /**< Acute O: Ó */
VK_ACUTE_U,		      /**< Acute U: Ú */
VK_ACUTE_Y,         /**< Acute Y: Ý */
VK_THORN,            /** Thorn */
VK_SMALL_THORN,            /** Thorn */
VK_UMLAUT_a,        /**< Diaeresis a: ä */
VK_UMLAUT_e,        /**< Diaeresis e: ë */
VK_UMLAUT_i,        /**< Diaeresis i: ï */
VK_UMLAUT_o,        /**< Diaeresis o: ö */
VK_UMLAUT_u,        /**< Diaeresis u: ü */
VK_UMLAUT_y,        /**< Diaeresis y: ÿ */
VK_UMLAUT_A,        /**< Diaeresis A: Ä */
VK_UMLAUT_E,        /**< Diaeresis E: Ë */
VK_UMLAUT_I,        /**< Diaeresis I: Ï */
VK_UMLAUT_O,        /**< Diaeresis O: Ö */
VK_UMLAUT_U,        /**< Diaeresis U: Ü */
VK_UMLAUT_Y,        /**< Diaeresis Y: Ÿ */
VK_ETH,            /**< D with stroke */
VK_MULTIPLICATION,            /**Multiplication */
VK_CARET_a,		      /**< Caret a: â */
VK_CARET_e,		      /**< Caret e: ê */
VK_CARET_i,		      /**< Caret i: î */
VK_CARET_o,		      /**< Caret o: ô */
VK_CARET_u,		      /**< Caret u: û */
VK_CARET_A,		      /**< Caret A: Â */
VK_CARET_E,		      /**< Caret E: Ê */
VK_CARET_I,		      /**< Caret I: Î */
VK_CARET_O,		      /**< Caret O: Ô */
VK_CARET_U,		      /**< Caret U: Û */
VK_CEDILLA_c,       /**< Cedilla c: ç */
VK_CEDILLA_C,       /**< Cedilla C: Ç */
VK_TILDE_a,         /**< Lower case tilde a: ã */
VK_TILDE_o,         /**< Lower case tilde o: õ */
VK_TILDE_n,		      /**< Lower case tilde n: ñ */
VK_TILDE_A,         /**< Upper case tilde A: Ã */
VK_TILDE_O,         /**< Upper case tilde O: Õ */
VK_TILDE_N,		      /**< Upper case tilde N: Ñ */
VK_ESZETT,          /**< Eszett: ß */
VK_MEDIUM_SHADE,          /**< Eszett: ß */
VK_EXCLAIM_INV,     /**< Inverted exclamation mark: ! */
VK_CENT,        /**< Inverted exclamation mark: ! */
VK_POUND,           /**< Pound: £ */
VK_EURO,            /**< Euro: € */
VK_YEN,            /**< Euro: € */
VK_CARON_S,            /**< Euro: € */
VK_SECTION,         /**< Section: § */
VK_CARON_s,            /**< Euro: € */
VK_COPYRIGHT,            /**< Euro: € */
VK_a_ORDINAL,            /**< Euro: € */
VK_DBLLEFTANG,            /**< Euro: € */
VK_NEGATION,        /**< Negation: ¬ */
VK_CURRENCY,        /**< Currency   : ¤ */
VK_REGISTERED,        /**< Currency   : ¤ */
VK_MACRON,        /**< Currency   : ¤ */
VK_DEGREE,          /**< Degree: ° */
VK_PLUSMINUS,          /**< Degree: ° */
VK_2SUPERSCRIPT,          /**< Square     : ² */
VK_3SUPERSCRIPT,          /**< Square     : ² */
VK_CARON_Z,          /**< Square     : ² */
VK_MU,              /**< Mu         : µ */
VK_PILCROW,              /**< Mu         : µ */
VK_MIDDLE_DOT,              /**< Mu         : µ */
VK_CARON_z,          /**< Square     : ² */
VK_1SUPERSCRIPT,          /**< Square     : ² */
VK_o_ORDINAL,            /**< Euro: € */
VK_DBLRIGHTANG,            /**< Euro: € */
VK_OELIG,           /** Upper case Oelig  :  */
VK_oelig,           /** Upper case Oelig  :  */
VK_QUESTION_INV,    /**< Inverted question mark : ? */
VK_aelig,           /** Lower case aelig  : æ */
VK_oslash,          /** Lower case oslash : ø */
VK_aring,           /** Lower case aring  : å */
VK_AELIG,           /** Upper case aelig  : Æ */
VK_OSLASH,          /** Upper case oslash : Ø */
VK_ARING,            /** Upper case aring  : Å */

VK_SMALL_PI,
VK_NOT_EQUAL,
VK_LESS_EQUAL,
VK_GREATER_EQUAL,
VK_SQUARE,
VK_DIAMOND,
VK_1OVER4,
VK_1OVER2,
VK_3OVER4,
VK_BROKENBAR,
VK_DIAERESIS,
VK_CEDILLA,
VK_f_HOOK,
VK_DAGGER,
VK_DBL_DAGGER,
VK_PERMILLE,
VK_TRADEMARK,
VK_ELLIPSIS,
VK_LEFTANG,
VK_RIGHTANG,
VK_QUOTEDBL_LEFT,    /* Double quote left */
VK_QUOTEDBL_RIGHT,   /* Double quote right */
VK_QUOTEDBL_LOW,     /* Double quote low*/
VK_QUOTEDBL_LOW_REV, /* Reversed double quote low*/
VK_COMMA_REV,        /* Reversed comma*/
VK_CARET_G,	      /**< Caret a: â */
VK_CARET_g,         /**< Caret e: ê */
VK_I_DOT,           /* Capital I with dot */ 
VK_i_DOTLESS,       /* Small i without dot */ 
VK_CEDILLA_S,       /**< Cedilla s:  */
VK_CEDILLA_s,       /**< Cedilla S:  */
VK_REPLACEMENT,
} Virtual_Key;


/* Unicode and font maps from int(VK) - VKCHAROFFSET */

static const int unicodemap[] = {
0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 
0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f, 
0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 
0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 
0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 
0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f, 
0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 
0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f, 
0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 
0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f, 
0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 
0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x2022, 
0x00E0, 0x00E8, 0x00EC, 0x00F2, 0x00F9, 0x00E1, 0x00E9, 0x00ED, 
0x00F3, 0x00FA, 0x00FD, 0x00C0, 0x00C8, 0x00CC, 0x00D2, 0x00D9, 
0x00C1, 0x00C9, 0x00CD, 0x00D3, 0x00DA, 0x00DD, 0x00DE, 0x00FE, 
0x00E4, 0x00EB, 0x00EF, 0x00F6, 0x00FC, 0x00FF, 0x00C4, 0x00CB, 
0x00CF, 0x00D6, 0x00DC, 0x0178, 0x00D0, 0x00D7, 0x00E2, 0x00EA, 
0x00EE, 0x00F4, 0x00FB, 0x00C2, 0x00CA, 0x00CE, 0x00D4, 0x00DB, 
0x00E7, 0x00C7, 0x00E3, 0x00F5, 0x00F1, 0x00C3, 0x00D5, 0x00D1, 
0x00DF, 0x2592, 0x00A1, 0x00A2, 0x00A3, 0x00AC, 0x00A5, 0x0160, 
0x00A7, 0x0161, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00A4, 0x00AE, 
0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x017D, 0x00B5, 0x00B6, 
0x00B7, 0x017E, 0x00B9, 0x00BA, 0x00BB, 0x0152, 0x0153, 0x00BF, 
0x00E6, 0x00F8, 0x00E5, 0x00C6, 0x00D8, 0x00C5, 0x03c0, 0x2260, 
0x2264, 0x2265, 0x25a0, 0x25c6, 0x00bc, 0x00bd, 0x00be, 0x00a6, 
0x00a8, 0x00b8, 0x0192, 0x2020, 0x2021, 0x2030, 0x2122, 0x2026, 
0x2039, 0x203a, 0x201c, 0x201d, 0x201e, 0x2e42, 0x2e41, 0x011e, 
0x011f, 0x0130, 0x0131, 0x015e, 0x015f, 0xfffd };

static const uint8_t fontmap[] = {
 32,    33,    34,    35,    36,    37,    38,    39,    
 40,    41,    42,    43,    44,    45,    46,    47,    
 48,    49,    50,    51,    52,    53,    54,    55,    
 56,    57,    58,    59,    60,    61,    62,    63,    
 64,    65,    66,    67,    68,    69,    70,    71,    
 72,    73,    74,    75,    76,    77,    78,    79,    
 80,    81,    82,    83,    84,    85,    86,    87,    
 88,    89,    90,    91,    92,    93,    94,    95,    
 96,    97,    98,    99,    100,    101,    102,    103,    
104,    105,    106,    107,    108,    109,    110,    111,    
112,    113,    114,    115,    116,    117,    118,    119,    
120,    121,    122,    123,    124,    125,    126,    127,    
224,    232,    236,    242,    249,    225,    233,    237,    
243,    250,    253,    128,    136,    140,    146,    153,    
129,    137,    141,    147,    154,    157,    158,    254,    
228,    235,    239,    246,    252,    255,    132,    139,    
143,    150,    156,    190,    144,    151,    226,    234,    
238,    244,    251,    130,   138,   142,   148,   155,   
231,    135,    227,    245,    241,    131,    149,    145,    
159,    160,    161,    162,    163,    164,    165,    166,    
167,    168,    169,    170,    171,    172,    173,    174,    
175,    176,    177,    178,    179,    180,    181,    182,    
183,    184,    185,    186,    187,    188,    189,    191,    
230,    248,    229,    134,    152,    133,    1,    2,    
  3,    4,    5,    6,    7,    8,    9,    10,    
 11,    12,    13,    14,    15,    16,    17,    18,    
 19,    20,    21,    22,    23,    24,    25,    26,    
 27,    28,    29,    30,    31,    0  }; 

/* US Layout in the 60% keyboard... replacing the ~ for ESC */
static int keymap[] =  { VK_NONE,  // 0
	VK_ESC, VK_1, VK_2, VK_3, VK_4, VK_5, VK_6, VK_7, VK_8, VK_9, VK_0, VK_MINUS, VK_EQUAL, VK_BACKSPACE,  // 14
	VK_TAB, VK_q, VK_w, VK_e, VK_r, VK_t, VK_y, VK_u, VK_i, VK_o, VK_p, VK_LEFTBRACKET, VK_RIGHTBRACKET, VK_ENTER,  // 28 
	VK_CAPSLOCK, VK_a, VK_s, VK_d, VK_f, VK_g, VK_h, VK_j, VK_k, VK_l, VK_SEMICOLON, VK_APOSTROPHE, VK_BACKSLASH,  // 41
	          VK_LESS, VK_z, VK_x, VK_c, VK_v, VK_b, VK_n, VK_m, VK_COMMA, VK_DOT, VK_SLASH,  // 52
	          VK_SPACE, VK_SYS, VK_LANG }; // 55

static int keymap_shift[] =  { VK_NONE,  // 0
	VK_TILDE, VK_EXCLAIM, VK_AT, VK_HASH, VK_DOLLAR, 
	VK_PERCENT, VK_CARET, VK_AMPERSAND, VK_ASTERISK, VK_LEFTPAREN, 
	VK_RIGHTPAREN, VK_UNDERSCORE, VK_PLUS, VK_BACKSPACE,  // 14
	VK_TAB, VK_Q, VK_W, VK_E, VK_R, VK_T, VK_Y, VK_U, VK_I, VK_O, VK_P, VK_LEFTBRACE, VK_RIGHTBRACE, VK_ENTER,  // 28 
	VK_CAPSLOCK, VK_A, VK_S, VK_D, VK_F, VK_G, VK_H, VK_J, VK_K, VK_L, VK_COLON, VK_QUOTEDBL, VK_VERTICALBAR,  // 41
	          VK_LESS, VK_Z, VK_X, VK_C, VK_V, VK_B, VK_N, VK_M, VK_LESS, VK_GREATER, VK_QUESTION,  // 52
	          VK_SPACE, VK_SYS, VK_LANG }; // 55
