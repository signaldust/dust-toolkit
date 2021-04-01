
#pragma once    // really only included from the system wrapper

// This based on SDL2-2.0.4/src/event/scancode_darwin.h with the
// prefixes removed, instead namespacing dust, adding header
// guards and other such minor edits.. but the actual codes should
// remain compatible.. because honestly the world doesn't need more
// ways to represent keys on the keyboard.
//
// Refer to the original to find out a complete list of changes, duh.
// Note that some of the comments are specific to SDL and might not apply.
//
// Original license follows:

/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2016 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "key_scancode.h"
namespace dust {

/* Mac virtual key code to SDL scancode mapping table
   Sources:
   - Inside Macintosh: Text <http://developer.apple.com/documentation/mac/Text/Text-571.html>
   - Apple USB keyboard driver source <http://darwinsource.opendarwin.org/10.4.6.ppc/IOHIDFamily-172.8/IOHIDFamily/Cosmo_USB2ADB.c>
   - experimentation on various ADB and USB ISO keyboards and one ADB ANSI keyboard
*/
static const Scancode scancode_table_osx[] = {
    /*   0 */   SCANCODE_A,
    /*   1 */   SCANCODE_S,
    /*   2 */   SCANCODE_D,
    /*   3 */   SCANCODE_F,
    /*   4 */   SCANCODE_H,
    /*   5 */   SCANCODE_G,
    /*   6 */   SCANCODE_Z,
    /*   7 */   SCANCODE_X,
    /*   8 */   SCANCODE_C,
    /*   9 */   SCANCODE_V,
    /*  10 */   SCANCODE_NONUSBACKSLASH, /* SCANCODE_NONUSBACKSLASH on ANSI and JIS keyboards (if this key would exist there), SCANCODE_GRAVE on ISO. (The USB keyboard driver actually translates these usage codes to different virtual key codes depending on whether the keyboard is ISO/ANSI/JIS. That's why you have to help it identify the keyboard type when you plug in a PC USB keyboard. It's a historical thing - ADB keyboards are wired this way.) */
    /*  11 */   SCANCODE_B,
    /*  12 */   SCANCODE_Q,
    /*  13 */   SCANCODE_W,
    /*  14 */   SCANCODE_E,
    /*  15 */   SCANCODE_R,
    /*  16 */   SCANCODE_Y,
    /*  17 */   SCANCODE_T,
    /*  18 */   SCANCODE_1,
    /*  19 */   SCANCODE_2,
    /*  20 */   SCANCODE_3,
    /*  21 */   SCANCODE_4,
    /*  22 */   SCANCODE_6,
    /*  23 */   SCANCODE_5,
    /*  24 */   SCANCODE_EQUALS,
    /*  25 */   SCANCODE_9,
    /*  26 */   SCANCODE_7,
    /*  27 */   SCANCODE_MINUS,
    /*  28 */   SCANCODE_8,
    /*  29 */   SCANCODE_0,
    /*  30 */   SCANCODE_RIGHTBRACKET,
    /*  31 */   SCANCODE_O,
    /*  32 */   SCANCODE_U,
    /*  33 */   SCANCODE_LEFTBRACKET,
    /*  34 */   SCANCODE_I,
    /*  35 */   SCANCODE_P,
    /*  36 */   SCANCODE_RETURN,
    /*  37 */   SCANCODE_L,
    /*  38 */   SCANCODE_J,
    /*  39 */   SCANCODE_APOSTROPHE,
    /*  40 */   SCANCODE_K,
    /*  41 */   SCANCODE_SEMICOLON,
    /*  42 */   SCANCODE_BACKSLASH,
    /*  43 */   SCANCODE_COMMA,
    /*  44 */   SCANCODE_SLASH,
    /*  45 */   SCANCODE_N,
    /*  46 */   SCANCODE_M,
    /*  47 */   SCANCODE_PERIOD,
    /*  48 */   SCANCODE_TAB,
    /*  49 */   SCANCODE_SPACE,
    /*  50 */   SCANCODE_GRAVE, /* SCANCODE_GRAVE on ANSI and JIS keyboards, SCANCODE_NONUSBACKSLASH on ISO (see comment about virtual key code 10 above) */
    /*  51 */   SCANCODE_BACKSPACE,
    /*  52 */   SCANCODE_KP_ENTER, /* keyboard enter on portables */
    /*  53 */   SCANCODE_ESCAPE,
    /*  54 */   SCANCODE_RGUI,
    /*  55 */   SCANCODE_LGUI,
    /*  56 */   SCANCODE_LSHIFT,
    /*  57 */   SCANCODE_CAPSLOCK,
    /*  58 */   SCANCODE_LALT,
    /*  59 */   SCANCODE_LCTRL,
    /*  60 */   SCANCODE_RSHIFT,
    /*  61 */   SCANCODE_RALT,
    /*  62 */   SCANCODE_RCTRL,
    /*  63 */   SCANCODE_RGUI, /* fn on portables, acts as a hardware-level modifier already, so we don't generate events for it, also XK_Meta_R */
    /*  64 */   SCANCODE_F17,
    /*  65 */   SCANCODE_KP_PERIOD,
    /*  66 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /*  67 */   SCANCODE_KP_MULTIPLY,
    /*  68 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /*  69 */   SCANCODE_KP_PLUS,
    /*  70 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /*  71 */   SCANCODE_NUMLOCKCLEAR,
    /*  72 */   SCANCODE_VOLUMEUP,
    /*  73 */   SCANCODE_VOLUMEDOWN,
    /*  74 */   SCANCODE_MUTE,
    /*  75 */   SCANCODE_KP_DIVIDE,
    /*  76 */   SCANCODE_KP_ENTER, /* keypad enter on external keyboards, fn-return on portables */
    /*  77 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /*  78 */   SCANCODE_KP_MINUS,
    /*  79 */   SCANCODE_F18,
    /*  80 */   SCANCODE_F19,
    /*  81 */   SCANCODE_KP_EQUALS,
    /*  82 */   SCANCODE_KP_0,
    /*  83 */   SCANCODE_KP_1,
    /*  84 */   SCANCODE_KP_2,
    /*  85 */   SCANCODE_KP_3,
    /*  86 */   SCANCODE_KP_4,
    /*  87 */   SCANCODE_KP_5,
    /*  88 */   SCANCODE_KP_6,
    /*  89 */   SCANCODE_KP_7,
    /*  90 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /*  91 */   SCANCODE_KP_8,
    /*  92 */   SCANCODE_KP_9,
    /*  93 */   SCANCODE_INTERNATIONAL3, /* Cosmo_USB2ADB.c says "Yen (JIS)" */
    /*  94 */   SCANCODE_INTERNATIONAL1, /* Cosmo_USB2ADB.c says "Ro (JIS)" */
    /*  95 */   SCANCODE_KP_COMMA, /* Cosmo_USB2ADB.c says ", JIS only" */
    /*  96 */   SCANCODE_F5,
    /*  97 */   SCANCODE_F6,
    /*  98 */   SCANCODE_F7,
    /*  99 */   SCANCODE_F3,
    /* 100 */   SCANCODE_F8,
    /* 101 */   SCANCODE_F9,
    /* 102 */   SCANCODE_LANG2, /* Cosmo_USB2ADB.c says "Eisu" */
    /* 103 */   SCANCODE_F11,
    /* 104 */   SCANCODE_LANG1, /* Cosmo_USB2ADB.c says "Kana" */
    /* 105 */   SCANCODE_PRINTSCREEN, /* On ADB keyboards, this key is labeled "F13/print screen". Problem: USB has different usage codes for these two functions. On Apple USB keyboards, the key is labeled "F13" and sends the F13 usage code (SCANCODE_F13). I decided to use SCANCODE_PRINTSCREEN here nevertheless since SDL applications are more likely to assume the presence of a print screen key than an F13 key. */
    /* 106 */   SCANCODE_F16,
    /* 107 */   SCANCODE_SCROLLLOCK, /* F14/scroll lock, see comment about F13/print screen above */
    /* 108 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /* 109 */   SCANCODE_F10,
    /* 110 */   SCANCODE_APPLICATION, /* windows contextual menu key, fn-enter on portables */
    /* 111 */   SCANCODE_F12,
    /* 112 */   SCANCODE_UNKNOWN, /* unknown (unused?) */
    /* 113 */   SCANCODE_PAUSE, /* F15/pause, see comment about F13/print screen above */
    /* 114 */   SCANCODE_INSERT, /* the key is actually labeled "help" on Apple keyboards, and works as such in Mac OS, but it sends the "insert" usage code even on Apple USB keyboards */
    /* 115 */   SCANCODE_HOME,
    /* 116 */   SCANCODE_PAGEUP,
    /* 117 */   SCANCODE_DELETE,
    /* 118 */   SCANCODE_F4,
    /* 119 */   SCANCODE_END,
    /* 120 */   SCANCODE_F2,
    /* 121 */   SCANCODE_PAGEDOWN,
    /* 122 */   SCANCODE_F1,
    /* 123 */   SCANCODE_LEFT,
    /* 124 */   SCANCODE_RIGHT,
    /* 125 */   SCANCODE_DOWN,
    /* 126 */   SCANCODE_UP,
    /* 127 */   SCANCODE_POWER
};

};  // namespace
