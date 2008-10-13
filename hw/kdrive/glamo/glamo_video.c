/*
 * Copyright � 2007 OpenMoko, Inc.
 *
 * This driver is based on Xati,
 * Copyright � 2004 Keith Packard
 * Copyright � 2005 Eric Anholt
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

#define SYS_PITCH_ALIGN(w) (((w) + 3) & ~3)
#define VID_PITCH_ALIGN(w) (((w) + 1) & ~1)

static Atom xvBrightness, xvSaturation;

#define IMAGE_MAX_WIDTH		2048
#define IMAGE_MAX_HEIGHT	2048

static void
GLAMOStopVideo(KdScreenInfo *screen, pointer data, Bool exit)
{
	int i;

	ScreenPtr pScreen = screen->pScreen;
	GLAMOPortPrivPtr pPortPriv = (GLAMOPortPrivPtr)data;

	REGION_EMPTY(screen->pScreen, &pPortPriv->clip);

	for (i = 0; i < GLAMO_VIDEO_NUM_BUFS; i++)
		if (pPortPriv->off_screen[i]) {
			KdOffscreenFree (pScreen, pPortPriv->off_screen[i]);
			pPortPriv->off_screen[i] = 0;
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
	CARD32 dst_offset, dst_pitch, *offsets;
	int dstxoff, dstyoff;
	RING_LOCALS;

	BoxPtr pBox = REGION_RECTS(&pPortPriv->clip);
	int nBox = REGION_NUM_RECTS(&pPortPriv->clip);
	int en3;

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

	en3 = GLAMO_ISP_EN3_PLANE_MODE |
	      GLAMO_ISP_EN3_YUV_INPUT |
	      GLAMO_ISP_EN3_YUV420;
	en3 |= GLAMO_ISP_EN3_SCALE_IMPROVE;

	BEGIN_CMDQ(8);

	OUT_REG(GLAMO_REG_ISP_EN3, en3);
	OUT_REG(GLAMO_REG_ISP_DEC_PITCH_Y, pPortPriv->src_pitch1 & 0x1fff);
	OUT_REG(GLAMO_REG_ISP_DEC_PITCH_UV, pPortPriv->src_pitch2 & 0x1fff);
	OUT_REG(GLAMO_REG_ISP_PORT1_DEC_PITCH, dst_pitch & 0x1fff);

	END_CMDQ();

	offsets = pPortPriv->src_offsets[pPortPriv->idx];

	while (nBox--) {
		int srcX, srcY, dstX, dstY, srcw, srch, dstw, dsth, scale;
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

		GLAMOEngineWait(pScreen, GLAMO_ENGINE_ISP);

		BEGIN_CMDQ(16);
		srcO = offsets[0] + srcY * pPortPriv->src_pitch1 + srcX;
		OUT_REG(GLAMO_REG_ISP_DEC_Y_ADDRL, srcO & 0xffff);
		OUT_REG(GLAMO_REG_ISP_DEC_Y_ADDRH, (srcO >> 16) & 0x7f);

		srcO = offsets[1] + srcY * pPortPriv->src_pitch2 + srcX;
		OUT_REG(GLAMO_REG_ISP_DEC_U_ADDRL, srcO & 0xffff);
		OUT_REG(GLAMO_REG_ISP_DEC_U_ADDRH, (srcO >> 16) & 0x7f);

		srcO = offsets[2] + srcY * pPortPriv->src_pitch2 + srcX;
		OUT_REG(GLAMO_REG_ISP_DEC_V_ADDRL, srcO & 0xffff);
		OUT_REG(GLAMO_REG_ISP_DEC_V_ADDRH, (srcO >> 16) & 0x7f);

		OUT_REG(GLAMO_REG_ISP_DEC_HEIGHT, srch & 0x1fff);
		OUT_REG(GLAMO_REG_ISP_DEC_WIDTH, srcw & 0x1fff);
		END_CMDQ();

		BEGIN_CMDQ(16);
		dstO = dst_offset + dstY * dst_pitch + dstX * 2;
		OUT_REG(GLAMO_REG_ISP_PORT1_DEC_0_ADDRL, dstO & 0xffff);
		OUT_REG(GLAMO_REG_ISP_PORT1_DEC_0_ADDRH, (dstO >> 16) & 0x7f);

		OUT_REG(GLAMO_REG_ISP_PORT1_DEC_WIDTH, dstw & 0x1fff);
		OUT_REG(GLAMO_REG_ISP_PORT1_DEC_HEIGHT, dsth & 0x1fff);

		scale = (srcw << 11) / dstw;
		OUT_REG(GLAMO_REG_ISP_DEC_SCALEH, scale);

		scale = (srch << 11) / dsth;
		OUT_REG(GLAMO_REG_ISP_DEC_SCALEV, scale);

		OUT_REG(GLAMO_REG_ISP_EN1, GLAMO_ISP_EN1_FIRE_ISP);
		OUT_REG(GLAMO_REG_ISP_EN1, 0);

		END_CMDQ();

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
	int i;

	for (i = 0; i < GLAMO_VIDEO_NUM_BUFS; i++)
		if (pPortPriv->off_screen[i] == area)
		{
			pPortPriv->off_screen[i] = 0;

			break;
		}
}

static int
GLAMOUploadImage(KdScreenInfo *screen, DrawablePtr pDraw,
		 GLAMOPortPrivPtr pPortPriv,
		 short src_x, short src_y,
		 short src_w, short src_h,
		 int id,
		 int randr,
		 unsigned char *buf,
		 short width, short height)
{
	CARD32 *offsets;
	int srcPitch1, srcPitch2, dstPitch1, dstPitch2;
	int src_x2, src_y2, size;
	int idx;

	src_x2 = src_x + src_w;
	src_y2 = src_y + src_h;

	src_x &= ~1;
	src_y &= ~1;
	src_w  = (src_x2 - src_x + 1) & ~1;
	src_h  = (src_y2 - src_y + 1) & ~1;

	switch (id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		srcPitch1 = SYS_PITCH_ALIGN(width);
		srcPitch2 = SYS_PITCH_ALIGN(width / 2);
		dstPitch1 = VID_PITCH_ALIGN(src_w);
		dstPitch2 = VID_PITCH_ALIGN(src_w / 2);
		size = (dstPitch1 + dstPitch2) * src_h;
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
	default:
		srcPitch1 = width << 1;
		srcPitch2 = 0;
		dstPitch1 = src_w << 1;
		dstPitch2 = 0;
		size = dstPitch1 * src_h;
		break;
	}

	idx = pPortPriv->idx;
	//ErrorF("uploading to buffer %d\n", idx);

	if (!pPortPriv->off_screen[idx] ||
	    size > pPortPriv->off_screen[idx]->size) {
		if (pPortPriv->off_screen[idx])
			KdOffscreenFree(screen->pScreen,
					pPortPriv->off_screen[idx]);

		pPortPriv->off_screen[idx] =
			KdOffscreenAlloc(screen->pScreen,
					 size, VID_PITCH_ALIGN(1), TRUE,
					 GLAMOVideoSave, pPortPriv);
		if (!pPortPriv->off_screen[idx])
			return BadAlloc;
	}

	if (pDraw->type == DRAWABLE_WINDOW)
		pPortPriv->pPixmap =
		    (*screen->pScreen->GetWindowPixmap)((WindowPtr)pDraw);
	else
		pPortPriv->pPixmap = (PixmapPtr)pDraw;

	/* Migrate the pixmap to offscreen if necessary. */
	if (!kaaPixmapIsOffscreen(pPortPriv->pPixmap))
		kaaMoveInPixmap(pPortPriv->pPixmap);

	if (!kaaPixmapIsOffscreen(pPortPriv->pPixmap))
		return BadAlloc;

	pPortPriv->pDraw = pDraw;

	offsets = pPortPriv->src_offsets[idx];
	offsets[0] = pPortPriv->off_screen[idx]->offset;
	offsets[1] = offsets[0] + dstPitch1 * src_h;
	offsets[2] = offsets[1] + dstPitch2 * src_h / 2;
	pPortPriv->src_pitch1 = dstPitch1;
	pPortPriv->src_pitch2 = dstPitch2;
	pPortPriv->size[idx] = size;

	/* copy data */
	switch (id) {
	case FOURCC_YV12:
	case FOURCC_I420:
		{
			CARD8 *src1, *src2, *src3;
			CARD8 *dst1, *dst2, *dst3;
			int i;

			src1 = buf + (src_y * srcPitch1) + src_x;
			src2 = buf + (height * srcPitch1) +
				(src_y * srcPitch2) + src_x / 2;
			src3 = src2 + height * srcPitch2 / 2;

			dst1 = (CARD8 *) (screen->memory_base + offsets[0]);

			if (id == FOURCC_I420) {
				dst2 = (CARD8 *) (screen->memory_base +
						  offsets[1]);
				dst3 = (CARD8 *) (screen->memory_base +
						  offsets[2]);
			} else {
				dst2 = (CARD8 *) (screen->memory_base +
						  offsets[2]);
				dst3 = (CARD8 *) (screen->memory_base +
						  offsets[1]);
			}

			for (i = 0; i < src_h; i++) {
				memcpy(dst1, src1, src_w);
				src1 += srcPitch1;
				dst1 += dstPitch1;

				if (!(i & 1)) {
					memcpy(dst2, src2, src_w / 2);
					memcpy(dst3, src3, src_w / 2);

					src2 += srcPitch2;
					dst2 += dstPitch2;
					src3 += srcPitch2;
					dst3 += dstPitch2;
				}
			}
		}
		break;
	case FOURCC_UYVY:
	case FOURCC_YUY2:
	default:
		KdXVCopyPackedData(screen, buf,
				(CARD8 *)(screen->memory_base + offsets[0]),
				randr, srcPitch1, dstPitch1,
				src_w, src_h, src_y, src_x, src_h, src_w);
		break;
	}

	return Success;
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
	GLAMOPortPrivPtr pPortPriv = (GLAMOPortPrivPtr)data;
	char *mmio = glamoc->reg_base;
	INT32 x1, x2, y1, y2;
	int randr = RR_Rotate_0 /* XXX */;
	int top, left, npixels, nlines;
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

	top = rot_y1 >> 16;
	left = rot_x1 >> 16;
	npixels = ((rot_x2 + 0xffff) >> 16) - left;
	nlines  = ((rot_y2 + 0xffff) >> 16) - top;

	/*
	 * We kaaWaitSync below.  This guarantees only one buffer
	 * is locked by ISP.  Thus, if ISP is busy, the buffer
	 * knext is free.
	 *
	 * This is a simple scheme.  Only dual buffer benefits.
	 */
	if (GLAMOEngineBusy(pScreen, GLAMO_ENGINE_ISP))
		pPortPriv->idx = (pPortPriv->idx + 1) % GLAMO_VIDEO_NUM_BUFS;

	if (GLAMOUploadImage(screen, pDraw, pPortPriv,
			     left, top, npixels, nlines,
			     id, randr, buf, width, height))
		return BadAlloc;

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

	kaaWaitSync(pScreen);
	GlamoDisplayVideo(screen, pPortPriv);

	return Success;
}

