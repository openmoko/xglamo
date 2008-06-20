/*
 * Copyright � 1999 Keith Packard
 * XKB integration � 2006 Nokia Corporation, author: Tomas Frydrych <tf@o-hand.com>
 *
 * LinuxKeyboardRead() XKB code based on xf86KbdLnx.c:
 * Copyright � 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright � 1994-2001 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "kdrive.h"
#define XK_PUBLISHING
#include <X11/keysym.h>

#include <linux/keyboard.h>
#include <linux/kd.h>
#include <termios.h>
#include <sys/ioctl.h>

extern int LinuxConsoleFd;

#ifdef XKB
int xkb_linux_key_index(int);
void LinuxKeyboardReadXkb(void*, unsigned char*);
#endif

static int seen_high_key = 0;
static unsigned char high_keys[3];

static const KeySym linux_to_x[256] = {
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	XK_BackSpace,	XK_Tab,		XK_Linefeed,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	XK_Escape,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	XK_space,	XK_exclam,	XK_quotedbl,	XK_numbersign,
	XK_dollar,	XK_percent,	XK_ampersand,	XK_apostrophe,
	XK_parenleft,	XK_parenright,	XK_asterisk,	XK_plus,
	XK_comma,	XK_minus,	XK_period,	XK_slash,
	XK_0,		XK_1,		XK_2,		XK_3,
	XK_4,		XK_5,		XK_6,		XK_7,
	XK_8,		XK_9,		XK_colon,	XK_semicolon,
	XK_less,	XK_equal,	XK_greater,	XK_question,
	XK_at,		XK_A,		XK_B,		XK_C,
	XK_D,		XK_E,		XK_F,		XK_G,
	XK_H,		XK_I,		XK_J,		XK_K,
	XK_L,		XK_M,		XK_N,		XK_O,
	XK_P,		XK_Q,		XK_R,		XK_S,
	XK_T,		XK_U,		XK_V,		XK_W,
	XK_X,		XK_Y,		XK_Z,		XK_bracketleft,
	XK_backslash,	XK_bracketright,XK_asciicircum,	XK_underscore,
	XK_grave,	XK_a,		XK_b,		XK_c,
	XK_d,		XK_e,		XK_f,		XK_g,
	XK_h,		XK_i,		XK_j,		XK_k,
	XK_l,		XK_m,		XK_n,		XK_o,
	XK_p,		XK_q,		XK_r,		XK_s,
	XK_t,		XK_u,		XK_v,		XK_w,
	XK_x,		XK_y,		XK_z,		XK_braceleft,
	XK_bar,		XK_braceright,	XK_asciitilde,	XK_BackSpace,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	NoSymbol,	NoSymbol,	NoSymbol,	NoSymbol,
	XK_nobreakspace,XK_exclamdown,	XK_cent,	XK_sterling,
	XK_currency,	XK_yen,		XK_brokenbar,	XK_section,
	XK_diaeresis,	XK_copyright,	XK_ordfeminine,	XK_guillemotleft,
	XK_notsign,	XK_hyphen,	XK_registered,	XK_macron,
	XK_degree,	XK_plusminus,	XK_twosuperior,	XK_threesuperior,
	XK_acute,	XK_mu,		XK_paragraph,	XK_periodcentered,
	XK_cedilla,	XK_onesuperior,	XK_masculine,	XK_guillemotright,
	XK_onequarter,	XK_onehalf,	XK_threequarters,XK_questiondown,
	XK_Agrave,	XK_Aacute,	XK_Acircumflex,	XK_Atilde,
	XK_Adiaeresis,	XK_Aring,	XK_AE,		XK_Ccedilla,
	XK_Egrave,	XK_Eacute,	XK_Ecircumflex,	XK_Ediaeresis,
	XK_Igrave,	XK_Iacute,	XK_Icircumflex,	XK_Idiaeresis,
	XK_ETH,		XK_Ntilde,	XK_Ograve,	XK_Oacute,
	XK_Ocircumflex,	XK_Otilde,	XK_Odiaeresis,	XK_multiply,
	XK_Ooblique,	XK_Ugrave,	XK_Uacute,	XK_Ucircumflex,
	XK_Udiaeresis,	XK_Yacute,	XK_THORN,	XK_ssharp,
	XK_agrave,	XK_aacute,	XK_acircumflex,	XK_atilde,
	XK_adiaeresis,	XK_aring,	XK_ae,		XK_ccedilla,
	XK_egrave,	XK_eacute,	XK_ecircumflex,	XK_ediaeresis,
	XK_igrave,	XK_iacute,	XK_icircumflex,	XK_idiaeresis,
	XK_eth,		XK_ntilde,	XK_ograve,	XK_oacute,
	XK_ocircumflex,	XK_otilde,	XK_odiaeresis,	XK_division,
	XK_oslash,	XK_ugrave,	XK_uacute,	XK_ucircumflex,
	XK_udiaeresis,	XK_yacute,	XK_thorn,	XK_ydiaeresis
};


static unsigned char tbl[KD_MAX_WIDTH] =
{
    0,
    1 << KG_SHIFT,
    (1 << KG_ALTGR),
    (1 << KG_ALTGR) | (1 << KG_SHIFT)
};

static void
readKernelMapping(KdKeyboardInfo *ki)
{
    KeySym	    *k;
    int		    i, j;
    struct kbentry  kbe;
    int		    minKeyCode, maxKeyCode;
    int		    row;
    int             fd;

    if (!ki)
        return;

    fd = LinuxConsoleFd;
    
    minKeyCode = NR_KEYS;
    maxKeyCode = 0;
    row = 0;
    ki->keySyms.mapWidth = KD_MAX_WIDTH;
    for (i = 0; i < NR_KEYS && row < KD_MAX_LENGTH; ++i)
    {
#ifdef XKB
        kbe.kb_index = xkb_linux_key_index(i);
#else
        kbe.kb_index = i;
#endif

        k = ki->keySyms.map + row * ki->keySyms.mapWidth;
	
	for (j = 0; j < ki->keySyms.mapWidth; ++j)
	{
	    unsigned short kval;

	    k[j] = NoSymbol;

	    kbe.kb_table = tbl[j];
	    kbe.kb_value = 0;
	    if (ioctl(fd, KDGKBENT, &kbe))
		continue;

	    kval = KVAL(kbe.kb_value);
	    switch (KTYP(kbe.kb_value))
	    {
	    case KT_LATIN:
	    case KT_LETTER:
		k[j] = linux_to_x[kval];
		break;

	    case KT_FN:
		if (kval <= 19)
		    k[j] = XK_F1 + kval;
		else switch (kbe.kb_value)
		{
		case K_FIND:
		    k[j] = XK_Home; /* or XK_Find */
		    break;
		case K_INSERT:
		    k[j] = XK_Insert;
		    break;
		case K_REMOVE:
		    k[j] = XK_Delete;
		    break;
		case K_SELECT:
		    k[j] = XK_End; /* or XK_Select */
		    break;
		case K_PGUP:
		    k[j] = XK_Prior;
		    break;
		case K_PGDN:
		    k[j] = XK_Next;
		    break;
		case K_HELP:
		    k[j] = XK_Help;
		    break;
		case K_DO:
		    k[j] = XK_Execute;
		    break;
		case K_PAUSE:
		    k[j] = XK_Pause;
		    break;
		case K_MACRO:
		    k[j] = XK_Menu;
		    break;
		default:
		    break;
		}
		break;

	    case KT_SPEC:
		switch (kbe.kb_value)
		{
		case K_ENTER:
		    k[j] = XK_Return;
		    break;
		case K_BREAK:
		    k[j] = XK_Break;
		    break;
		case K_CAPS:
		    k[j] = XK_Caps_Lock;
		    break;
		case K_NUM:
		    k[j] = XK_Num_Lock;
		    break;
		case K_HOLD:
		    k[j] = XK_Scroll_Lock;
		    break;
		case K_COMPOSE:
		    k[j] = XK_Multi_key;
		    break;
		default:
		    break;
		}
		break;

	    case KT_PAD:
		switch (kbe.kb_value)
		{
		case K_PPLUS:
		    k[j] = XK_KP_Add;
		    break;
		case K_PMINUS:
		    k[j] = XK_KP_Subtract;
		    break;
		case K_PSTAR:
		    k[j] = XK_KP_Multiply;
		    break;
		case K_PSLASH:
		    k[j] = XK_KP_Divide;
		    break;
		case K_PENTER:
		    k[j] = XK_KP_Enter;
		    break;
		case K_PCOMMA:
		    k[j] = XK_KP_Separator;
		    break;
		case K_PDOT:
		    k[j] = XK_KP_Decimal;
		    break;
		case K_PPLUSMINUS:
		    k[j] = XK_KP_Subtract;
		    break;
		default:
		    if (kval <= 9)
			k[j] = XK_KP_0 + kval;
		    break;
		}
		break;

		/*
		 * KT_DEAD keys are for accelerated diacritical creation.
		 */
	    case KT_DEAD:
		switch (kbe.kb_value)
		{
		case K_DGRAVE:
		    k[j] = XK_dead_grave;
		    break;
		case K_DACUTE:
		    k[j] = XK_dead_acute;
		    break;
		case K_DCIRCM:
		    k[j] = XK_dead_circumflex;
		    break;
		case K_DTILDE:
		    k[j] = XK_dead_tilde;
		    break;
		case K_DDIERE:
		    k[j] = XK_dead_diaeresis;
		    break;
		}
		break;

	    case KT_CUR:
		switch (kbe.kb_value)
		{
		case K_DOWN:
		    k[j] = XK_Down;
		    break;
		case K_LEFT:
		    k[j] = XK_Left;
		    break;
		case K_RIGHT:
		    k[j] = XK_Right;
		    break;
		case K_UP:
		    k[j] = XK_Up;
		    break;
		}
		break;

	    case KT_SHIFT:
		switch (kbe.kb_value)
		{
		case K_ALTGR:
		    k[j] = XK_Mode_switch;
		    break;
		case K_ALT:
		    k[j] = (kbe.kb_index == 0x64 ?
			  XK_Alt_R : XK_Alt_L);
		    break;
		case K_CTRL:
		    k[j] = (kbe.kb_index == 0x61 ?
			  XK_Control_R : XK_Control_L);
		    break;
		case K_CTRLL:
		    k[j] = XK_Control_L;
		    break;
		case K_CTRLR:
		    k[j] = XK_Control_R;
		    break;
		case K_SHIFT:
		    k[j] = (kbe.kb_index == 0x36 ?
			  XK_Shift_R : XK_Shift_L);
		    break;
		case K_SHIFTL:
		    k[j] = XK_Shift_L;
		    break;
		case K_SHIFTR:
		    k[j] = XK_Shift_R;
		    break;
		default:
		    break;
		}
		break;

		/*
		 * KT_ASCII keys accumulate a 3 digit decimal number that gets
		 * emitted when the shift state changes. We can't emulate that.
		 */
	    case KT_ASCII:
		break;

	    case KT_LOCK:
		if (kbe.kb_value == K_SHIFTLOCK)
		    k[j] = XK_Shift_Lock;
		break;

