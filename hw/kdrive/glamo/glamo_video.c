/*
 * Copyright © 2007 OpenMoko, Inc.
 *
 * This driver is based on Xati,
 * Copyright © 2004 Keith Packard
 * Copyright © 2005 Eric Anholt
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
#include "glamo.h"
#include "glamo_dma.h"
#include "glamo_draw.h"
#include "glamo-regs.h"
#include "kaa.h"

#include <X11/extensions/Xv.h>
#include "fourcc.h"

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

static Atom xvBrightness, xvSaturation;

#define IMAGE_MAX_WIDTH		2048
#define IMAGE_MAX_HEIGHT	2048

static void
GLAMOStopVideo(KdScreenInfo *screen, pointer data, Bool exit)
{
	ScreenPtr pScreen = screen->pScreen;
	GLAMOPortPrivPtr pPortPriv = (GLAMOPortPrivPtr)data;

	REGION_EMPTY(screen->pScreen, &pPortPriv->clip);

	if (pPortPriv->off_screen) {
		KdOffscreenFree (pScreen, pPortPriv->off_screen);
		pPortPriv->off_screen = 0;
	}
}

static int
GLAMOSetPortAttribute(KdScreenInfo *screen, Atom attribute, int value,
    pointer data)
{
	return BadMatch;
}

static int
GLAMOGetPortAttribute(KdScreenInfo *screen, Atom attribute, int *value,
    pointer data)
{
	return BadMatch;
}

static void
GLAMOQueryBestSize(KdScreenInfo *screen, Bool motion, short vid_w, short vid_h,
    short drw_w, short drw_h, unsigned int *p_w, unsigned int *p_h,
    pointer data)
{
	*p_w = drw_w;
	*p_h = drw_h;
}

/* GLAMOClipVideo -

   Takes the dst box in standard X BoxRec form (top and left
   edges inclusive, bottom and right exclusive).  The new dst
   box is returned.  The source boundaries are given (x1, y1
   inclusive, x2, y2 exclusive) and returned are the new source
   boundaries in 16.16 fixed point.
*/

static void
GLAMOClipVideo(BoxPtr dst, INT32 *x1, INT32 *x2, INT32 *y1, INT32 *y2,
    BoxPtr extents, INT32 width, INT32 height)
{
	INT32 vscale, hscale, delta;
	int diff;

	hscale = ((*x2 - *x1) << 16) / (dst->x2 - dst->x1);
	vscale = ((*y2 - *y1) << 16) / (dst->y2 - dst->y1);

	*x1 <<= 16; *x2 <<= 16;
	*y1 <<= 16; *y2 <<= 16;

	diff = extents->x1 - dst->x1;
	if (diff > 0) {
		dst->x1 = extents->x1;
		*x1 += diff * hscale;
	}
	diff = dst->x2 - extents->x2;
	if (diff > 0) {
		dst->x2 = extents->x2;
		*x2 -= diff * hscale;
	}
	diff = extents->y1 - dst->y1;
	if (diff > 0) {
		dst->y1 = extents->y1;
		*y1 += diff * vscale;
	}
	diff = dst->y2 - extents->y2;
	if (diff > 0) {
		dst->y2 = extents->y2;
		*y2 -= diff * vscale;
	}

	if (*x1 < 0) {
		diff =  (- *x1 + hscale - 1)/ hscale;
		dst->x1 += diff;
		*x1 += diff * hscale;
	}
	delta = *x2 - (width << 16);
	if (delta > 0) {
		diff = (delta + hscale - 1)/ hscale;
		dst->x2 -= diff;
		*x2 -= diff * hscale;
	}
	if (*y1 < 0) {
		diff =  (- *y1 + vscale - 1)/ vscale;
		dst->y1 += diff;
		*y1 += diff * vscale;
	}
	delta = *y2 - (height << 16);
	if (delta > 0) {
		diff = (delta + vscale - 1)/ vscale;
		dst->y2 -= diff;
		*y2 -= diff * vscale;
	}
}