static int
GLAMOReputImage(KdScreenInfo *screen, DrawablePtr pDraw,
		short drw_x, short drw_y, RegionPtr clipBoxes, pointer data)
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

	kaaWaitSync(pScreen);
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

		tmp = SYS_PITCH_ALIGN(*w);
		size = tmp * *h;

		if (pitches)
			pitches[0] = tmp;
		if (offsets)
			offsets[1] = size;

		tmp = SYS_PITCH_ALIGN(*w / 2);
		size += tmp * *h / 2;
		if (pitches)
			pitches[1] = pitches[2] = tmp;
		if (offsets)
			offsets[2] = size;

		size += tmp * *h / 2;
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

#ifdef PACKED_IMAGE
#define NUM_IMAGES 4

static KdImageRec Images[NUM_IMAGES] =
{
	XVIMAGE_YUY2,
	XVIMAGE_YV12,
	XVIMAGE_I420,
	XVIMAGE_UYVY
};
#else
#define NUM_IMAGES 2

static KdImageRec Images[NUM_IMAGES] =
{
	XVIMAGE_YV12,
	XVIMAGE_I420,
};
#endif /* PACKED_IMAGE */

static KdVideoAdaptorPtr
GLAMOSetupImageVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	KdVideoAdaptorPtr adapt;
	GLAMOPortPrivPtr pPortPriv;
	int i;

	glamos->num_texture_ports = 1;

	adapt = xcalloc(1, sizeof(KdVideoAdaptorRec) +
			   glamos->num_texture_ports *
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

	pPortPriv = (GLAMOPortPrivPtr)
		(&adapt->pPortPrivates[glamos->num_texture_ports]);

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

	for (i = 0; i < glamos->num_texture_ports; i++)
		REGION_INIT(pScreen, &pPortPriv[i].clip, NullBox, 0);

	glamos->pAdaptor = adapt;

	xvBrightness = MAKE_ATOM("XV_BRIGHTNESS");
	xvSaturation = MAKE_ATOM("XV_SATURGLAMOON");

	return adapt;
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

	for (i = 0; i < glamos->num_texture_ports; i++) {
		pPortPriv = (GLAMOPortPrivPtr)(adapt->pPortPrivates[i].ptr);
		REGION_UNINIT(pScreen, &pPortPriv->clip);
	}
	xfree(adapt);
	glamos->pAdaptor = NULL;
}

