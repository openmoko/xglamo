/*
 * Copyright © 2007 OpenMoko, Inc.
 *
 * This driver is based on Xati,
 * Copyright © 2003 Eric Anholt
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

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "glamo-log.h"
#include "glamo.h"
#include "glamo-regs.h"
#include "glamo_dma.h"
#include "glamo_draw.h"
#include "kaa.h"

static const CARD8 GLAMOSolidRop[16] = {
    /* GXclear      */      0x00,         /* 0 */
    /* GXand        */      0xa0,         /* src AND dst */
    /* GXandReverse */      0x50,         /* src AND NOT dst */
    /* GXcopy       */      0xf0,         /* src */
    /* GXandInverted*/      0x0a,         /* NOT src AND dst */
    /* GXnoop       */      0xaa,         /* dst */
    /* GXxor        */      0x5a,         /* src XOR dst */
    /* GXor         */      0xfa,         /* src OR dst */
    /* GXnor        */      0x05,         /* NOT src AND NOT dst */
    /* GXequiv      */      0xa5,         /* NOT src XOR dst */
    /* GXinvert     */      0x55,         /* NOT dst */
    /* GXorReverse  */      0xf5,         /* src OR NOT dst */
    /* GXcopyInverted*/     0x0f,         /* NOT src */
    /* GXorInverted */      0xaf,         /* NOT src OR dst */
    /* GXnand       */      0x5f,         /* NOT src OR NOT dst */
    /* GXset        */      0xff,         /* 1 */
};

static const CARD8 GLAMOBltRop[16] = {
    /* GXclear      */      0x00,         /* 0 */
    /* GXand        */      0x88,         /* src AND dst */
    /* GXandReverse */      0x44,         /* src AND NOT dst */
    /* GXcopy       */      0xcc,         /* src */
    /* GXandInverted*/      0x22,         /* NOT src AND dst */
    /* GXnoop       */      0xaa,         /* dst */
    /* GXxor        */      0x66,         /* src XOR dst */
    /* GXor         */      0xee,         /* src OR dst */
    /* GXnor        */      0x11,         /* NOT src AND NOT dst */
    /* GXequiv      */      0x99,         /* NOT src XOR dst */
    /* GXinvert     */      0x55,         /* NOT dst */
    /* GXorReverse  */      0xdd,         /* src OR NOT dst */
    /* GXcopyInverted*/     0x33,         /* NOT src */
    /* GXorInverted */      0xbb,         /* NOT src OR dst */
    /* GXnand       */      0x77,         /* NOT src OR NOT dst */
    /* GXset        */      0xff,         /* 1 */
};

GLAMOScreenInfo *accel_glamos;
CARD32 settings, color, src_pitch_offset, dst_pitch_offset;

int sample_count;
float sample_offsets_x[255];
float sample_offsets_y[255];

void
GLAMODrawSetup(ScreenPtr pScreen)
{
	GLAMOEngineEnable(pScreen, GLAMO_ENGINE_2D);
	GLAMOEngineReset(pScreen, GLAMO_ENGINE_2D);
}

static void
GLAMOWaitMarker(ScreenPtr pScreen, int marker)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

	GLAMO_LOG("enter");
	GLAMOWaitIdle(glamos);
	GLAMO_LOG("leave");
}

static Bool
GLAMOPrepareSolid(PixmapPtr pPix, int alu, Pixel pm, Pixel fg)
{
	KdScreenPriv(pPix->drawable.pScreen);
	GLAMOScreenInfo(pScreenPriv);
	CARD32 offset, pitch;
	FbBits mask;
	RING_LOCALS;
        return FALSE;

	if (pPix->drawable.bitsPerPixel != 16)
		GLAMO_FALLBACK(("Only 16bpp is supported\n"));

	mask = FbFullMask(16);
	if ((pm & mask) != mask)
		GLAMO_FALLBACK(("Can't do planemask 0x%08x\n", (unsigned int) pm));

	accel_glamos = glamos;

	settings = GLAMOSolidRop[alu] << 8;
	offset = ((CARD8 *) pPix->devPrivate.ptr -
			pScreenPriv->screen->memory_base);
	pitch = pPix->devKind;

	GLAMO_LOG("enter");

	BEGIN_DMA(12);
	OUT_REG(GLAMO_REG_2D_DST_ADDRL, offset & 0xffff);
	OUT_REG(GLAMO_REG_2D_DST_ADDRH, (offset >> 16) & 0x7f);
	OUT_REG(GLAMO_REG_2D_DST_PITCH, pitch);
	OUT_REG(GLAMO_REG_2D_DST_HEIGHT, pPix->drawable.height);
	OUT_REG(GLAMO_REG_2D_PAT_FG, fg);
	OUT_REG(GLAMO_REG_2D_COMMAND2, settings);
	END_DMA();

	GLAMO_LOG("leave");

	return TRUE;
}