#ifdef KT_X
	    case KT_X:
		/* depends on new keyboard symbols in file linux/keyboard.h */
		if(kbe.kb_value == K_XMENU) k[j] = XK_Menu;
		if(kbe.kb_value == K_XTELEPHONE) k[j] = XK_telephone;
		break;
#endif
#ifdef KT_XF
	    case KT_XF:
		/* special linux keysyms which map directly to XF86 keysyms */
		k[j] = (kbe.kb_value & 0xFF) + 0x1008FF00;
		break;
#endif
		
	    default:
		break;
	    }
	    if (i < minKeyCode)
		minKeyCode = i;
	    if (i > maxKeyCode)
		maxKeyCode = i;
	}

	if (minKeyCode == NR_KEYS)
	    continue;

	if (k[3] == k[2]) k[3] = NoSymbol;
	if (k[2] == k[1]) k[2] = NoSymbol;
	if (k[1] == k[0]) k[1] = NoSymbol;
	if (k[0] == k[2] && k[1] == k[3]) k[2] = k[3] = NoSymbol;
	if (k[3] == k[0] && k[2] == k[1] && k[2] == NoSymbol) k[3] =NoSymbol;
	row++;
    }
    ki->minScanCode = minKeyCode;
    ki->maxScanCode = maxKeyCode;
}

