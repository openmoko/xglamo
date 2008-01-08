/*
 * Copyright © 2007 OpenMoko, Inc.
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
 *
 * Author:
 *  Dodji Seketeli <dodji@openedhand.com>
 */

#include "glamo-log.h"
#include "glamo-funcs.h"
#include "glamo-regs.h"
#include "glamo.h"

#define GLAMO_OUT_REG(glamo_mmio, reg, val) \
	(*((volatile unsigned short *) ((glamo_mmio) + (reg))) = (val))

#define GLAMO_IN_REG(glamo_mmio, reg) \
	(*((volatile unsigned short *) ((glamo_mmio) + (reg))))

void
glamoOutReg(ScreenPtr pScreen, unsigned short reg, unsigned short val)
{
	KdScreenPriv(pScreen);
	GLAMOCardInfo(pScreenPriv);
	GLAMO_LOG("mark: pScreen:%#x, reg:%#x, val:%#x, reg_base:%#x\n",
		  pScreen, reg, val, glamoc->reg_base);
	if (!glamoc->reg_base) {
		GLAMO_LOG_ERROR("got null glamoc->reg_base\n");
		return;
	}
	GLAMO_OUT_REG(glamoc->reg_base, reg, val);
}

unsigned short
glamoInReg(ScreenPtr pScreen, unsigned short reg)
{
	KdScreenPriv(pScreen);
	GLAMOCardInfo(pScreenPriv);
	GLAMO_LOG("mark: pScreen:%#x, reg:%#x, reg_base:%#x\n",
		  pScreen, reg, glamoc->reg_base);
	if (!glamoc->reg_base) {
		GLAMO_LOG_ERROR("got null glamoc->reg_base\n");
		return 0;
	}
	return GLAMO_IN_REG(glamoc->reg_base, reg);
}

void
glamoSetBitMask(ScreenPtr pScreen, int reg, int mask, int val)
{
	int old;
	old = glamoInReg(pScreen, reg);
	old &= ~mask;
	old |= val & mask;
	GLAMO_LOG("mark\n");
	glamoOutReg(pScreen, reg, old);
}

void
setCmdMode (ScreenPtr pScreen, Bool on)
{
	if (on) {
		GLAMO_LOG("mark\n");
		/*TODO: busy waiting is bad*/
		while (!glamoInReg(pScreen, GLAMO_REG_LCD_STATUS1) & (1 << 15)) {
			GLAMO_LOG("mark\n");
			usleep(1 * 1000);
		}
		GLAMO_LOG("mark\n");
		glamoOutReg(pScreen,
			    GLAMO_REG_LCD_COMMAND1,
			    GLAMO_LCD_CMD_TYPE_DISP |
			    GLAMO_LCD_CMD_DATA_FIRE_VSYNC);
		GLAMO_LOG("mark\n");
		while (!glamoInReg(pScreen, GLAMO_REG_LCD_STATUS2) & (1 << 12)) {
			GLAMO_LOG("mark\n");
			usleep(1 * 1000);
		}
		/* wait */
		GLAMO_LOG("mark\n");
		usleep(100 * 1000);
	} else {
		GLAMO_LOG("mark\n");
		glamoOutReg(pScreen,
			    GLAMO_REG_LCD_COMMAND1,
			    GLAMO_LCD_CMD_TYPE_DISP |
			    GLAMO_LCD_CMD_DATA_DISP_SYNC);
		GLAMO_LOG("mark\n");
		glamoOutReg(pScreen,
			    GLAMO_REG_LCD_COMMAND1,
			    GLAMO_LCD_CMD_TYPE_DISP |
			    GLAMO_LCD_CMD_DATA_DISP_FIRE);
	}
}

/*
Bool
glamoRotateLCD (ScreenPtr pScreen, Rotation rotation)
{
	int rot=0;

	GLAMO_LOG("enter\n");
	switch(rotation) {
		case RR_Rotate_0 :
			rot = GLAMO_LCD_ROT_MODE_0;
			break;
		case RR_Rotate_90:
			rot = GLAMO_LCD_ROT_MODE_90;
			break;
		case RR_Rotate_180:
			rot = GLAMO_LCD_ROT_MODE_180;
			break;
		case RR_Rotate_270:
			rot = GLAMO_LCD_ROT_MODE_270;
			break;
		default:
			rot = GLAMO_LCD_ROT_MODE_0;
			break;
	}

	GLAMO_LOG("mark\n");
	setCmdMode(pScreen, 1);
	GLAMO_LOG("mark\n");
	glamoSetBitMask(pScreen, GLAMO_REG_LCD_WIDTH, GLAMO_LCD_ROT_MODE_MASK, rot);
	GLAMO_LOG("mark\n");
	glamoSetBitMask(pScreen, GLAMO_REG_LCD_MODE1,
			GLAMO_LCD_MODE1_ROTATE_EN,
			(rot != GLAMO_LCD_ROT_MODE_0) ?
			GLAMO_LCD_MODE1_ROTATE_EN : 0);
	GLAMO_LOG("mark\n");
	setCmdMode(pScreen, 0);
	GLAMO_LOG("mark\n");
	GLAMO_LOG("leave\n");
	return TRUE;
}
*/