static void
GLAMOSolid(int x1, int y1, int x2, int y2)
{
	GLAMO_LOG("enter");
	GLAMOScreenInfo *glamos = accel_glamos;
	RING_LOCALS;

	BEGIN_DMA(14);
	OUT_REG(GLAMO_REG_2D_DST_X, x1);
	OUT_REG(GLAMO_REG_2D_DST_Y, y1);
	OUT_REG(GLAMO_REG_2D_RECT_WIDTH, x2 - x1);
	OUT_REG(GLAMO_REG_2D_RECT_HEIGHT, y2 - y1);
	OUT_REG(GLAMO_REG_2D_COMMAND3, 0);
	OUT_REG(GLAMO_REG_2D_ID1, 0);
	OUT_REG(GLAMO_REG_2D_ID2, 0);
	END_DMA();
	GLAMO_LOG("leave");
}

static void
GLAMODoneSolid(void)
{
}

static Bool
GLAMOPrepareCopy(PixmapPtr pSrc, PixmapPtr pDst, int dx, int dy, int alu, Pixel pm)
{
	KdScreenPriv(pDst->drawable.pScreen);
	GLAMOScreenInfo(pScreenPriv);
	CARD32 src_offset, src_pitch;
	CARD32 dst_offset, dst_pitch;
	FbBits mask;
	RING_LOCALS;

	GLAMO_LOG("enter");

	if (pSrc->drawable.bitsPerPixel != 16 ||
	    pDst->drawable.bitsPerPixel != 16)
		GLAMO_FALLBACK(("Only 16bpp is supported"));

	mask = FbFullMask(16);
	if ((pm & mask) != mask)
		GLAMO_FALLBACK(("Can't do planemask 0x%08x", (unsigned int) pm));

	accel_glamos = glamos;

	src_offset = ((CARD8 *) pSrc->devPrivate.ptr -
			pScreenPriv->screen->memory_base);
	src_pitch = pSrc->devKind;

	dst_offset = ((CARD8 *) pDst->devPrivate.ptr -
			pScreenPriv->screen->memory_base);
	dst_pitch = pDst->devKind;

	settings = GLAMOBltRop[alu] << 8;

	BEGIN_DMA(16);

	OUT_REG(GLAMO_REG_2D_SRC_ADDRL, src_offset & 0xffff);
	OUT_REG(GLAMO_REG_2D_SRC_ADDRH, (src_offset >> 16) & 0x7f);
	OUT_REG(GLAMO_REG_2D_SRC_PITCH, src_pitch);

	OUT_REG(GLAMO_REG_2D_DST_ADDRL, dst_offset & 0xffff);
	OUT_REG(GLAMO_REG_2D_DST_ADDRH, (dst_offset >> 16) & 0x7f);
	OUT_REG(GLAMO_REG_2D_DST_PITCH, dst_pitch);
	OUT_REG(GLAMO_REG_2D_DST_HEIGHT, pDst->drawable.height);

	OUT_REG(GLAMO_REG_2D_COMMAND2, settings);

	END_DMA();

	GLAMO_LOG("leave");

	return TRUE;
}

static void
GLAMOCopy(int srcX, int srcY, int dstX, int dstY, int w, int h)
{
	GLAMOScreenInfo *glamos = accel_glamos;
	RING_LOCALS;

	BEGIN_DMA(18);
	OUT_REG(GLAMO_REG_2D_SRC_X, srcX);
	OUT_REG(GLAMO_REG_2D_SRC_Y, srcY);
	OUT_REG(GLAMO_REG_2D_DST_X, dstX);
	OUT_REG(GLAMO_REG_2D_DST_Y, dstY);
	OUT_REG(GLAMO_REG_2D_RECT_WIDTH, w);
	OUT_REG(GLAMO_REG_2D_RECT_HEIGHT, h);
	OUT_REG(GLAMO_REG_2D_COMMAND3, 0);
	OUT_REG(GLAMO_REG_2D_ID1, 0);
	OUT_REG(GLAMO_REG_2D_ID2, 0);
	END_DMA();
}

static void
GLAMODoneCopy(void)
{
	GLAMO_LOG("enter");
	GLAMO_LOG("leave");
}

