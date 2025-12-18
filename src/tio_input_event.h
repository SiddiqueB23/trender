enum tio_input_event_type {
    TIO_INPUT_EVENT_TYPE_UNKNOWN = 0,
    TIO_INPUT_EVENT_TYPE_KEY = 1,
    TIO_INPUT_EVENT_TYPE_MOUSE = 2,
};

enum tio_input_event_codes {
    KEY_NULL = 0,

    CTRL_A = 1,
    CTRL_B = 2,
    CTRL_C = 3,
    CTRL_D = 4,
    CTRL_F = 6,
    CTRL_G = 7,
    CTRL_H = 8,
    CTRL_I = 9,
    CTRL_J = 10,
    CTRL_K = 11,
    CTRL_L = 12,
    CTRL_M = 13,
    CTRL_N = 14,
    CTRL_O = 15,
    CTRL_P = 16,
    CTRL_Q = 17,
    CTRL_R = 18,
    CTRL_S = 19,
    CTRL_T = 20,
    CTRL_U = 21,
    CTRL_V = 22,
    CTRL_W = 23,
    CTRL_X = 24,
    CTRL_Y = 25,
    CTRL_Z = 26,
    CTRL_BACKSPACE = 8,
    TAB = 9,
    CTRL_ENTER = 10,
    ENTER = 13,
    ESC = 27,
    CTRL_CLOSING_SQUARE_BRACKET = 29,
    CTRL_FORWARD_SLASH = 31,
    SPACE = 32,

    EXCLAMATION_MARK = 33,
    DOUBLE_QUOTES = 34,
    NUMBER_SIGN = 35,
    DOLLAR = 36,
    PER_CENT_SIGN = 37,
    AMPERSAND = 38,
    SINGLE_QUOTE = 39,
    OPEN_PARENTHESIS = 40,
    CLOSE_PARENTHESIS = 41,
    ASTERISK = 42,
    PLUS = 43,
    COMMA = 44,
    MINUS = 45,
    PERIOD = 46,
    FORWARD_SLASH = 47,
    ZERO = 48,
    ONE = 49,
    TWO = 50,
    THREE = 51,
    FOUR = 52,
    FIVE = 53,
    SIX = 54,
    SEVEN = 55,
    EIGHT = 56,
    NINE = 57,
    COLON = 58,
    SEMICOLON = 59,
    LESS_THAN = 60,
    EQUALS = 61,
    GREATER_THAN = 62,
    QUESTION_MARK = 63,
    AT_SIGN = 64,

    UPPERCASE_A = 65,
    UPPERCASE_B = 66,
    UPPERCASE_C = 67,
    UPPERCASE_D = 68,
    UPPERCASE_E = 69,
    UPPERCASE_F = 70,
    UPPERCASE_G = 71,
    UPPERCASE_H = 72,
    UPPERCASE_I = 73,
    UPPERCASE_J = 74,
    UPPERCASE_K = 75,
    UPPERCASE_L = 76,
    UPPERCASE_M = 77,
    UPPERCASE_N = 78,
    UPPERCASE_O = 79,
    UPPERCASE_P = 80,
    UPPERCASE_Q = 81,
    UPPERCASE_R = 82,
    UPPERCASE_S = 83,
    UPPERCASE_T = 84,
    UPPERCASE_U = 85,
    UPPERCASE_V = 86,
    UPPERCASE_W = 87,
    UPPERCASE_X = 88,
    UPPERCASE_Y = 89,
    UPPERCASE_Z = 90,
    OPENING_SQUARE_BRACKET = 91,
    BACKSLASH = 92,
    CLOSING_SQUARE_BRACKET = 93,
    CARET = 94,
    UNDERSCORE = 95,
    GRAVE_ACCENT = 96,

    LOWERCASE_A = 97,
    LOWERCASE_B = 98,
    LOWERCASE_C = 99,
    LOWERCASE_D = 100,
    LOWERCASE_E = 101,
    LOWERCASE_F = 102,
    LOWERCASE_G = 103,
    LOWERCASE_H = 104,
    LOWERCASE_I = 105,
    LOWERCASE_J = 106,
    LOWERCASE_K = 107,
    LOWERCASE_L = 108,
    LOWERCASE_M = 109,
    LOWERCASE_N = 110,
    LOWERCASE_O = 111,
    LOWERCASE_P = 112,
    LOWERCASE_Q = 113,
    LOWERCASE_R = 114,
    LOWERCASE_S = 115,
    LOWERCASE_T = 116,
    LOWERCASE_U = 117,
    LOWERCASE_V = 118,
    LOWERCASE_W = 119,
    LOWERCASE_X = 120,
    LOWERCASE_Y = 121,
    LOWERCASE_Z = 122,
    OPENING_CURLY_BRACE = 123,
    VERTICAL_BAR = 124,
    CLOSING_CURLY_BRACE = 125,
    TILDE = 126,
    BACKSPACE = 127,

