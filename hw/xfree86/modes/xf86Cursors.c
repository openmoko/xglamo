/*
 * Copyright © 2007 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#else
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#endif

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "xf86.h"
#include "xf86DDC.h"
#include "xf86Crtc.h"
#include "xf86Modes.h"
#include "xf86RandR12.h"
#include "X11/extensions/render.h"
#define DPMS_SERVER
#include "X11/extensions/dpms.h"
#include "X11/Xatom.h"
#ifdef RENDER
#include "picturestr.h"
#endif
#include "cursorstr.h"

/*
 * Given a screen coordinate, rotate back to a cursor source coordinate
 */
static void
xf86_crtc_rotate_coord (Rotation    rotation,
			int	    width,
			int	    height,
			int	    x_dst,
			int	    y_dst,
			int	    *x_src,
			int	    *y_src)
{
    if (rotation & RR_Reflect_X)
	x_dst = width - x_dst - 1;
    if (rotation & RR_Reflect_Y)
	y_dst = height - y_dst - 1;
    
    switch (rotation & 0xf) {
    case RR_Rotate_0:
	*x_src = x_dst;
	*y_src = y_dst;
	break;
    case RR_Rotate_90:
	*x_src = height - y_dst - 1;
	*y_src = x_dst;
	break;
    case RR_Rotate_180:
	*x_src = width - x_dst - 1;
	*y_src = height - y_dst - 1;
	break;
    case RR_Rotate_270:
	*x_src = y_dst;
	*y_src = width - x_dst - 1;
	break;
    }
}

/*
 * Convert an x coordinate to a position within the cursor bitmap
 */
static int
cursor_bitpos (int flags, int x, Bool mask)
{
    if (flags & HARDWARE_CURSOR_SWAP_SOURCE_AND_MASK)
	mask = !mask;
    if (flags & HARDWARE_CURSOR_NIBBLE_SWAPPED)
	x = (x & ~3) | (3 - (x & 3));
    if (flags & HARDWARE_CURSOR_BIT_ORDER_MSBFIRST)
	x = (x & ~7) | (7 - (x & 7));
    if (flags & HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1)
	x = (x << 1) + mask;
    else if (flags & HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_8)
	x = ((x & ~7) << 1) | (mask << 3) | (x & 7);
    else if (flags & HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_16)
	x = ((x & ~15) << 1) | (mask << 4) | (x & 15);
    else if (flags & HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_32)
	x = ((x & ~31) << 1) | (mask << 5) | (x & 31);
    else if (flags & HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_64)
	x = ((x & ~63) << 1) | (mask << 6) | (x & 63);
    return x;
}

/*
 * Fetch one bit from a cursor bitmap
 */
static CARD8
get_bit (CARD8 *image, int stride, int flags, int x, int y, Bool mask)
{
    x = cursor_bitpos (flags, x, mask);
    image += y * stride;
    return (image[(x >> 3)] >> (x & 7)) & 1;
}

/*
 * Set one bit in a cursor bitmap
 */
static void
set_bit (CARD8 *image, int stride, int flags, int x, int y, Bool mask)
{
    x = cursor_bitpos (flags, x, mask);
    image += y * stride;
    image[(x >> 3)] |= 1 << (x & 7);
}
    
/*
 * Load a two color cursor into a driver that supports only ARGB cursors
 */
static void
xf86_crtc_convert_cursor_to_argb (xf86CrtcPtr crtc, unsigned char *src)
{
    ScrnInfoPtr		scrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info = xf86_config->cursor_info;
    CARD32		*cursor_image = (CARD32 *) xf86_config->cursor_image;
    int			x, y;
    int			xin, yin;
    int			stride = cursor_info->MaxWidth >> 2;
    int			flags = cursor_info->Flags;
    CARD32		bits;

#ifdef ARGB_CURSOR
    crtc->cursor_argb = FALSE;
#endif

    for (y = 0; y < cursor_info->MaxHeight; y++)
	for (x = 0; x < cursor_info->MaxWidth; x++) 
	{
	    xf86_crtc_rotate_coord (crtc->rotation,
				    cursor_info->MaxWidth,
				    cursor_info->MaxHeight,
				    x, y, &xin, &yin);
	    if (get_bit (src, stride, flags, xin, yin, TRUE) ==
		((flags & HARDWARE_CURSOR_INVERT_MASK) == 0))
	    {
		if (get_bit (src, stride, flags, xin, yin, FALSE))
		    bits = xf86_config->cursor_fg;
		else
		    bits = xf86_config->cursor_bg;
	    }
	    else
		bits = 0;
	    cursor_image[y * cursor_info->MaxWidth + x] = bits;
	}
    crtc->funcs->load_cursor_argb (crtc, cursor_image);
}