static void
GlamoDisplayVideo(KdScreenInfo *screen, GLAMOPortPrivPtr pPortPriv)
{
	ScreenPtr pScreen = screen->pScreen;
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	PixmapPtr pPixmap = pPortPriv->pPixmap;
	CARD32 dst_offset, dst_pitch;
	int dstxoff, dstyoff, srcDatatype;
	RING_LOCALS;

	BoxPtr pBox = REGION_RECTS(&pPortPriv->clip);
	int nBox = REGION_NUM_RECTS(&pPortPriv->clip);

	dst_offset = ((CARD8 *)pPixmap->devPrivate.ptr -
	    pScreenPriv->screen->memory_base);
	dst_pitch = pPixmap->devKind;

#ifdef COMPOSITE
	dstxoff = -pPixmap->screen_x + pPixmap->drawable.x;
	dstyoff = -pPixmap->screen_y + pPixmap->drawable.y;
#else
	dstxoff = 0;
	dstyoff = 0;
#endif

	BEGIN_DMA(14);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_11, 0x0167);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_21, 0x01c5);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_32, 0x00b6);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_33, 0x0058);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_RG, 0xb3 << 8 | 0x89);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_B, 0xe2);

	/* TODO weight matrix */

	OUT_REG(GLAMO_REG_ISP_PORT2_EN, GLAMO_ISP_PORT2_EN_DECODE);

	END_DMA();

	if (pPortPriv->id == FOURCC_UYVY)
		srcDatatype = 3;
	else
		srcDatatype = 1;

	BEGIN_DMA(8);
#if 0
	OUT_REG(GLAMO_REG_ISP_EN3, GLAMO_ISP_EN3_SCALE_IMPROVE |
				   GLAMO_ISP_EN3_PLANE_MODE |
				   GLAMO_ISP_EN3_YUV_INPUT |
				   GLAMO_ISP_EN3_YUV420);
	OUT_REG(GLAMO_REG_ISP_PORT1_DEC_EN, GLAMO_ISP_PORT1_EN_OUTPUT);

	OUT_REG(GLAMO_REG_ISP_DEC_SCALEH, 1 << 11);
	OUT_REG(GLAMO_REG_ISP_DEC_SCALEV, 1 << 11);

	{
		struct {
			int src_block_y;
			int src_block_x;
			int src_block_h;
			int src_block_w;
			int jpeg_out_y;
			int jpeg_out_x;
			int fifo_full_cnt;
			int in_length;
			int fifo_data_cnt;
			int in_height;
		} onfly;

		onfly.src_block_y = 32;
		onfly.src_block_x = 32;
		onfly.src_block_h = 36;
		onfly.src_block_w = 35;
		onfly.jpeg_out_y = 32;
		onfly.jpeg_out_x = 32;
		onfly.fifo_full_cnt = 0;
		onfly.in_length = onfly.jpeg_out_x + 3;
		onfly.fifo_data_cnt = onfly.src_block_w * onfly.src_block_h / 2;
		onfly.in_height = onfly.jpeg_out_y + 2;

		OUT_REG(GLAMO_REG_ISP_ONFLY_MODE1, onfly.src_block_y << 10 | onfly.src_block_x << 2);
		OUT_REG(GLAMO_REG_ISP_ONFLY_MODE2, onfly.src_block_h << 8 | onfly.src_block_w);
		OUT_REG(GLAMO_REG_ISP_ONFLY_MODE3, onfly.jpeg_out_y << 8 | onfly.jpeg_out_x);
		OUT_REG(GLAMO_REG_ISP_ONFLY_MODE4, onfly.fifo_full_cnt << 8 | onfly.in_length);
		OUT_REG(GLAMO_REG_ISP_ONFLY_MODE5, onfly.fifo_data_cnt << 6 | onfly.in_height);
	}