    ARROW_UP = 128,
    ARROW_DOWN,
    ARROW_RIGHT,
    ARROW_LEFT,
    HOME_KEY,
    END_KEY,
    CTRL_ARROW_UP,
    CTRL_ARROW_DOWN,
    CTRL_ARROW_RIGHT,
    CTRL_ARROW_LEFT,

    INSERT_KEY,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN,

    FUNCTION_1,
    FUNCTION_2,
    FUNCTION_3,
    FUNCTION_4,
    FUNCTION_5,
    FUNCTION_6,
    FUNCTION_7,
    FUNCTION_8,
    FUNCTION_9,
    FUNCTION_10,
    FUNCTION_11,
    FUNCTION_12,

    LMB_DOWN,
    LMB_UP,
    LMB_PRESSED_MOVE,
    MMB_DOWN,
    MMB_UP,
    MMB_PRESSED_MOVE,
    RMB_DOWN,
    RMB_UP,
    RMB_PRESSED_MOVE,
    SCROLL_UP,
    SCROLL_DOWN,
    MOUSE_MOVE,

    CTRL_LMB_DOWN,
    CTRL_LMB_UP,
    CTRL_LMB_PRESSED_MOVE,
    CTRL_MMB_DOWN,
    CTRL_MMB_UP,
    CTRL_MMB_PRESSED_MOVE,
    CTRL_RMB_DOWN,
    CTRL_RMB_UP,
    CTRL_RMB_PRESSED_MOVE,
    CTRL_SCROLL_UP,
    CTRL_SCROLL_DOWN,
    CTRL_MOUSE_MOVE,

    CTRL_FUNCTION_1,
    CTRL_FUNCTION_2,
    CTRL_FUNCTION_3,
    CTRL_FUNCTION_4,
    CTRL_FUNCTION_5,
    CTRL_FUNCTION_6,
    CTRL_FUNCTION_7,
    CTRL_FUNCTION_8,
    CTRL_FUNCTION_9,
    CTRL_FUNCTION_10,
    CTRL_FUNCTION_11,
    CTRL_FUNCTION_12,

    CTRL_HOME_KEY,
    CTRL_END_KEY,
    CTRL_INSERT_KEY,
    CTRL_DEL_KEY,
    CTRL_PAGE_UP,
    CTRL_PAGE_DOWN,

    BEGIN,
    CTRL_BEGIN,
    SHIFT_BEGIN,
    CTRL_SHIFT_BEGIN,

    SHIFT_FUNCTION_1,
    SHIFT_FUNCTION_2,
    SHIFT_FUNCTION_3,
    SHIFT_FUNCTION_4,
    SHIFT_FUNCTION_5,
    SHIFT_FUNCTION_6,
    SHIFT_FUNCTION_7,
    SHIFT_FUNCTION_8,
    SHIFT_FUNCTION_9,
    SHIFT_FUNCTION_10,
    SHIFT_FUNCTION_11,
    SHIFT_FUNCTION_12,

    CTRL_SHIFT_FUNCTION_1,
    CTRL_SHIFT_FUNCTION_2,
    CTRL_SHIFT_FUNCTION_3,
    CTRL_SHIFT_FUNCTION_4,
    CTRL_SHIFT_FUNCTION_5,
    CTRL_SHIFT_FUNCTION_6,
    CTRL_SHIFT_FUNCTION_7,
    CTRL_SHIFT_FUNCTION_8,
    CTRL_SHIFT_FUNCTION_9,
    CTRL_SHIFT_FUNCTION_10,
    CTRL_SHIFT_FUNCTION_11,
    CTRL_SHIFT_FUNCTION_12,

    SHIFT_HOME_KEY,
    SHIFT_END_KEY,
    SHIFT_DEL_KEY,
    SHIFT_PAGE_UP,
    SHIFT_PAGE_DOWN,

