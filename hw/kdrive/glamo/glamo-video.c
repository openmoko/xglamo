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
 * authors:
 *   Dodji SEKETELI <dodji@openedhand.com>
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#ifdef XV

#define LOG_XVIDEO 0

#include "glamo.h"
#include "glamo-cmdq.h"
#include "glamo-draw.h"
#include "glamo-regs.h"
#include "glamo-log.h"
#include "kaa.h"

#include <X11/extensions/Xv.h>
#include "fourcc.h"

#define MAKE_ATOM(a) MakeAtom(a, sizeof(a) - 1, TRUE)

#define SYS_PITCH_ALIGN(w) (((w) + 3) & ~3)
#define VID_PITCH_ALIGN(w) (((w) + 1) & ~1)

static Atom xvColorKey;

#define IMAGE_MAX_WIDTH		640
#define IMAGE_MAX_HEIGHT	480

static void
GLAMOStopVideo(KdScreenInfo *screen, pointer data, Bool exit)
{
	/*
	int i;

	ScreenPtr pScreen = screen->pScreen;
	GLAMOPortPrivPtr pPortPriv = (GLAMOPortPrivPtr)data;

	REGION_EMPTY(screen->pScreen, &pPortPriv->clip);

	for (i = 0; i < GLAMO_VIDEO_NUM_BUFS; i++)
		if (pPortPriv->off_screen[i]) {
			KdOffscreenFree (pScreen, pPortPriv->off_screen[i]);
			pPortPriv->off_screen[i] = 0;
		}
	*/
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
GLAMOQueryBestSize(KdScreenInfo *screen,
		   Bool motion,
		   short vid_w,
		   short vid_h,
		   short drw_w,
		   short drw_h,
		   unsigned int *p_w,
		   unsigned int *p_h,
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
GLAMOVideoSave(ScreenPtr pScreen, KdOffscreenArea *area)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	GLAMOPortPrivPtr pPortPriv = glamos->pAdaptor->pPortPrivates[0].ptr;
	int i;

	GLAMO_LOG("mark\n");
	/*
	for (i = 0; i < GLAMO_VIDEO_NUM_BUFS; i++)
		if (pPortPriv->off_screen[i] == area)
		{
			pPortPriv->off_screen[i] = 0;

			break;
		}
		*/
}

static Bool
GetYUVFrameByteSize (int fourcc_code,
		     unsigned short width,
		     unsigned short height,
		     unsigned int *size)
{
	if (!size)
		return FALSE;
	switch (fourcc_code) {
		case FOURCC_YV12:
		case FOURCC_I420:
			*size = width*height * 3 / 2 ;
			break;
		default:
			return FALSE;
	}
	return TRUE;

}

static Bool
GetYUVFrameAddresses (unsigned int frame_addr,
		      int fourcc_code,
		      unsigned short frame_width,
		      unsigned short frame_height,
		      unsigned short x,
		      unsigned short y,
		      unsigned int *y_addr,
		      unsigned int *u_addr,
		      unsigned int *v_addr)
{
	Bool is_ok = FALSE;

	if (!u_addr || !v_addr) {
		GLAMO_LOG("failed sanity check\n");
		goto out;
	}

#if LOG_XVIDEO
	GLAMO_LOG("enter: frame(%dx%d), frame_addr:%#x\n"
		  "position:(%d,%d)\n",
		  frame_width, frame_height,
		  frame_addr, x, y);
#endif

	switch (fourcc_code) {
		case FOURCC_YV12:
			*y_addr = frame_addr + x +  y * frame_width;
			*v_addr = frame_addr + frame_width*frame_height+
			          x/2 + (y/2) *(frame_width/2);
			*u_addr = frame_addr + frame_width*frame_height +
			          frame_width*frame_height/4 +
				  x/2 + (y/2)*(frame_width/2);
			is_ok = TRUE;
			break;
		case FOURCC_I420:
			*y_addr = frame_addr + x +  y*frame_width;
			*u_addr = frame_addr + frame_width*frame_height+
			          x/2 + (y/2)*frame_width/2;
			*v_addr = frame_addr + frame_width*frame_height +
			          frame_width*frame_height/4 +
				  x/2 + (y/2)*frame_height/2;
			is_ok = TRUE;
			break;
		default:
			is_ok = FALSE;
			break;
	}
#if LOG_XVIDEO
	GLAMO_LOG("y_addr:%#x, u_addr:%#x, v_addr:%#x\n",
		  *y_addr, *u_addr, *v_addr);
#endif
out:
#if LOG_XVIDEO
	GLAMO_LOG("leave. is_ok:%d\n",
		  is_ok);
#endif
	return is_ok;
}

/**
 * copy a portion of the YUV frame "src_frame" to a destination in memory.
 * The portion to copy is a rectangle located at (src_x,src_y),
 * of size (rect_width,rect_height).
 *
 * @src_frame pointer to the start of the YUV frame to consider
 * @frame_width width of the YUV frame
 * @frame_height height of the YUV frame
 * @src_x
 * @src_y
 * @rect_width
 * @rect_height
 */
static Bool
CopyYUVPlanarFrameRect (const char *src_frame,
			int fourcc_code,
			unsigned short frame_width,
			unsigned short frame_height,
			unsigned short src_x,
			unsigned short src_y,
			unsigned short rect_width,
			unsigned short rect_height,
			char *destination)
{
	char *y_copy_src, *u_copy_src, *v_copy_src,
		*y_copy_dst, *u_copy_dst, *v_copy_dst;
	unsigned line;
	Bool is_ok = FALSE;

#if LOG_XVIDEO
	GLAMO_LOG("enter. src_frame:%#x, code:%#x\n"
		  "frame(%d,%d)-(%dx%d), crop(%dx%d)\n"
		  "dest:%#x",
		  (unsigned)src_frame, (unsigned)fourcc_code,
		  src_x, src_y, frame_width, frame_height,
		  rect_width, rect_height, (unsigned)destination);
#endif

	switch (fourcc_code) {
		case FOURCC_YV12:
		case FOURCC_I420:
			/*planar yuv formats of the 4:2:0 family*/
			y_copy_src = (char*) src_frame + src_x +
					frame_width*src_y;
			u_copy_src = (char*) src_frame +
					frame_width*frame_height +
					src_x/2 + (frame_width/2)*(src_y/2);
			v_copy_src = (char*) src_frame +
				frame_width*frame_height*5/4 + src_x/2 +
				(frame_width/2)*src_y/2;
			y_copy_dst = destination;
			u_copy_dst = destination + rect_width*rect_height;
			v_copy_dst = destination + rect_width*rect_height*5/4;
#if LOG_XVIDEO
			GLAMO_LOG("y_copy_src:%#x, "
				  "u_copy_src:%#x, "
				  "v_copy_src:%#x\n"
				  "y_copy_dst:%#x, "
				  "u_copy_dst:%#x, "
				  "v_copy_dst:%#x\n",
				  (unsigned)y_copy_src,
				  (unsigned)u_copy_src,
				  (unsigned)v_copy_src,
				  (unsigned)y_copy_dst,
				  (unsigned)u_copy_dst,
				  (unsigned)v_copy_dst);
#endif
			for (line = 0; line < rect_height; line++) {
#if LOG_XVIDEO
				GLAMO_LOG("============\n"
					  "line:%d\n"
					  "============\n",
					  line);
				GLAMO_LOG("y_copy_src:%#x, "
					  "y_copy_dst:%#x, \n",
					  (unsigned)y_copy_src,
					  (unsigned)y_copy_dst);
#endif

				memcpy(y_copy_dst,
				       y_copy_src,
				       rect_width);
				y_copy_src += frame_width;
				y_copy_dst += rect_width;

				/*
				 * one line out of two has chrominance (u,v)
				 * sampling.
				 */
				if (!(line&1)) {
#if LOG_XVIDEO
					GLAMO_LOG("u_copy_src:%#x, "
						  "u_copy_dst:%#x\n",
						  (unsigned)u_copy_src,
						  (unsigned)u_copy_dst);
#endif
					memcpy(u_copy_dst,
					       u_copy_src,
					       rect_width/2);
#if LOG_XVIDEO
					GLAMO_LOG("v_copy_src:%#x, "
						  "v_copy_dst:%#x\n",
						  (unsigned)v_copy_src,
						  (unsigned)v_copy_dst);
#endif
					memcpy(v_copy_dst,
					       v_copy_src,
					       rect_width/2);
					u_copy_src += frame_width/2;
					u_copy_dst += rect_width/2;
					v_copy_src += frame_width/2;
					v_copy_dst += rect_width/2;
				}
			}
			break;
		default:
			/*
			 * glamo 3362 only supports YUV 4:2:0 planar formats.
			 */
			is_ok = FALSE;
			goto out;
	}
	is_ok = TRUE;
out:
#if LOG_XVIDEO
	GLAMO_LOG("leave.is_ok:%d\n", is_ok);
#endif
	return is_ok;
}

static Bool
GLAMOVideoUploadFrameToOffscreen (KdScreenInfo *screen,
				  unsigned char *yuv_frame,
				  int fourcc_code,
				  unsigned yuv_frame_width,
				  unsigned yuv_frame_height,
				  short src_x, short src_y,
				  short src_w, short src_h,
				  GLAMOPortPrivPtr portPriv,
				  unsigned int *out_offscreen_frame)
{
	int idx = 0;
	unsigned size = 0;
	Bool is_ok = FALSE;
	ScreenPtr pScreen = screen->pScreen;
	char *offscreen_frame = NULL;

#if LOG_XVIDEO
	GLAMO_LOG("enter. frame(%dx%d), crop(%dx%d)\n",
		  yuv_frame_width, yuv_frame_height,
		  src_w, src_h);
#endif

	if (!GetYUVFrameByteSize (fourcc_code, src_w, src_h, &size)) {
		GLAMO_LOG("failed to get frame size\n");
		goto out;
	}

	if (!portPriv->off_screen_yuv_buf
	    || size < portPriv->off_screen_yuv_buf->size) {
		if (portPriv->off_screen_yuv_buf) {
			KdOffscreenFree(pScreen,
					portPriv->off_screen_yuv_buf);
		}
		portPriv->off_screen_yuv_buf =
			KdOffscreenAlloc(pScreen, size, VID_PITCH_ALIGN(2),
					 TRUE, GLAMOVideoSave, portPriv);
		if (!portPriv->off_screen_yuv_buf) {
			GLAMO_LOG("failed to allocate offscreen memory\n");
			goto out;
		}
		GLAMO_LOG("allocated %d bytes of offscreen memory\n", size);
	}
	offscreen_frame = screen->memory_base +
				portPriv->off_screen_yuv_buf->offset;

	if (out_offscreen_frame)
		*out_offscreen_frame = portPriv->off_screen_yuv_buf->offset;

	if (!CopyYUVPlanarFrameRect (yuv_frame, fourcc_code,
				     yuv_frame_width, yuv_frame_height,
				     src_x, src_y, src_w, src_h,
				     offscreen_frame)) {
		GLAMO_LOG("failed to copy yuv frame to offscreen memory\n");
		goto out;
	}

	is_ok = TRUE;
out:

#if LOG_XVIDEO
	GLAMO_LOG("leave:%d\n", is_ok);
#endif
	return is_ok;

}

static Bool
GLAMOApplyClipBoxes(ScreenPtr pScreen,
		    GLAMOPortPrivPtr portPriv,
		    const RegionPtr clipBoxes,
		    const DrawablePtr dst_drawable,
		    unsigned short dst_w,
		    unsigned short dst_h)
{
	Bool is_ok = FALSE;
	int i = 0, num_clip_rects =0, depth = 0;
	BoxPtr clip_extents = NULL;
	BoxPtr clip_rect = NULL;
	BoxRec full_box;
	RegionRec full_region;
	PixmapPtr dst_pixmap = NULL;
	DrawablePtr overlay_drawable = NULL;
	CARD32 overlay_pixmap_offset = 0;
	CARD16 overlay_pitch = 0, overlay_width = 0, overlay_height = 0;
	KdScreenPriv(pScreen);

	GLAMO_RETURN_VAL_IF_FAIL (pScreen
				  && portPriv
				  && clipBoxes
				  && dst_drawable,
				  FALSE);

	GLAMO_LOG("enter\n");

	clip_extents = REGION_EXTENTS(pScreen, clipBoxes);
	
	num_clip_rects = REGION_NUM_RECTS (clipBoxes);
	GLAMO_LOG("got %d clip rects\n", num_clip_rects);
	for (i = 0, clip_rect = REGION_RECTS(clipBoxes);
	     i < num_clip_rects;
	     i++, clip_rect++) {
		GLAMO_LOG("rect N%d:(%d,%d,%d,%d)\n",
			  i, clip_rect->x1, clip_rect->y1,
			  clip_rect->x2, clip_rect->y2);
	}

	if (dst_drawable->type == DRAWABLE_WINDOW) {
		dst_pixmap =
			(*pScreen->GetWindowPixmap)((WindowPtr)dst_drawable);
	} else {
		dst_pixmap = (PixmapPtr) dst_drawable;
	}

	if (portPriv->overlay_pixmap
	    && (portPriv->overlay_pixmap->drawable.width < dst_w
		||portPriv->overlay_pixmap->drawable.height < dst_h)) {
		(*pScreen->DestroyPixmap)(portPriv->overlay_pixmap);
		portPriv->overlay_pixmap = NULL;
		GLAMO_LOG("destroyed overlay pixmap");
	}
	depth = pScreenPriv->screen->fb[0].depth;
	if (!portPriv->overlay_pixmap) {
		portPriv->overlay_pixmap =
			(*pScreen->CreatePixmap) (pScreen,
						  dst_w, dst_h,
						  depth);
		GLAMO_LOG("overlay pixmap info: (%dx%d):%d,@:%#x\n",
			  dst_w, dst_h, depth,
			  (char*)portPriv->overlay_pixmap->devPrivate.ptr);
	}
	if (!portPriv->overlay_pixmap) {
		GLAMO_LOG_ERROR("failed to allocate overlay pixmap\n");
		goto out;
	}
	if (!kaaPixmapIsOffscreen(portPriv->overlay_pixmap)) {
		kaaMoveInPixmap(portPriv->overlay_pixmap);
	}
	if (!kaaPixmapIsOffscreen(portPriv->overlay_pixmap)) {
		GLAMO_LOG_ERROR("failed to migrate overlay "
				"pixmap to vram\n");
		goto out;
	}
	/*
	 * compute a couple of paramters that will be useful later.
	 */
	overlay_drawable = (DrawablePtr)portPriv->overlay_pixmap;
	overlay_pixmap_offset =
		(CARD8*)portPriv->overlay_pixmap->devPrivate.ptr
		 - (CARD8*)pScreenPriv->screen->memory_base;
	overlay_pitch = portPriv->overlay_pixmap->devKind/(depth/8);
	overlay_width = abs(clip_extents->x2 - clip_extents->x1);
	overlay_height = abs(clip_extents->y2 - clip_extents->y1);
	full_box.x1 = full_box.y1 = 0;
	full_box.x2 = full_box.x1 + overlay_drawable->width;
	full_box.y2 = full_box.y1 + overlay_drawable->height;
	REGION_INIT(pScreen, &full_region, &full_box, 0);
	/*
	 * first repaint the whole overlay pixmap in black, i.e,
	 * different from colorkey.
	 * That is a kind of reinitialisation of the overlay area
	 */
	KXVPaintRegion (overlay_drawable, &full_region, 0);
	REGION_UNINIT(pScreen, &full_region);
	/*
	 * now draw the clipping region in the overlay pixmap using the
	 * colorkey as foreground
	 */
	KXVPaintRegion (overlay_drawable, clipBoxes, portPriv->color_key);

	/*tell the ISP about the color kay overlay.*/
	GLAMOISPSetColorKeyOverlay2(pScreen, overlay_pixmap_offset,
				    clip_extents->x1, clip_extents->y1,
				    overlay_width,
				    overlay_height,
				    overlay_pitch,
				    portPriv->color_key);

	is_ok = TRUE;

out:
	GLAMO_LOG("leave\n");
	return is_ok;
}

static void
GLAMODisplayYUVPlanarFrameRegion (ScreenPtr pScreen,
				  unsigned int yuv_frame_addr,
				  int fourcc_code,
				  short frame_width,
				  short frame_height,
				  unsigned int dst_addr,
				  short dst_pitch,
				  unsigned short scale_w,
				  unsigned short scale_h,
				  RegionPtr clipping_region,
				  BoxPtr dst_box)
{
	BoxPtr rect = NULL;
	int num_rects = 0;
	int i =0;
	int dst_width = 0, dst_height = 0;
	unsigned int y_addr = 0, u_addr = 0, v_addr = 0,
		     src_x, src_y, src_w, src_h, dest_addr;

	GLAMO_RETURN_IF_FAIL(clipping_region && dst_box);

	GLAMO_LOG("enter: frame addr:%#x, fourcc:%#x, \n"
		  "frame:(%dx%d), dst_addr:%#x, dst_pitch:%hd\n"
		  "scale:(%hd,%hd), dst_box(%d,%d)\n",
		  yuv_frame_addr, fourcc_code, frame_width, frame_height,
		  dst_addr, dst_pitch, scale_w, scale_h,
		  dst_box->x1, dst_box->y1);

	GLAMO_RETURN_IF_FAIL(clipping_region);
	rect = REGION_RECTS(clipping_region);
	num_rects = REGION_NUM_RECTS(clipping_region);
	GLAMO_LOG("num_rects to display:%d\n", num_rects);
	for (i = 0; i < num_rects; i++, rect++) {
		GLAMO_LOG("rect num:%d, (%d,%d,%d,%d)\n",
			  i,
			  rect->x1, rect->y1,
			  rect->x2, rect->y2);
		dst_width = abs(rect->x2 - rect->x1);
		dst_height = abs(rect->y2 - rect->y1);
		dest_addr = dst_addr + rect->x1*2 + rect->y1*dst_pitch;
		src_w = (dst_width * scale_w) >> 11;
		src_h = (dst_height * scale_h) >> 11;
		src_x = ((abs(rect->x1 - dst_box->x1) * scale_w) >> 11);
		src_y = ((abs(rect->y1 - dst_box->y1) * scale_h) >> 11);
		GLAMO_LOG("matching src rect:(%d,%d)-(%dx%d)\n",
			  src_x,src_y, src_w, src_h);

		if (!GetYUVFrameAddresses (yuv_frame_addr,
					   fourcc_code,
					   frame_width,
					   frame_height,
					   src_x,
					   src_y,
					   &y_addr,
					   &u_addr,
					   &v_addr)) {
			GLAMO_LOG_ERROR("failed to get yuv frame @\n");
			continue;
		}
		GLAMOISPDisplayYUVPlanarFrame(pScreen,
					      y_addr,
					      u_addr,
					      v_addr,
					      frame_width,
					      frame_width/2,
					      src_w, src_h,
					      dest_addr,
					      dst_pitch,
					      dst_width, dst_height,
					      scale_w, scale_h);
	}

	GLAMO_LOG("leave\n");
}

static int
GLAMOPutImage(KdScreenInfo *screen, DrawablePtr dst_drawable,
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
	GLAMOPortPrivPtr portPriv = (GLAMOPortPrivPtr)data;
	unsigned short scale_w, scale_h;
	unsigned int offscreen_frame_addr = 0, dst_addr = 0;
	PixmapPtr dst_pixmap;
	RegionRec dst_reg, clipped_dst_reg;
	BoxRec dst_box;

#if LOG_XVIDEO
	GLAMO_LOG("enter. id:%#x, frame:(%dx%d) srccrop:(%d,%d)-(%dx%d)\n"
		  "dst geo:(%d,%d)-(%dx%d)\n",
		  id, width, height, src_x, src_y, src_w, src_h,
		  drw_x, drw_y, drw_w, drw_h);
#endif
	memset(&dst_reg, 0, sizeof(dst_reg));
	memset(&clipped_dst_reg, 0, sizeof(clipped_dst_reg));
	memset(&dst_box, 0, sizeof(dst_box));

	dst_box.x1 = drw_x;
	dst_box.y1 = drw_y;
	dst_box.x2 = dst_box.x1 + drw_w;
	dst_box.y2 = dst_box.y1 + drw_h;

	REGION_INIT(pScreen, &dst_reg, &dst_box, 0);
	REGION_INTERSECT(pScreen, &clipped_dst_reg, &dst_reg, clipBoxes);
	REGION_UNINIT(pScreen, &dst_reg);
	GLAMO_RETURN_VAL_IF_FAIL(REGION_NOTEMPTY(pScreen, &clipped_dst_reg),
				 BadImplementation);

	/*
	 * upload the YUV frame to offscreen vram so that GLAMO can
	 * later have access to it and blit it from offscreen vram to
	 * onscreen vram.
	 */
	if (!GLAMOVideoUploadFrameToOffscreen(screen, buf, id,
					      width, height,
					      src_x, src_y, src_w, src_h,
					      portPriv,
					      &offscreen_frame_addr)) {
		GLAMO_LOG("failed to upload frame to offscreen\n");
		return BadAlloc;
	}
#if LOG_XVIDEO
	GLAMO_LOG("copied video frame to vram offset:%#x\n",
		  offscreen_frame_addr);
	GLAMO_LOG("y_pitch:%hd, crop(%hdx%hd)\n", src_w, src_w, src_h);
#endif 

	if (dst_drawable->type == DRAWABLE_WINDOW) {
		dst_pixmap =
		(*screen->pScreen->GetWindowPixmap)((WindowPtr)dst_drawable);
	} else {
		dst_pixmap = (PixmapPtr)dst_drawable;
	}
	if (!dst_pixmap->devPrivate.ptr) {
		GLAMO_LOG("dst pixmap should be in vram\n");
		return BadImplementation;
	}

	dst_addr = (CARD8*)dst_pixmap->devPrivate.ptr - screen->memory_base;
	scale_w = (src_w << 11)/drw_w;
	scale_h = (src_h << 11)/drw_h;

#if LOG_XVIDEO
	GLAMO_LOG("y_pitch:%hd, crop(%hdx%hd)\n", src_w, src_w, src_h);
#endif

	GLAMODisplayYUVPlanarFrameRegion(pScreen,
					 offscreen_frame_addr,
					 id,
					 src_w, src_h,
					 dst_addr,
					 dst_pixmap->devKind,
					 scale_w, scale_h,
					 &clipped_dst_reg,
					 &dst_box);

	REGION_UNINIT(pScreen, &clipped_dst_reg);

#if LOG_XVIDEO
	GLAMO_LOG("leave\n");
#endif
	return Success;

}

static int
GLAMOReputImage(KdScreenInfo *screen, DrawablePtr pDraw,
		short drw_x, short drw_y, RegionPtr clipBoxes, pointer data)
{
	ScreenPtr pScreen = screen->pScreen;
	GLAMOPortPrivPtr	pPortPriv = (GLAMOPortPrivPtr)data;
	BoxPtr pOldExtents = REGION_EXTENTS(screen->pScreen,
					    &pPortPriv->clip);
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

static KdImageRec Images[] =
{
	XVIMAGE_YV12,
	XVIMAGE_I420,
};
#define NUM_IMAGES (sizeof(Images)/sizeof(Images[0]))

static KdVideoAdaptorPtr
GLAMOSetupImageVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	KdVideoAdaptorPtr adapt;
	GLAMOPortPrivPtr portPriv;
	int i;

	GLAMO_LOG("enter\n");
	glamos->num_texture_ports = 1;

	adapt = xcalloc(1, sizeof(KdVideoAdaptorRec) +
			   glamos->num_texture_ports *
			   (sizeof(GLAMOPortPrivRec) + sizeof(DevUnion)));
	if (adapt == NULL)
		return NULL;

	adapt->type = XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags = VIDEO_CLIP_TO_VIEWPORT;
	adapt->name = "GLAMO Video Overlay";
	adapt->nEncodings = 1;
	adapt->pEncodings = DummyEncoding;
	adapt->nFormats = NUM_FORMATS;
	adapt->pFormats = Formats;
	adapt->nPorts = glamos->num_texture_ports;
	adapt->pPortPrivates = (DevUnion*)(&adapt[1]);

	portPriv = (GLAMOPortPrivPtr)
		(&adapt->pPortPrivates[glamos->num_texture_ports]);

	for (i = 0; i < glamos->num_texture_ports; i++)
		adapt->pPortPrivates[i].ptr = &portPriv[i];

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
	/*adapt->ReputImage = GLAMOReputImage;*/
	adapt->QueryImageAttributes = GLAMOQueryImageAttributes;

	for (i = 0; i < glamos->num_texture_ports; i++) {
		REGION_INIT(pScreen, &portPriv[i].clip, NullBox, 0);
		portPriv[i].color_key = 0xffff;
	}

	glamos->pAdaptor = adapt;

	GLAMO_LOG("leave. adaptor:%#x\n", (unsigned)adapt);

	return adapt;
}

Bool
GLAMOInitVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	GLAMOCardInfo(pScreenPriv);
	KdScreenInfo *screen = pScreenPriv->screen;
	KdVideoAdaptorPtr *oldAdaptors = NULL, *newAdaptors = NULL;
	KdVideoAdaptorPtr newAdaptor = NULL;
	int num_adaptors;
	Bool is_ok = FALSE;

	GLAMO_LOG("enter\n");

	glamos->pAdaptor = NULL;

	if (glamoc->reg_base == NULL) {
		GLAMO_LOG("glamoc->reg_base is null\n");
		goto out;
	}

	num_adaptors = KdXVListGenericAdaptors(screen, &oldAdaptors);

	newAdaptor = GLAMOSetupImageVideo(pScreen);

	if (newAdaptor)  {
		if (!num_adaptors) {
			num_adaptors = 1;
			newAdaptors = &newAdaptor;
		} else {
			newAdaptors = xalloc((num_adaptors + 1) *
			    sizeof(KdVideoAdaptorPtr *));
			if (!newAdaptors)
				goto out;

			memcpy(newAdaptors,
			       oldAdaptors,
			       num_adaptors * sizeof(KdVideoAdaptorPtr));
			newAdaptors[num_adaptors] = newAdaptor;
			num_adaptors++;
		}
	}

	GLAMOCMDQCacheSetup(pScreen);
	GLAMOISPEngineInit(pScreen);

	if (num_adaptors)
		KdXVScreenInit(pScreen, newAdaptors, num_adaptors);

	is_ok = TRUE;
out:
	GLAMO_LOG("leave. is_ok:%d, adaptors:%d\n",
		  is_ok, num_adaptors);
	/*
	if (newAdaptors) {
		xfree(newAdaptors);
	}
	*/
	return is_ok;
}