static void GLAMOSetOnFlyRegs(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	struct {
		int src_block_x;
		int src_block_y;
		int src_block_w;
		int src_block_h;
		int jpeg_out_y;
		int jpeg_out_x;
		int fifo_full_cnt;
		int in_length;
		int fifo_data_cnt;
		int in_height;
	} onfly;
	RING_LOCALS;

	onfly.src_block_y = 32;
	onfly.src_block_x = 32;
	onfly.src_block_w = 36;
	onfly.src_block_h = 35;
	onfly.jpeg_out_y = 32;
	onfly.jpeg_out_x = 32;
	onfly.fifo_full_cnt = onfly.src_block_w * 2 + 2;
	onfly.in_length = onfly.jpeg_out_x + 3;
	onfly.fifo_data_cnt = onfly.src_block_w * onfly.src_block_h / 2;
	onfly.in_height = onfly.jpeg_out_y + 2;

	BEGIN_CMDQ(10);
	OUT_REG(GLAMO_REG_ISP_ONFLY_MODE1,
		onfly.src_block_y << 10 | onfly.src_block_x << 2);
	OUT_REG(GLAMO_REG_ISP_ONFLY_MODE2,
		onfly.src_block_h << 8 | onfly.src_block_w);
	OUT_REG(GLAMO_REG_ISP_ONFLY_MODE3,
		onfly.jpeg_out_y << 8 | onfly.jpeg_out_x);
	OUT_REG(GLAMO_REG_ISP_ONFLY_MODE4,
		onfly.fifo_full_cnt << 8 | onfly.in_length);
	OUT_REG(GLAMO_REG_ISP_ONFLY_MODE5,
		onfly.fifo_data_cnt << 6 | onfly.in_height);
	END_CMDQ();
}