/*
 * Set the colors for a two-color cursor (ignore for ARGB cursors)
 */
static void
xf86_set_cursor_colors (ScrnInfoPtr scrn, int bg, int fg)
{
    ScreenPtr		screen = scrn->pScreen;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    CursorPtr		cursor = xf86_config->cursor;
    int			c;
    CARD8		*bits = cursor ? cursor->devPriv[screen->myNum] : NULL;

    /* Save ARGB versions of these colors */
    xf86_config->cursor_fg = (CARD32) fg | 0xff000000;
    xf86_config->cursor_bg = (CARD32) bg | 0xff000000;
    
    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (crtc->enabled && !crtc->cursor_argb)
	{
	    if (crtc->funcs->load_cursor_image)
		crtc->funcs->set_cursor_colors (crtc, bg, fg);
	    else if (bits)
		xf86_crtc_convert_cursor_to_argb (crtc, bits);
	}
    }
}

static void
xf86_crtc_hide_cursor (xf86CrtcPtr crtc)
{
    if (crtc->cursor_shown)
    {
	crtc->funcs->hide_cursor (crtc);
	crtc->cursor_shown = FALSE;
    }
}

void
xf86_hide_cursors (ScrnInfoPtr scrn)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    xf86_config->cursor_on = FALSE;
    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (crtc->enabled)
	    xf86_crtc_hide_cursor (crtc);
    }
}
    
static void
xf86_crtc_show_cursor (xf86CrtcPtr crtc)
{
    if (!crtc->cursor_shown && crtc->cursor_in_range)
    {
	crtc->funcs->show_cursor (crtc);
	crtc->cursor_shown = TRUE;
    }
}

void
xf86_show_cursors (ScrnInfoPtr scrn)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    xf86_config->cursor_on = TRUE;
    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (crtc->enabled)
	    xf86_crtc_show_cursor (crtc);
    }
}
    
static void
xf86_crtc_set_cursor_position (xf86CrtcPtr crtc, int x, int y)
{
    ScrnInfoPtr		scrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info = xf86_config->cursor_info;
    DisplayModePtr	mode = &crtc->mode;
    int			x_temp;
    int			y_temp;
    Bool		in_range;

    /* 
     * Move to crtc coordinate space
     */
    x -= crtc->x;
    y -= crtc->y;
    
    /*
     * Rotate
     */
    switch ((crtc->rotation) & 0xf) {
    case RR_Rotate_0:
	break;
    case RR_Rotate_90:
	x_temp = y;
	y_temp = mode->VDisplay - cursor_info->MaxWidth - x;
	x = x_temp;
	y = y_temp;
	break;
    case RR_Rotate_180:
	x_temp = mode->HDisplay - cursor_info->MaxWidth - x;
	y_temp = mode->VDisplay - cursor_info->MaxHeight - y;
	x = x_temp;
	y = y_temp;
	break;
    case RR_Rotate_270:
	x_temp = mode->HDisplay - cursor_info->MaxHeight -  y;
	y_temp = x;
	x = x_temp;
	y = y_temp;
	break;
    }
    
    /*
     * Reflect
     */
    if (crtc->rotation & RR_Reflect_X)
	x = mode->HDisplay - cursor_info->MaxWidth - x;
    if (crtc->rotation & RR_Reflect_Y)
	y = mode->VDisplay - cursor_info->MaxHeight - y;

    /*
     * Disable the cursor when it is outside the viewport
     */
    in_range = TRUE;
    if (x >= mode->HDisplay || y >= mode->VDisplay ||
	x <= -cursor_info->MaxWidth || y <= -cursor_info->MaxHeight) 
    {
	in_range = FALSE;
	x = 0;
	y = 0;
    }

    crtc->cursor_in_range = in_range;
    
    if (in_range)
    {
	crtc->funcs->set_cursor_position (crtc, x, y);
	xf86_crtc_show_cursor (crtc);
    }
    else
	xf86_crtc_hide_cursor (crtc);
}