#endif

	OUT_REG(GLAMO_REG_ISP_EN1,
		GLAMO_ISP_EN1_YUV420 |
		GLAMO_ISP_EN1_YUV_INPUT |
		GLAMO_ISP_EN1_YUV_PACK |
		((srcDatatype << 4) & 0x7));

	OUT_REG(GLAMO_REG_ISP_PORT1_CAP_EN,
		GLAMO_ISP_PORT1_EN_OUTPUT);

	OUT_REG(GLAMO_REG_ISP_CAP_PITCH, pPortPriv->src_pitch);
	OUT_REG(GLAMO_REG_ISP_PORT1_CAP_PITCH, dst_pitch);

	END_DMA();

	while (nBox--) {
		int srcX, srcY, dstX, dstY, srcw, srch, dstw, dsth;
		CARD32 srcO, dstO;

		dstX = pBox->x1 + dstxoff;
		dstY = pBox->y1 + dstyoff;
		dstw = pBox->x2 - pBox->x1;
		dsth = pBox->y2 - pBox->y1;
		srcX = (pBox->x1 - pPortPriv->dst_x1) *
		    pPortPriv->src_w / pPortPriv->dst_w;
		srcY = (pBox->y1 - pPortPriv->dst_y1) *
		    pPortPriv->src_h / pPortPriv->dst_h;
		srcw = pPortPriv->src_w - srcX; /* XXX */
		srch = pPortPriv->src_h - srcY; /* XXX */

		srcO = pPortPriv->src_offset + srcY * pPortPriv->src_pitch + srcX * 2;
		dstO = dst_offset + dstY * dst_pitch + dstX * 2;

		BEGIN_DMA(18);

		OUT_REG(GLAMO_REG_ISP_CAP_0_ADDRL, srcO & 0xffff);
		OUT_REG(GLAMO_REG_ISP_CAP_0_ADDRH, (srcO >> 16) & 0x7f);
		OUT_REG(GLAMO_REG_ISP_CAP_HEIGHT, srch);
		OUT_REG(GLAMO_REG_ISP_CAP_WIDTH, srcw);

		OUT_REG(GLAMO_REG_ISP_PORT1_CAP_0_ADDRL, dstO & 0xffff);
		OUT_REG(GLAMO_REG_ISP_PORT1_CAP_0_ADDRH, (dstO >> 16) & 0x7f);
		OUT_REG(GLAMO_REG_ISP_PORT1_CAP_WIDTH, dstw);
		OUT_REG(GLAMO_REG_ISP_PORT1_CAP_HEIGHT, dsth);

		/* fire */
		OUT_REG(GLAMO_REG_ISP_EN1, GLAMO_ISP_EN1_FIRE_ISP);
		OUT_REG(GLAMO_REG_ISP_EN1, 0);

		END_DMA();

		GLAMOWaitIdle(glamos);

		pBox++;
	}
#ifdef DAMAGEEXT
	/* XXX: Shouldn't this be in kxv.c instead? */
	DamageDamageRegion(pPortPriv->pDraw, &pPortPriv->clip);
#endif
	kaaMarkSync(pScreen);
}

static void
GLAMOVideoSave(ScreenPtr pScreen, KdOffscreenArea *area)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	GLAMOPortPrivPtr pPortPriv = glamos->pAdaptor->pPortPrivates[0].ptr;

	if (pPortPriv->off_screen == area)
		pPortPriv->off_screen = 0;
}