static Bool
GLAMOUploadToScreen(PixmapPtr pDst, char *src, int src_pitch)
{
	int width, height, bpp, i;
	CARD8 *dst_offset;
	int dst_pitch;

        GLAMO_LOG("enter");
	dst_offset = (CARD8 *)pDst->devPrivate.ptr;
	dst_pitch = pDst->devKind;
	width = pDst->drawable.width;
	height = pDst->drawable.height;
	bpp = pDst->drawable.bitsPerPixel;
	bpp /= 8;

	for (i = 0; i < height; i++)
	{
		memcpy(dst_offset, src, width * bpp);

		dst_offset += dst_pitch;
		src += src_pitch;
	}

	return TRUE;
}

static void
GLAMOBlockHandler(pointer blockData, OSTimePtr timeout, pointer readmask)
{
	ScreenPtr pScreen = (ScreenPtr) blockData;
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

	/* When the server is going to sleep, make sure that all DMA data has
	 * been flushed.
	 */
	if (glamos->indirectBuffer)
		GLAMOFlushIndirect(glamos, 1);
}

static void
GLAMOWakeupHandler(pointer blockData, int result, pointer readmask)
{
}

Bool
GLAMODrawInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

	ErrorF("Screen: %d/%d depth/bpp\n", pScreenPriv->screen->fb[0].depth,
	    pScreenPriv->screen->fb[0].bitsPerPixel);

	RegisterBlockAndWakeupHandlers(GLAMOBlockHandler, GLAMOWakeupHandler,
	    pScreen);

	memset(&glamos->kaa, 0, sizeof(KaaScreenInfoRec));
	glamos->kaa.waitMarker = GLAMOWaitMarker;
	glamos->kaa.PrepareSolid = GLAMOPrepareSolid;
	glamos->kaa.Solid = GLAMOSolid;
	glamos->kaa.DoneSolid = GLAMODoneSolid;
	glamos->kaa.PrepareCopy = GLAMOPrepareCopy;
	glamos->kaa.Copy = GLAMOCopy;
	glamos->kaa.DoneCopy = GLAMODoneCopy;
	/* Other acceleration will be hooked in in DrawEnable depending on
	 * what type of DMA gets initialized.
	 */

	glamos->kaa.flags = KAA_OFFSCREEN_PIXMAPS;
	glamos->kaa.offsetAlign = 0;
	glamos->kaa.pitchAlign = 0;

	kaaInitTrapOffsets(8, sample_offsets_x, sample_offsets_y, 0.0, 0.0);
	sample_count = (1 << 8) - 1;

	if (!kaaDrawInit(pScreen, &glamos->kaa))
		return FALSE;

	return TRUE;
}

#if 0
static void
GLAMOScratchSave(ScreenPtr pScreen, KdOffscreenArea *area)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

	glamos->scratch_area = NULL;
}
#endif

void
GLAMODrawEnable(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

	GLAMODMASetup(pScreen);
	GLAMODrawSetup(pScreen);

	glamos->scratch_area = NULL;
	glamos->kaa.PrepareBlend = NULL;
	glamos->kaa.Blend = NULL;
	glamos->kaa.DoneBlend = NULL;
	glamos->kaa.CheckComposite = NULL;
	glamos->kaa.PrepareComposite = NULL;
	glamos->kaa.Composite = NULL;
	glamos->kaa.DoneComposite = NULL;
	glamos->kaa.UploadToScreen = NULL;
	glamos->kaa.UploadToScratch = NULL;

	glamos->kaa.UploadToScreen = GLAMOUploadToScreen;

	/* Reserve a scratch area.  It'll be used for storing glyph data during
	 * Composite operations, because glyphs aren't in real pixmaps and thus
	 * can't be migrated.
	 */
#if 0
	glamos->scratch_area = KdOffscreenAlloc(pScreen, 131072,
	    glamos->kaa.offsetAlign, TRUE, GLAMOScratchSave, glamos);
	if (glamos->scratch_area != NULL) {
		glamos->scratch_next = glamos->scratch_area->offset;
		glamos->kaa.UploadToScratch = GLAMOUploadToScratch;
	}
#endif

	kaaMarkSync(pScreen);
}

void
GLAMODrawDisable(ScreenPtr pScreen)
{
	kaaWaitSync(pScreen);
	GLAMODMATeardown(pScreen);
}

void
GLAMODrawFini(ScreenPtr pScreen)
{
	RemoveBlockAndWakeupHandlers(GLAMOBlockHandler, GLAMOWakeupHandler,
	    pScreen);

	kaaDrawFini(pScreen);
}

