
#pragma once    // really only included from the system wrapper

// This based on SDL2-2.0.4/src/event/scancode_windows.h with the
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

/* Windows scancode to SDL scancode mapping table */
/* derived from Microsoft scan code document, http://download.microsoft.com/download/1/6/1/161ba512-40e2-4cc9-843a-923143f3456c/scancode.doc */

static const Scancode windows_scancode_table[] =
{
	/*	0						1							2							3							4						5							6							7 */
	/*	8						9							A							B							C						D							E							F */
	SCANCODE_UNKNOWN,		SCANCODE_ESCAPE,		SCANCODE_1,				SCANCODE_2,				SCANCODE_3,			SCANCODE_4,				SCANCODE_5,				SCANCODE_6,			/* 0 */
	SCANCODE_7,				SCANCODE_8,				SCANCODE_9,				SCANCODE_0,				SCANCODE_MINUS,		SCANCODE_EQUALS,		SCANCODE_BACKSPACE,		SCANCODE_TAB,		/* 0 */

	SCANCODE_Q,				SCANCODE_W,				SCANCODE_E,				SCANCODE_R,				SCANCODE_T,			SCANCODE_Y,				SCANCODE_U,				SCANCODE_I,			/* 1 */
	SCANCODE_O,				SCANCODE_P,				SCANCODE_LEFTBRACKET,	SCANCODE_RIGHTBRACKET,	SCANCODE_RETURN,	SCANCODE_LCTRL,			SCANCODE_A,				SCANCODE_S,			/* 1 */

	SCANCODE_D,				SCANCODE_F,				SCANCODE_G,				SCANCODE_H,				SCANCODE_J,			SCANCODE_K,				SCANCODE_L,				SCANCODE_SEMICOLON,	/* 2 */
	SCANCODE_APOSTROPHE,	SCANCODE_GRAVE,			SCANCODE_LSHIFT,		SCANCODE_BACKSLASH,		SCANCODE_Z,			SCANCODE_X,				SCANCODE_C,				SCANCODE_V,			/* 2 */

	SCANCODE_B,				SCANCODE_N,				SCANCODE_M,				SCANCODE_COMMA,			SCANCODE_PERIOD,	SCANCODE_SLASH,			SCANCODE_RSHIFT,		SCANCODE_PRINTSCREEN,/* 3 */
	SCANCODE_LALT,			SCANCODE_SPACE,			SCANCODE_CAPSLOCK,		SCANCODE_F1,			SCANCODE_F2,		SCANCODE_F3,			SCANCODE_F4,			SCANCODE_F5,		/* 3 */

	SCANCODE_F6,			SCANCODE_F7,			SCANCODE_F8,			SCANCODE_F9,			SCANCODE_F10,		SCANCODE_NUMLOCKCLEAR,	SCANCODE_SCROLLLOCK,	SCANCODE_HOME,		/* 4 */
	SCANCODE_UP,			SCANCODE_PAGEUP,		SCANCODE_KP_MINUS,		SCANCODE_LEFT,			SCANCODE_KP_5,		SCANCODE_RIGHT,			SCANCODE_KP_PLUS,		SCANCODE_END,		/* 4 */

	SCANCODE_DOWN,			SCANCODE_PAGEDOWN,		SCANCODE_INSERT,		SCANCODE_DELETE,		SCANCODE_UNKNOWN,	SCANCODE_UNKNOWN,		SCANCODE_NONUSBACKSLASH,SCANCODE_F11,		/* 5 */
	SCANCODE_F12,			SCANCODE_PAUSE,			SCANCODE_UNKNOWN,		SCANCODE_LGUI,			SCANCODE_RGUI,		SCANCODE_APPLICATION,	SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,	/* 5 */

	SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,		SCANCODE_F13,		SCANCODE_F14,			SCANCODE_F15,			SCANCODE_F16,		/* 6 */
	SCANCODE_F17,			SCANCODE_F18,			SCANCODE_F19,			SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,	SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,	/* 6 */

	SCANCODE_INTERNATIONAL2,		SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,		SCANCODE_INTERNATIONAL1,		SCANCODE_UNKNOWN,	SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN,	/* 7 */
	SCANCODE_UNKNOWN,		SCANCODE_INTERNATIONAL4,		SCANCODE_UNKNOWN,		SCANCODE_INTERNATIONAL5,		SCANCODE_UNKNOWN,	SCANCODE_INTERNATIONAL3,		SCANCODE_UNKNOWN,		SCANCODE_UNKNOWN	/* 7 */
};