static int
GLAMOPutImage(KdScreenInfo *screen, DrawablePtr pDraw,
	       short src_x, short src_y,
	       short drw_x, short drw_y,
	       short src_w, short src_h,
	       short drw_w, short drw_h,
	       int id,
	       unsigned char *buf,
	       short width,
	       short height,
	       Bool sync,
	       RegionPtr clipBoxes,
	       pointer data)
{
	ScreenPtr pScreen = screen->pScreen;
	KdScreenPriv(pScreen);
	GLAMOCardInfo(pScreenPriv);
	GLAMOScreenInfo(pScreenPriv);
	GLAMOPortPrivPtr pPortPriv = (GLAMOPortPrivPtr)data;
	char *mmio = glamoc->reg_base;
	INT32 x1, x2, y1, y2;
	int randr = RR_Rotate_0 /* XXX */;
	int srcPitch, srcPitch2, dstPitch;
	int top, left, npixels, nlines, size;
	BoxRec dstBox;
	int dst_width = width, dst_height = height;
	int rot_x1, rot_y1, rot_x2, rot_y2;
	int dst_x1, dst_y1, dst_x2, dst_y2;
	int rot_src_w, rot_src_h, rot_drw_w, rot_drw_h;

	/* Clip */
	x1 = src_x;
	x2 = src_x + src_w;
	y1 = src_y;
	y2 = src_y + src_h;

	dstBox.x1 = drw_x;
	dstBox.x2 = drw_x + drw_w;
	dstBox.y1 = drw_y;
	dstBox.y2 = drw_y + drw_h;

	GLAMOClipVideo(&dstBox, &x1, &x2, &y1, &y2,
	    REGION_EXTENTS(pScreen, clipBoxes), width, height);

	src_w = (x2 - x1) >> 16;
	src_h = (y2 - y1) >> 16;
	drw_w = dstBox.x2 - dstBox.x1;
	drw_h = dstBox.y2 - dstBox.y1;

	if ((x1 >= x2) || (y1 >= y2))
		return Success;

	if (mmio == NULL)
		return BadAlloc;

	if (randr & (RR_Rotate_0|RR_Rotate_180)) {
		dst_width = width;
		dst_height = height;
		rot_src_w = src_w;
		rot_src_h = src_h;
		rot_drw_w = drw_w;
		rot_drw_h = drw_h;
	} else {
		dst_width = height;
		dst_height = width;
		rot_src_w = src_h;
		rot_src_h = src_w;
		rot_drw_w = drw_h;
		rot_drw_h = drw_w;
	}

	switch (randr & RR_Rotate_All) {
	case RR_Rotate_0:
	default:
		dst_x1 = dstBox.x1;
		dst_y1 = dstBox.y1;
		dst_x2 = dstBox.x2;
		dst_y2 = dstBox.y2;
		rot_x1 = x1;
		rot_y1 = y1;
		rot_x2 = x2;
		rot_y2 = y2;
		break;
	case RR_Rotate_90:
		dst_x1 = dstBox.y1;
		dst_y1 = screen->height - dstBox.x2;
		dst_x2 = dstBox.y2;
		dst_y2 = screen->height - dstBox.x1;
		rot_x1 = y1;
		rot_y1 = (src_w << 16) - x2;
		rot_x2 = y2;
		rot_y2 = (src_w << 16) - x1;
		break;
	case RR_Rotate_180:
		dst_x1 = screen->width - dstBox.x2;
		dst_y1 = screen->height - dstBox.y2;
		dst_x2 = screen->width - dstBox.x1;
		dst_y2 = screen->height - dstBox.y1;
		rot_x1 = (src_w << 16) - x2;
		rot_y1 = (src_h << 16) - y2;
		rot_x2 = (src_w << 16) - x1;
		rot_y2 = (src_h << 16) - y1;
		break;
	case RR_Rotate_270:
		dst_x1 = screen->width - dstBox.y2;
		dst_y1 = dstBox.x1;
		dst_x2 = screen->width - dstBox.y1;
		dst_y2 = dstBox.x2;
		rot_x1 = (src_h << 16) - y2;
		rot_y1 = x1;
		rot_x2 = (src_h << 16) - y1;
		rot_y2 = x2;
		break;
	}

	switch(id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		dstPitch = ((dst_width << 1) + 15) & ~15;
		srcPitch = (width + 3) & ~3;
		srcPitch2 = ((width >> 1) + 3) & ~3;
		size = dstPitch * dst_height;
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
	default:
		dstPitch = ((dst_width << 1) + 15) & ~15;
		srcPitch = (width << 1);
		srcPitch2 = 0;
		size = dstPitch * dst_height;
		break;
	}

	if (pPortPriv->off_screen != NULL && size != pPortPriv->size) {
		KdOffscreenFree(screen->pScreen, pPortPriv->off_screen);
		pPortPriv->off_screen = 0;
	}

	if (pPortPriv->off_screen == NULL) {
		pPortPriv->off_screen = KdOffscreenAlloc(screen->pScreen,
		    size * 2, 64, TRUE, GLAMOVideoSave, pPortPriv);
		if (pPortPriv->off_screen == NULL)
			return BadAlloc;
	}


	if (pDraw->type == DRAWABLE_WINDOW)
		pPortPriv->pPixmap =
		    (*pScreen->GetWindowPixmap)((WindowPtr)pDraw);
	else
		pPortPriv->pPixmap = (PixmapPtr)pDraw;

	/* Migrate the pixmap to offscreen if necessary. */
	if (!kaaPixmapIsOffscreen(pPortPriv->pPixmap))
		kaaMoveInPixmap(pPortPriv->pPixmap);

	if (!kaaPixmapIsOffscreen(pPortPriv->pPixmap)) {
		return BadAlloc;
	}

	pPortPriv->src_offset = pPortPriv->off_screen->offset;
	pPortPriv->src_addr = (CARD8 *)(pScreenPriv->screen->memory_base +
	    pPortPriv->src_offset);
	pPortPriv->src_pitch = dstPitch;
	pPortPriv->size = size;
	pPortPriv->pDraw = pDraw;

	/* copy data */
	top = rot_y1 >> 16;
	left = (rot_x1 >> 16) & ~1;
	npixels = ((((rot_x2 + 0xffff) >> 16) + 1) & ~1) - left;

	/* Since we're probably overwriting the area that might still be used
	 * for the last PutImage request, wait for idle.
	 */
	GLAMOWaitIdle(glamos);

	switch(id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		top &= ~1;
		nlines = ((((rot_y2 + 0xffff) >> 16) + 1) & ~1) - top;
		/* pack the source as YUY2 to vram */
		KdXVCopyPlanarData(screen, buf, pPortPriv->src_addr, randr,
		    srcPitch, srcPitch2, dstPitch, rot_src_w, rot_src_h,
		    height, top, left, nlines, npixels, id);
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
	default:
		nlines = ((rot_y2 + 0xffff) >> 16) - top;
		KdXVCopyPackedData(screen, buf, pPortPriv->src_addr, randr,
		    srcPitch, dstPitch, rot_src_w, rot_src_h, top, left,
		    nlines, npixels);
		break;
	}

	/* update cliplist */
	if (!REGION_EQUAL(screen->pScreen, &pPortPriv->clip, clipBoxes)) {
		REGION_COPY(screen->pScreen, &pPortPriv->clip, clipBoxes);
	}

	pPortPriv->id = id;
	pPortPriv->src_x1 = rot_x1;
	pPortPriv->src_y1 = rot_y1;
	pPortPriv->src_x2 = rot_x2;
	pPortPriv->src_y2 = rot_y2;
	pPortPriv->src_w = rot_src_w;
	pPortPriv->src_h = rot_src_h;
	pPortPriv->dst_x1 = dst_x1;
	pPortPriv->dst_y1 = dst_y1;
	pPortPriv->dst_x2 = dst_x2;
	pPortPriv->dst_y2 = dst_y2;
	pPortPriv->dst_w = rot_drw_w;
	pPortPriv->dst_h = rot_drw_h;

	GlamoDisplayVideo(screen, pPortPriv);

	return Success;
}