static void
xf86_set_cursor_position (ScrnInfoPtr scrn, int x, int y)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    /* undo what xf86HWCurs did to the coordinates */
    x += scrn->frameX0;
    y += scrn->frameY0;
    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (crtc->enabled)
	    xf86_crtc_set_cursor_position (crtc, x, y);
    }
}
    
/*
 * Load a two-color cursor into a crtc, performing rotation as needed
 */
static void
xf86_crtc_load_cursor_image (xf86CrtcPtr crtc, CARD8 *src)
{
    ScrnInfoPtr		scrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info = xf86_config->cursor_info;
    CARD8		*cursor_image;

#ifdef ARGB_CURSOR
    crtc->cursor_argb = FALSE;
#endif

    if (crtc->rotation == RR_Rotate_0)
	cursor_image = src;
    else
    {
        int x, y;
    	int xin, yin;
	int stride = cursor_info->MaxWidth >> 2;
	int flags = cursor_info->Flags;
	
	cursor_image = xf86_config->cursor_image;
	memset(cursor_image, 0, cursor_info->MaxWidth * stride);
	
        for (y = 0; y < cursor_info->MaxHeight; y++)
	    for (x = 0; x < cursor_info->MaxWidth; x++) 
	    {
		xf86_crtc_rotate_coord (crtc->rotation,
					cursor_info->MaxWidth,
					cursor_info->MaxHeight,
					x, y, &xin, &yin);
		if (get_bit(src, stride, flags, xin, yin, FALSE))
		    set_bit(cursor_image, stride, flags, x, y, FALSE);
		if (get_bit(src, stride, flags, xin, yin, TRUE))
		    set_bit(cursor_image, stride, flags, x, y, TRUE);
	    }
    }
    crtc->funcs->load_cursor_image (crtc, cursor_image);
}
    
/*
 * Load a cursor image into all active CRTCs
 */
static void
xf86_load_cursor_image (ScrnInfoPtr scrn, unsigned char *src)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (crtc->enabled)
	{
	    if (crtc->funcs->load_cursor_image)
		xf86_crtc_load_cursor_image (crtc, src);
	    else if (crtc->funcs->load_cursor_argb)
		xf86_crtc_convert_cursor_to_argb (crtc, src);
	}
    }
}

static Bool
xf86_use_hw_cursor (ScreenPtr screen, CursorPtr cursor)
{
    ScrnInfoPtr		scrn = xf86Screens[screen->myNum];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info = xf86_config->cursor_info;

    xf86_config->cursor = cursor;
    
    if (cursor->bits->width > cursor_info->MaxWidth ||
	cursor->bits->height> cursor_info->MaxHeight)
	return FALSE;

    return TRUE;
}

static Bool
xf86_use_hw_cursor_argb (ScreenPtr screen, CursorPtr cursor)
{
    ScrnInfoPtr		scrn = xf86Screens[screen->myNum];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info = xf86_config->cursor_info;
    
    xf86_config->cursor = cursor;
    
    /* Make sure ARGB support is available */
    if ((cursor_info->Flags & HARDWARE_CURSOR_ARGB) == 0)
	return FALSE;
    
    if (cursor->bits->width > cursor_info->MaxWidth ||
	cursor->bits->height> cursor_info->MaxHeight)
	return FALSE;

    return TRUE;
}

static void
xf86_crtc_load_cursor_argb (xf86CrtcPtr crtc, CursorPtr cursor)
{
    ScrnInfoPtr		scrn = crtc->scrn;
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info = xf86_config->cursor_info;
    CARD32		*cursor_image = (CARD32 *) xf86_config->cursor_image;
    CARD32		*cursor_source = (CARD32 *) cursor->bits->argb;
    int			x, y;
    int			xin, yin;
    CARD32		bits;
    int			source_width = cursor->bits->width;
    int			source_height = cursor->bits->height;
    int			image_width = cursor_info->MaxWidth;
    int			image_height = cursor_info->MaxHeight;
    
    for (y = 0; y < image_height; y++)
	for (x = 0; x < image_width; x++)
	{
	    xf86_crtc_rotate_coord (crtc->rotation, image_width, image_height,
				    x, y, &xin, &yin);
	    if (xin < source_width && yin < source_height)
		bits = cursor_source[yin * source_width + xin];
	    else
		bits = 0;
	    cursor_image[y * image_width + x] = bits;
	}
    
    crtc->funcs->load_cursor_argb (crtc, cursor_image);
}