void
GLAMOFiniVideo(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	KdVideoAdaptorPtr adapt = glamos->pAdaptor;
	GLAMOPortPrivPtr portPriv;
	int i;

	GLAMO_LOG ("enter\n");

	if (!adapt)
		return;

	for (i = 0; i < glamos->num_texture_ports; i++) {
		portPriv = (GLAMOPortPrivPtr)(adapt->pPortPrivates[i].ptr);
		GLAMO_LOG("freeing clipping region...\n");
		REGION_UNINIT(pScreen, &portPriv->clip);
		GLAMO_LOG("freed clipping region\n");
#if 0
		if (portPriv->off_screen_yuv_buf) {
			GLAMO_LOG("freeing offscreen yuv buf...\n");
			KdOffscreenFree(pScreen,
					portPriv->off_screen_yuv_buf);
			portPriv->off_screen_yuv_buf = NULL;
			GLAMO_LOG("freeed offscreen yuv buf\n");
		}
#endif

	}
	xfree(adapt);
	glamos->pAdaptor = NULL;
	GLAMO_LOG ("leave\n");
}

void
GLAMOVideoTeardown(ScreenPtr pScreen)
{
	GLAMOEngineReset(pScreen, GLAMO_ENGINE_ISP);
	GLAMOEngineDisable(pScreen, GLAMO_ENGINE_ISP);
}

#endif /*XV*/