static int
GLAMOReputImage(KdScreenInfo *screen, DrawablePtr pDraw, short drw_x, short drw_y,
    RegionPtr clipBoxes, pointer data)
{
	ScreenPtr pScreen = screen->pScreen;
	GLAMOPortPrivPtr	pPortPriv = (GLAMOPortPrivPtr)data;
	BoxPtr pOldExtents = REGION_EXTENTS(screen->pScreen, &pPortPriv->clip);
	BoxPtr pNewExtents = REGION_EXTENTS(screen->pScreen, clipBoxes);

	if (pOldExtents->x1 != pNewExtents->x1 ||
	    pOldExtents->x2 != pNewExtents->x2 ||
	    pOldExtents->y1 != pNewExtents->y1 ||
	    pOldExtents->y2 != pNewExtents->y2)
		return BadMatch;

	if (pDraw->type == DRAWABLE_WINDOW)
		pPortPriv->pPixmap =
		    (*pScreen->GetWindowPixmap)((WindowPtr)pDraw);
	else
		pPortPriv->pPixmap = (PixmapPtr)pDraw;

	if (!kaaPixmapIsOffscreen(pPortPriv->pPixmap))
		kaaMoveInPixmap(pPortPriv->pPixmap);

	if (!kaaPixmapIsOffscreen(pPortPriv->pPixmap)) {
		ErrorF("err\n");
		return BadAlloc;
	}


	/* update cliplist */
	if (!REGION_EQUAL(screen->pScreen, &pPortPriv->clip, clipBoxes))
		REGION_COPY(screen->pScreen, &pPortPriv->clip, clipBoxes);

	/* XXX: What do the drw_x and drw_y here mean for us? */

	GlamoDisplayVideo(screen, pPortPriv);

	return Success;
}