static void
xf86_load_cursor_argb (ScrnInfoPtr scrn, CursorPtr cursor)
{
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    int			c;

    for (c = 0; c < xf86_config->num_crtc; c++)
    {
	xf86CrtcPtr crtc = xf86_config->crtc[c];

	if (crtc->enabled)
	    xf86_crtc_load_cursor_argb (crtc, cursor);
    }
}

Bool
xf86_cursors_init (ScreenPtr screen, int max_width, int max_height, int flags)
{
    ScrnInfoPtr		scrn = xf86Screens[screen->myNum];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    xf86CursorInfoPtr	cursor_info;

    cursor_info = xf86CreateCursorInfoRec();
    if (!cursor_info)
	return FALSE;

    xf86_config->cursor_image = xalloc (max_width * max_height * 4);

    if (!xf86_config->cursor_image)
    {
	xf86DestroyCursorInfoRec (cursor_info);
	return FALSE;
    }
	
    xf86_config->cursor_info = cursor_info;

    cursor_info->MaxWidth = max_width;
    cursor_info->MaxHeight = max_height;
    cursor_info->Flags = flags;

    cursor_info->SetCursorColors = xf86_set_cursor_colors;
    cursor_info->SetCursorPosition = xf86_set_cursor_position;
    cursor_info->LoadCursorImage = xf86_load_cursor_image;
    cursor_info->HideCursor = xf86_hide_cursors;
    cursor_info->ShowCursor = xf86_show_cursors;
    cursor_info->UseHWCursor = xf86_use_hw_cursor;
#ifdef ARGB_CURSOR
    if (flags & HARDWARE_CURSOR_ARGB)
    {
	cursor_info->UseHWCursorARGB = xf86_use_hw_cursor_argb;
	cursor_info->LoadCursorARGB = xf86_load_cursor_argb;
    }
#endif
    
    xf86_config->cursor = NULL;
    xf86_hide_cursors (scrn);
    
    return xf86InitCursor (screen, cursor_info);
}

/**
 * Called when anything on the screen is reconfigured.
 *
 * Reloads cursor images as needed, then adjusts cursor positions
 */

void
xf86_reload_cursors (ScreenPtr screen)
{
    ScrnInfoPtr		scrn;
    xf86CrtcConfigPtr   xf86_config;
    xf86CursorInfoPtr   cursor_info;
    CursorPtr		cursor;
    int			x, y;
    
    /* initial mode setting will not have set a screen yet */
    if (!screen)
	return;
    scrn = xf86Screens[screen->myNum];
    xf86_config = XF86_CRTC_CONFIG_PTR(scrn);

    /* make sure the cursor code has been initialized */
    cursor_info = xf86_config->cursor_info;
    if (!cursor_info)
	return;
    
    cursor = xf86_config->cursor;
    GetSpritePosition (&x, &y);
    if (!(cursor_info->Flags & HARDWARE_CURSOR_UPDATE_UNHIDDEN))
	(*cursor_info->HideCursor)(scrn);

    if (cursor)
    {
#ifdef ARGB_CURSOR
	if (cursor->bits->argb && cursor_info->LoadCursorARGB)
	    (*cursor_info->LoadCursorARGB) (scrn, cursor);
	else
#endif
	    (*cursor_info->LoadCursorImage)(cursor_info->pScrn,
					    cursor->devPriv[screen->myNum]);

	(*cursor_info->SetCursorPosition)(cursor_info->pScrn, x, y);
	(*cursor_info->ShowCursor)(cursor_info->pScrn);
    }
}

/**
 * Clean up CRTC-based cursor code
 */
void
xf86_cursors_fini (ScreenPtr screen)
{
    ScrnInfoPtr		scrn = xf86Screens[screen->myNum];
    xf86CrtcConfigPtr   xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
    
    if (xf86_config->cursor_info)
    {
	xf86DestroyCursorInfoRec (xf86_config->cursor_info);
	xf86_config->cursor_info = NULL;
    }
    if (xf86_config->cursor_image)
    {
	xfree (xf86_config->cursor_image);
	xf86_config->cursor_image = NULL;
    }
}