static void GLAMOSetWeightRegs(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	int left = 1 << 14;
	RING_LOCALS;

	/* nearest */

	BEGIN_CMDQ(12);
	OUT_BURST(GLAMO_REG_ISP_DEC_SCALEH_MATRIX, 10);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX +  0, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX +  2, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX +  4, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX +  6, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX +  8, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX + 10, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX + 12, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX + 14, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX + 16, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEH_MATRIX + 18, 0);
	END_CMDQ();

	BEGIN_CMDQ(12);
	OUT_BURST(GLAMO_REG_ISP_DEC_SCALEV_MATRIX, 10);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX +  0, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX +  2, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX +  4, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX +  6, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX +  8, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX + 10, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX + 12, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX + 14, 0);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX + 16, left);
	OUT_BURST_REG(GLAMO_REG_ISP_DEC_SCALEV_MATRIX + 18, 0);
	END_CMDQ();
}

static void GLAMOInitISP(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	RING_LOCALS;

	BEGIN_CMDQ(16);

	/*
	 * In 8.8 fixed point,
	 *
	 *  R = Y + 1.402 (Cr-128)
	 *    = Y + 0x0167 Cr - 0xb3
	 *
	 *  G = Y - 0.34414 (Cb-128) - 0.71414 (Cr-128)
	 *    = Y - 0x0058 Cb - 0x00b6 Cr + 0x89
	 *
	 *  B = Y + 1.772 (Cb-128)
	 *    = Y + 0x01c5 Cb - 0xe2
	 */

	OUT_REG(GLAMO_REG_ISP_YUV2RGB_11, 0x0167);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_21, 0x01c5);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_32, 0x00b6);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_33, 0x0058);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_RG, 0xb3 << 8 | 0x89);
	OUT_REG(GLAMO_REG_ISP_YUV2RGB_B, 0xe2);

	OUT_REG(GLAMO_REG_ISP_PORT1_DEC_EN, GLAMO_ISP_PORT1_EN_OUTPUT);
	OUT_REG(GLAMO_REG_ISP_PORT2_EN, GLAMO_ISP_PORT2_EN_DECODE);

	END_CMDQ();

	GLAMOSetOnFlyRegs(pScreen);
	GLAMOSetWeightRegs(pScreen);
}

Bool
GLAMOVideoSetup(ScreenPtr pScreen)
{
	GLAMOEngineEnable(pScreen, GLAMO_ENGINE_ISP);
	GLAMOEngineReset(pScreen, GLAMO_ENGINE_ISP);

	GLAMOInitISP(pScreen);

	return TRUE;
}

void
GLAMOVideoTeardown(ScreenPtr pScreen)
{
	GLAMOEngineReset(pScreen, GLAMO_ENGINE_ISP);
	GLAMOEngineDisable(pScreen, GLAMO_ENGINE_ISP);
}