static int
GLAMOQueryImageAttributes(KdScreenInfo *screen, int id, unsigned short *w,
    unsigned short *h, int *pitches, int *offsets)
{
	int size, tmp;

	if (*w > IMAGE_MAX_WIDTH)
		*w = IMAGE_MAX_WIDTH;
	if (*h > IMAGE_MAX_HEIGHT)
		*h = IMAGE_MAX_HEIGHT;

	*w = (*w + 1) & ~1;
	if (offsets)
		offsets[0] = 0;

	switch (id)
	{
	case FOURCC_YV12:
	case FOURCC_I420:
		*h = (*h + 1) & ~1;
		size = (*w + 3) & ~3;
		if (pitches)
			pitches[0] = size;
		size *= *h;
		if (offsets)
			offsets[1] = size;
		tmp = ((*w >> 1) + 3) & ~3;
		if (pitches)
			pitches[1] = pitches[2] = tmp;
		tmp *= (*h >> 1);
		size += tmp;
		if (offsets)
			offsets[2] = size;
		size += tmp;
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
	default:
		size = *w << 1;
		if (pitches)
			pitches[0] = size;
		size *= *h;
		break;
	}

	return size;
}


/* client libraries expect an encoding */
static KdVideoEncodingRec DummyEncoding[1] =
{
	{
		0,
		"XV_IMAGE",
		IMAGE_MAX_WIDTH, IMAGE_MAX_HEIGHT,
		{1, 1}
	}
};

#define NUM_FORMATS 1

static KdVideoFormatRec Formats[NUM_FORMATS] =
{
	{16, TrueColor}
};

#define NUM_ATTRIBUTES 0

static KdAttributeRec Attributes[NUM_ATTRIBUTES] =
{
};

#define NUM_IMAGES 4

static KdImageRec Images[NUM_IMAGES] =
{
	XVIMAGE_YUY2,
	XVIMAGE_YV12,
	XVIMAGE_I420,
	XVIMAGE_UYVY
};