static void
LinuxKeyboardRead (int fd, void *closure)
{
    unsigned char   buf[256], *b;
    int		    n;

    while ((n = read (fd, buf, sizeof (buf))) > 0) {
	b = buf;
	while (n--) {
#ifdef XKB
            if (!noXkbExtension) {
               LinuxKeyboardReadXkb(closure, b++);
            } else
#endif
            {
                /*
                 * See drivers/char/keyboard.c in the linux tree for extended
                 * medium raw for keys above 127.
                 */ 
                int code = b[0] & 0x7f;

                if (seen_high_key > 0) {
                    high_keys[seen_high_key++] = b[0];

                    /*
                     * Commit high key codes
                     */ 
                    if (seen_high_key >= 3) {
                        int keycode = ((high_keys[1] & 0x7f) << 7) | (high_keys[2] & 0x7f);
                        if (keycode < NR_KEYS)
                            KdEnqueueKeyboardEvent (closure, keycode, high_keys[0] & 0x80);

                        seen_high_key = 0;
                        high_keys[0] = high_keys[1] = high_keys[2] = 0;
                    }
                } else if (code != 0) {
                    KdEnqueueKeyboardEvent (closure, code, b[0] & 0x80);
                } else {
                    seen_high_key = 1;
                    high_keys[0] = b[0];
                }
            }
	}
    }
}