// This is originally not in the header, but might just as well keep it here
// Original name is WindowsScanCodeToSDLScanCode in SDL_windowsevents.c
static Scancode decodeWindowsScancode(LPARAM lParam, WPARAM wParam)
{
    Scancode code;
    char bIsExtended;
    int nScanCode = (lParam >> 16) & 0xFF;

    /* 0x45 here to work around both pause and numlock sharing the same scancode, so use the VK key to tell them apart */
    if (nScanCode == 0 || nScanCode == 0x45) {
        switch(wParam) {
        case VK_CLEAR: return SCANCODE_CLEAR;
        case VK_MODECHANGE: return SCANCODE_MODE;
        case VK_SELECT: return SCANCODE_SELECT;
        case VK_EXECUTE: return SCANCODE_EXECUTE;
        case VK_HELP: return SCANCODE_HELP;
        case VK_PAUSE: return SCANCODE_PAUSE;
        case VK_NUMLOCK: return SCANCODE_NUMLOCKCLEAR;

        case VK_F13: return SCANCODE_F13;
        case VK_F14: return SCANCODE_F14;
        case VK_F15: return SCANCODE_F15;
        case VK_F16: return SCANCODE_F16;
        case VK_F17: return SCANCODE_F17;
        case VK_F18: return SCANCODE_F18;
        case VK_F19: return SCANCODE_F19;
        case VK_F20: return SCANCODE_F20;
        case VK_F21: return SCANCODE_F21;
        case VK_F22: return SCANCODE_F22;
        case VK_F23: return SCANCODE_F23;
        case VK_F24: return SCANCODE_F24;

        case VK_OEM_NEC_EQUAL: return SCANCODE_KP_EQUALS;
        case VK_BROWSER_BACK: return SCANCODE_AC_BACK;
        case VK_BROWSER_FORWARD: return SCANCODE_AC_FORWARD;
        case VK_BROWSER_REFRESH: return SCANCODE_AC_REFRESH;
        case VK_BROWSER_STOP: return SCANCODE_AC_STOP;
        case VK_BROWSER_SEARCH: return SCANCODE_AC_SEARCH;
        case VK_BROWSER_FAVORITES: return SCANCODE_AC_BOOKMARKS;
        case VK_BROWSER_HOME: return SCANCODE_AC_HOME;
        case VK_VOLUME_MUTE: return SCANCODE_AUDIOMUTE;
        case VK_VOLUME_DOWN: return SCANCODE_VOLUMEDOWN;
        case VK_VOLUME_UP: return SCANCODE_VOLUMEUP;

        case VK_MEDIA_NEXT_TRACK: return SCANCODE_AUDIONEXT;
        case VK_MEDIA_PREV_TRACK: return SCANCODE_AUDIOPREV;
        case VK_MEDIA_STOP: return SCANCODE_AUDIOSTOP;
        case VK_MEDIA_PLAY_PAUSE: return SCANCODE_AUDIOPLAY;
        case VK_LAUNCH_MAIL: return SCANCODE_MAIL;
        case VK_LAUNCH_MEDIA_SELECT: return SCANCODE_MEDIASELECT;

        case VK_OEM_102: return SCANCODE_NONUSBACKSLASH;

        case VK_ATTN: return SCANCODE_SYSREQ;
        case VK_CRSEL: return SCANCODE_CRSEL;
        case VK_EXSEL: return SCANCODE_EXSEL;
        case VK_OEM_CLEAR: return SCANCODE_CLEAR;

        case VK_LAUNCH_APP1: return SCANCODE_APP1;
        case VK_LAUNCH_APP2: return SCANCODE_APP2;

        default: return SCANCODE_UNKNOWN;
        }
    }

    if (nScanCode > 127)
        return SCANCODE_UNKNOWN;

    code = windows_scancode_table[nScanCode];

    bIsExtended = (lParam & (1 << 24)) != 0;
    if (!bIsExtended) {
        switch (code) {
        case SCANCODE_HOME:
            return SCANCODE_KP_7;
        case SCANCODE_UP:
            return SCANCODE_KP_8;
        case SCANCODE_PAGEUP:
            return SCANCODE_KP_9;
        case SCANCODE_LEFT:
            return SCANCODE_KP_4;
        case SCANCODE_RIGHT:
            return SCANCODE_KP_6;
        case SCANCODE_END:
            return SCANCODE_KP_1;
        case SCANCODE_DOWN:
            return SCANCODE_KP_2;
        case SCANCODE_PAGEDOWN:
            return SCANCODE_KP_3;
        case SCANCODE_INSERT:
            return SCANCODE_KP_0;
        case SCANCODE_DELETE:
            return SCANCODE_KP_PERIOD;
        case SCANCODE_PRINTSCREEN:
            return SCANCODE_KP_MULTIPLY;
        default:
            break;
        }
    } else {
        switch (code) {
        case SCANCODE_RETURN:
            return SCANCODE_KP_ENTER;
        case SCANCODE_LALT:
            return SCANCODE_RALT;
        case SCANCODE_LCTRL:
            return SCANCODE_RCTRL;
        case SCANCODE_SLASH:
            return SCANCODE_KP_DIVIDE;
        case SCANCODE_CAPSLOCK:
            return SCANCODE_KP_PLUS;
        default:
            break;
        }
    }

    return code;
}

}; // namespace