static KdVideoAdaptorPtr
GLAMOSetupImageVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	KdVideoAdaptorPtr adapt;
	GLAMOPortPrivPtr pPortPriv;
	int i;

	glamos->num_texture_ports = 16;

	adapt = xcalloc(1, sizeof(KdVideoAdaptorRec) + glamos->num_texture_ports *
	    (sizeof(GLAMOPortPrivRec) + sizeof(DevUnion)));
	if (adapt == NULL)
		return NULL;

	adapt->type = XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags = VIDEO_CLIP_TO_VIEWPORT;
	adapt->name = "GLAMO Texture Video";
	adapt->nEncodings = 1;
	adapt->pEncodings = DummyEncoding;
	adapt->nFormats = NUM_FORMATS;
	adapt->pFormats = Formats;
	adapt->nPorts = glamos->num_texture_ports;
	adapt->pPortPrivates = (DevUnion*)(&adapt[1]);

	pPortPriv =
	    (GLAMOPortPrivPtr)(&adapt->pPortPrivates[glamos->num_texture_ports]);

	for (i = 0; i < glamos->num_texture_ports; i++)
		adapt->pPortPrivates[i].ptr = &pPortPriv[i];

	adapt->nAttributes = NUM_ATTRIBUTES;
	adapt->pAttributes = Attributes;
	adapt->pImages = Images;
	adapt->nImages = NUM_IMAGES;
	adapt->PutVideo = NULL;
	adapt->PutStill = NULL;
	adapt->GetVideo = NULL;
	adapt->GetStill = NULL;
	adapt->StopVideo = GLAMOStopVideo;
	adapt->SetPortAttribute = GLAMOSetPortAttribute;
	adapt->GetPortAttribute = GLAMOGetPortAttribute;
	adapt->QueryBestSize = GLAMOQueryBestSize;
	adapt->PutImage = GLAMOPutImage;
	adapt->ReputImage = GLAMOReputImage;
	adapt->QueryImageAttributes = GLAMOQueryImageAttributes;

	/* gotta uninit this someplace */
	REGION_INIT(pScreen, &pPortPriv->clip, NullBox, 0);

	glamos->pAdaptor = adapt;

	xvBrightness = MAKE_ATOM("XV_BRIGHTNESS");
	xvSaturation = MAKE_ATOM("XV_SATURGLAMOON");

	return adapt;
}

static void GLAMOPowerUp(ScreenPtr pScreen)
{
	GLAMOEngineEnable(pScreen, GLAMO_ENGINE_ISP);
	GLAMOEngineReset(pScreen, GLAMO_ENGINE_ISP);

	/* HW_DEBUG_0?? */
	//MMIOSetBitMask(mmio, REG_ISP(0x102), 0x0020, 0);
}

static void GLAMOPowerDown(ScreenPtr pScreen)
{
	GLAMOEngineReset(pScreen, GLAMO_ENGINE_ISP);

	/* ... and stop the clock */
}

Bool GLAMOInitVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	GLAMOCardInfo(pScreenPriv);
	KdScreenInfo *screen = pScreenPriv->screen;
	KdVideoAdaptorPtr *adaptors, *newAdaptors = NULL;
	KdVideoAdaptorPtr newAdaptor = NULL;
	int num_adaptors;

	glamos->pAdaptor = NULL;

	if (glamoc->reg_base == NULL)
		return FALSE;

	num_adaptors = KdXVListGenericAdaptors(screen, &adaptors);

	newAdaptor = GLAMOSetupImageVideo(pScreen);

	if (newAdaptor)  {
		GLAMOPowerUp(pScreen);

		if (!num_adaptors) {
			num_adaptors = 1;
			adaptors = &newAdaptor;
		} else {
			newAdaptors = xalloc((num_adaptors + 1) *
			    sizeof(KdVideoAdaptorPtr *));
			if (newAdaptors) {
				memcpy(newAdaptors, adaptors, num_adaptors *
				    sizeof(KdVideoAdaptorPtr));
				newAdaptors[num_adaptors] = newAdaptor;
				adaptors = newAdaptors;
				num_adaptors++;
			}
		}
	}

	if (num_adaptors)
		KdXVScreenInit(pScreen, adaptors, num_adaptors);

	if (newAdaptors)
		xfree(newAdaptors);

	return TRUE;
}

void
GLAMOFiniVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	KdVideoAdaptorPtr adapt = glamos->pAdaptor;
	GLAMOPortPrivPtr pPortPriv;
	int i;

	if (!adapt)
		return;

	GLAMOPowerDown(pScreen);

	for (i = 0; i < glamos->num_texture_ports; i++) {
		pPortPriv = (GLAMOPortPrivPtr)(&adapt->pPortPrivates[i].ptr);
		REGION_UNINIT(pScreen, &pPortPriv->clip);
	}
	xfree(adapt);
	glamos->pAdaptor = NULL;
}