static int		LinuxKbdTrans;
static struct termios	LinuxTermios;

static Status
LinuxKeyboardEnable (KdKeyboardInfo *ki)
{
    struct termios nTty;
    unsigned char   buf[256];
    int		    n;
    int             fd;

    if (!ki)
        return !Success;

    fd = LinuxConsoleFd;
    ki->driverPrivate = (void *) fd;

    ioctl (fd, KDGKBMODE, &LinuxKbdTrans);
    tcgetattr (fd, &LinuxTermios);
#ifdef XKB
    if (!noXkbExtension)
        ioctl(fd, KDSKBMODE, K_RAW);
    else
#else
        ioctl(fd, KDSKBMODE, K_MEDIUMRAW);
#endif
    nTty = LinuxTermios;
    nTty.c_iflag = (IGNPAR | IGNBRK) & (~PARMRK) & (~ISTRIP);
    nTty.c_oflag = 0;
    nTty.c_cflag = CREAD | CS8;
    nTty.c_lflag = 0;
    nTty.c_cc[VTIME]=0;
    nTty.c_cc[VMIN]=1;
    cfsetispeed(&nTty, 9600);
    cfsetospeed(&nTty, 9600);
    tcsetattr(fd, TCSANOW, &nTty);
    /*
     * Flush any pending keystrokes
     */
    while ((n = read (fd, buf, sizeof (buf))) > 0)
	;
    KdRegisterFd (fd, LinuxKeyboardRead, ki);
    return Success;
}

static void
LinuxKeyboardDisable (KdKeyboardInfo *ki)
{
    int fd;
    
    if (!ki)
        return;

    fd = (int) ki->driverPrivate;

    KdUnregisterFd(ki, fd, FALSE);
    ioctl(fd, KDSKBMODE, LinuxKbdTrans);
    tcsetattr(fd, TCSANOW, &LinuxTermios);
}

static Status
LinuxKeyboardInit (KdKeyboardInfo *ki)
{
    if (!ki)
        return !Success;

    if (ki->path)
        xfree(ki->path);
    ki->path = KdSaveString("console");
    if (ki->name)
        xfree(ki->name);
    ki->name = KdSaveString("Linux console keyboard");

    readKernelMapping (ki);

    return Success;
}

static void
LinuxKeyboardLeds (KdKeyboardInfo *ki, int leds)
{
    if (!ki)
        return;

    ioctl ((int)ki->driverPrivate, KDSETLED, leds & 7);
}

KdKeyboardDriver LinuxKeyboardDriver = {
    "keyboard",
    .Init = LinuxKeyboardInit,
    .Enable = LinuxKeyboardEnable,
    .Leds = LinuxKeyboardLeds,
    .Disable = LinuxKeyboardDisable,
};