    CTRL_SHIFT_HOME_KEY,
    CTRL_SHIFT_END_KEY,
    CTRL_SHIFT_DEL_KEY,
    CTRL_SHIFT_PAGE_UP,
    CTRL_SHIFT_PAGE_DOWN,
};

typedef struct {
    int type;
    int code;
    int position_x, position_y;
} tio_input_event;

#define TIO_INPUT_EVENT_INITIALIZER {TIO_INPUT_EVENT_TYPE_UNKNOWN, KEY_NULL, 0, 0}

#define TIO_INPUT_EVENT_CODE_COUNT 234

/* Array of string literals mapped by numeric code (index == enum value). */
static const char *tio_input_event_code_names[TIO_INPUT_EVENT_CODE_COUNT] = {
    "KEY_NULL",                    /* 0 */
    "CTRL_A",                      /* 1 */
    "CTRL_B",                      /* 2 */
    "CTRL_C",                      /* 3 */
    "CTRL_D",                      /* 4 */
    "CTRL_E",                      /* 5 */
    "CTRL_F",                      /* 6 */
    "CTRL_G",                      /* 7 */
    "CTRL_H/CTRL_BACKSPACE",       /* 8 */
    "CTRL_I/TAB",                  /* 9 */
    "CTRL_J/CTRL_ENTER",           /* 10 */
    "CTRL_K",                      /* 11 */
    "CTRL_L",                      /* 12 */
    "CTRL_M/ENTER",                /* 13 */
    "CTRL_N",                      /* 14 */
    "CTRL_O",                      /* 15 */
    "CTRL_P",                      /* 16 */
    "CTRL_Q",                      /* 17 */
    "CTRL_R",                      /* 18 */
    "CTRL_S",                      /* 19 */
    "CTRL_T",                      /* 20 */
    "CTRL_U",                      /* 21 */
    "CTRL_V",                      /* 22 */
    "CTRL_W",                      /* 23 */
    "CTRL_X",                      /* 24 */
    "CTRL_Y",                      /* 25 */
    "CTRL_Z",                      /* 26 */
    "ESC",                         /* 27 */
    "UNASSIGNED",                  /* 28 */
    "CTRL_CLOSING_SQUARE_BRACKET", /* 29 */
    "UNASSIGNED",                  /* 30 */
    "CTRL_FORWARD_SLASH",          /* 31 */

    "SPACE",             /* 32 */
    "EXCLAMATION_MARK",  /* 33 */
    "DOUBLE_QUOTES",     /* 34 */
    "NUMBER_SIGN",       /* 35 */
    "DOLLAR",            /* 36 */
    "PER_CENT_SIGN",     /* 37 */
    "AMPERSAND",         /* 38 */
    "SINGLE_QUOTE",      /* 39 */
    "OPEN_PARENTHESIS",  /* 40 */
    "CLOSE_PARENTHESIS", /* 41 */
    "ASTERISK",          /* 42 */
    "PLUS",              /* 43 */
    "COMMA",             /* 44 */
    "MINUS",             /* 45 */
    "PERIOD",            /* 46 */
    "FORWARD_SLASH",     /* 47 */
    "ZERO",              /* 48 */
    "ONE",               /* 49 */
    "TWO",               /* 50 */
    "THREE",             /* 51 */
    "FOUR",              /* 52 */
    "FIVE",              /* 53 */
    "SIX",               /* 54 */
    "SEVEN",             /* 55 */
    "EIGHT",             /* 56 */
    "NINE",              /* 57 */
    "COLON",             /* 58 */
    "SEMICOLON",         /* 59 */
    "LESS_THAN",         /* 60 */
    "EQUALS",            /* 61 */
    "GREATER_THAN",      /* 62 */
    "QUESTION_MARK",     /* 63 */

    "AT_SIGN",                /* 64 */
    "UPPERCASE_A",            /* 65 */
    "UPPERCASE_B",            /* 66 */
    "UPPERCASE_C",            /* 67 */
    "UPPERCASE_D",            /* 68 */
    "UPPERCASE_E",            /* 69 */
    "UPPERCASE_F",            /* 70 */
    "UPPERCASE_G",            /* 71 */
    "UPPERCASE_H",            /* 72 */
    "UPPERCASE_I",            /* 73 */
    "UPPERCASE_J",            /* 74 */
    "UPPERCASE_K",            /* 75 */
    "UPPERCASE_L",            /* 76 */
    "UPPERCASE_M",            /* 77 */
    "UPPERCASE_N",            /* 78 */
    "UPPERCASE_O",            /* 79 */
    "UPPERCASE_P",            /* 80 */
    "UPPERCASE_Q",            /* 81 */
    "UPPERCASE_R",            /* 82 */
    "UPPERCASE_S",            /* 83 */
    "UPPERCASE_T",            /* 84 */
    "UPPERCASE_U",            /* 85 */
    "UPPERCASE_V",            /* 86 */
    "UPPERCASE_W",            /* 87 */
    "UPPERCASE_X",            /* 88 */
    "UPPERCASE_Y",            /* 89 */
    "UPPERCASE_Z",            /* 90 */
    "OPENING_SQUARE_BRACKET", /* 91 */
    "BACKSLASH",              /* 92 */
    "CLOSING_SQUARE_BRACKET", /* 93 */
    "CARET",                  /* 94 */
    "UNDERSCORE",             /* 95 */

    "GRAVE_ACCENT",        /* 96 */
    "LOWERCASE_A",         /* 97 */
    "LOWERCASE_B",         /* 98 */
    "LOWERCASE_C",         /* 99 */
    "LOWERCASE_D",         /* 100 */
    "LOWERCASE_E",         /* 101 */
    "LOWERCASE_F",         /* 102 */
    "LOWERCASE_G",         /* 103 */
    "LOWERCASE_H",         /* 104 */
    "LOWERCASE_I",         /* 105 */
    "LOWERCASE_J",         /* 106 */
    "LOWERCASE_K",         /* 107 */
    "LOWERCASE_L",         /* 108 */
    "LOWERCASE_M",         /* 109 */
    "LOWERCASE_N",         /* 110 */
    "LOWERCASE_O",         /* 111 */
    "LOWERCASE_P",         /* 112 */
    "LOWERCASE_Q",         /* 113 */
    "LOWERCASE_R",         /* 114 */
    "LOWERCASE_S",         /* 115 */
    "LOWERCASE_T",         /* 116 */
    "LOWERCASE_U",         /* 117 */
    "LOWERCASE_V",         /* 118 */
    "LOWERCASE_W",         /* 119 */
    "LOWERCASE_X",         /* 120 */
    "LOWERCASE_Y",         /* 121 */
    "LOWERCASE_Z",         /* 122 */
    "OPENING_CURLY_BRACE", /* 123 */
    "VERTICAL_BAR",        /* 124 */
    "CLOSING_CURLY_BRACE", /* 125 */
    "TILDE",               /* 126 */
    "BACKSPACE",           /* 127 */

    "ARROW_UP",         /* 128 */
    "ARROW_DOWN",       /* 129 */
    "ARROW_RIGHT",      /* 130 */
    "ARROW_LEFT",       /* 131 */
    "HOME_KEY",         /* 132 */
    "END_KEY",          /* 133 */
    "CTRL_ARROW_UP",    /* 134 */
    "CTRL_ARROW_DOWN",  /* 135 */
    "CTRL_ARROW_RIGHT", /* 136 */
    "CTRL_ARROW_LEFT",  /* 137 */
    "INSERT_KEY",       /* 138 */
    "DEL_KEY",          /* 139 */
    "PAGE_UP",          /* 140 */
    "PAGE_DOWN",        /* 141 */

    "FUNCTION_1",  /* 142 */
    "FUNCTION_2",  /* 143 */
    "FUNCTION_3",  /* 144 */
    "FUNCTION_4",  /* 145 */
    "FUNCTION_5",  /* 146 */
    "FUNCTION_6",  /* 147 */
    "FUNCTION_7",  /* 148 */
    "FUNCTION_8",  /* 149 */
    "FUNCTION_9",  /* 150 */
    "FUNCTION_10", /* 151 */
    "FUNCTION_11", /* 152 */
    "FUNCTION_12", /* 153 */

    "LMB_DOWN",         /* 154 */
    "LMB_UP",           /* 155 */
    "LMB_PRESSED_MOVE", /* 156 */
    "MMB_DOWN",         /* 157 */
    "MMB_UP",           /* 158 */
    "MMB_PRESSED_MOVE", /* 159 */
    "RMB_DOWN",         /* 160 */
    "RMB_UP",           /* 161 */
    "RMB_PRESSED_MOVE", /* 162 */
    "SCROLL_UP",        /* 163 */
    "SCROLL_DOWN",      /* 164 */
    "MOUSE_MOVE",       /* 165 */

    "CTRL_LMB_DOWN",         /*166*/
    "CTRL_LMB_UP",           /*167*/
    "CTRL_LMB_PRESSED_MOVE", /*168*/
    "CTRL_MMB_DOWN",         /*169*/
    "CTRL_MMB_UP",           /*170*/
    "CTRL_MMB_PRESSED_MOVE", /*171*/
    "CTRL_RMB_DOWN",         /*172*/
    "CTRL_RMB_UP",           /*173*/
    "CTRL_RMB_PRESSED_MOVE", /*174*/
    "CTRL_SCROLL_UP",        /*175*/
    "CTRL_SCROLL_DOWN",      /*176*/
    "CTRL_MOUSE_MOVE",       /*177*/

    "CTRL_FUNCTION_1",  /* 178 */
    "CTRL_FUNCTION_2",  /* 179 */
    "CTRL_FUNCTION_3",  /* 180 */
    "CTRL_FUNCTION_4",  /* 181 */
    "CTRL_FUNCTION_5",  /* 182 */
    "CTRL_FUNCTION_6",  /* 183 */
    "CTRL_FUNCTION_7",  /* 184 */
    "CTRL_FUNCTION_8",  /* 185 */
    "CTRL_FUNCTION_9",  /* 186 */
    "CTRL_FUNCTION_10", /* 187 */
    "CTRL_FUNCTION_11", /* 188 */
    "CTRL_FUNCTION_12", /* 189 */

    "CTRL_HOME_KEY",   /* 190 */
    "CTRL_END_KEY",    /* 191 */
    "CTRL_INSERT_KEY", /* 192 */
    "CTRL_DEL_KEY",    /* 193 */
    "CTRL_PAGE_UP",    /* 194 */
    "CTRL_PAGE_DOWN",  /* 195 */

    "BEGIN",            /* 196 */
    "CTRL_BEGIN",       /* 197 */
    "SHIFT_BEGIN",      /* 198 */
    "CTRL_SHIFT_BEGIN", /* 199 */

    "SHIFT_FUNCTION_1",  /* 200 */
    "SHIFT_FUNCTION_2",  /* 201 */
    "SHIFT_FUNCTION_3",  /* 202 */
    "SHIFT_FUNCTION_4",  /* 203 */
    "SHIFT_FUNCTION_5",  /* 204 */
    "SHIFT_FUNCTION_6",  /* 205 */
    "SHIFT_FUNCTION_7",  /* 206 */
    "SHIFT_FUNCTION_8",  /* 207 */
    "SHIFT_FUNCTION_9",  /* 208 */
    "SHIFT_FUNCTION_10", /* 209 */
    "SHIFT_FUNCTION_11", /* 210 */
    "SHIFT_FUNCTION_12", /* 211 */

    "CTRL_SHIFT_FUNCTION_1",  /* 212 */
    "CTRL_SHIFT_FUNCTION_2",  /* 213 */
    "CTRL_SHIFT_FUNCTION_3",  /* 214 */
    "CTRL_SHIFT_FUNCTION_4",  /* 215 */
    "CTRL_SHIFT_FUNCTION_5",  /* 216 */
    "CTRL_SHIFT_FUNCTION_6",  /* 217 */
    "CTRL_SHIFT_FUNCTION_7",  /* 218 */
    "CTRL_SHIFT_FUNCTION_8",  /* 219 */
    "CTRL_SHIFT_FUNCTION_9",  /* 220 */
    "CTRL_SHIFT_FUNCTION_10", /* 221 */
    "CTRL_SHIFT_FUNCTION_11", /* 222 */
    "CTRL_SHIFT_FUNCTION_12", /* 223 */

    "SHIFT_HOME_KEY",  /* 224 */
    "SHIFT_END_KEY",   /* 225 */
    "SHIFT_DEL_KEY",   /* 226 */
    "SHIFT_PAGE_UP",   /* 227 */
    "SHIFT_PAGE_DOWN", /* 228 */

    "CTRL_SHIFT_HOME_KEY",  /* 229 */
    "CTRL_SHIFT_END_KEY",   /* 230 */
    "CTRL_SHIFT_DEL_KEY",   /* 231 */
    "CTRL_SHIFT_PAGE_UP",   /* 232 */
    "CTRL_SHIFT_PAGE_DOWN", /* 233 */
};

static inline const char *tio_input_event_code_to_string(int code) {
    if (code < 0 || code >= (int)TIO_INPUT_EVENT_CODE_COUNT) return "UNKNOWN";
    return tio_input_event_code_names[code];
}