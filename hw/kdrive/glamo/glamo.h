/*
 * Copyright � 2007 OpenMoko, Inc.
 *
 * This driver is based on Xati,
 * Copyright � 2003 Eric Anholt
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

#ifndef _GLAMO_H_
#define _GLAMO_H_

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif

#ifdef KDRIVEFBDEV
#include <fbdev.h>
#endif
#ifdef KDRIVEVESA
#include <vesa.h>
#endif

#include "kxv.h"

#undef XF86DRI
#ifdef XF86DRI
#define USE_DRI
#include "xf86drm.h"
#include "dri.h"
#ifdef GLXEXT
#include "GL/glxint.h"
#include "GL/glxtokens.h"
#include "glamo_dripriv.h"
#endif
#endif

#define GLAMO_REG_BASE(c)		((c)->attr.address[0])
#define GLAMO_REG_SIZE(c)		(0x2400)

#ifdef __powerpc__

static __inline__ void
MMIO_OUT16(__volatile__ void *base, const unsigned long offset,
	   const unsigned int val)
{
	__asm__ __volatile__(
			"stwbrx %1,%2,%3\n\t"
			"eieio"
			: "=m" (*((volatile unsigned char *)base+offset))
			: "r" (val), "b" (base), "r" (offset));
}

static __inline__ CARD32
MMIO_IN16(__volatile__ void *base, const unsigned long offset)
{
	register unsigned int val;
	__asm__ __volatile__(
			"lwbrx %0,%1,%2\n\t"
			"eieio"
			: "=r" (val)
			: "b" (base), "r" (offset),
			"m" (*((volatile unsigned char *)base+offset)));
	return val;
}

#else

#define MMIO_OUT16(mmio, a, v)		(*(VOL16 *)((mmio) + (a)) = (v))
#define MMIO_IN16(mmio, a)		(*(VOL16 *)((mmio) + (a)))

#endif

typedef volatile CARD8	VOL8;
typedef volatile CARD16	VOL16;
typedef volatile CARD32	VOL32;

struct backend_funcs {
	void    (*cardfini)(KdCardInfo *);
	void    (*scrfini)(KdScreenInfo *);
	Bool    (*initScreen)(ScreenPtr);
	Bool    (*finishInitScreen)(ScreenPtr pScreen);
	Bool	(*createRes)(ScreenPtr);
	void    (*preserve)(KdCardInfo *);
	void    (*restore)(KdCardInfo *);
	Bool    (*dpms)(ScreenPtr, int);
	Bool    (*enable)(ScreenPtr);
	void    (*disable)(ScreenPtr);
	void    (*getColors)(ScreenPtr, int, int, xColorItem *);
	void    (*putColors)(ScreenPtr, int, int, xColorItem *);
#ifdef RANDR
	Bool	(*randrSetConfig) (ScreenPtr, Rotation, int, RRScreenSizePtr);
#endif
};

typedef struct _GLAMOCardInfo {
	union {
#ifdef KDRIVEFBDEV
		FbdevPriv fbdev;
#endif
#ifdef KDRIVEVESA
		VesaCardPrivRec vesa;
#endif
	} backend_priv;
	struct backend_funcs backend_funcs;

	char *reg_base;
	Bool is_3362;
	CARD32 crtc_pitch;
	CARD32 crtc2_pitch;
#ifdef USE_DRI
	int drmFd;
#endif /* USE_DRI */
	Bool use_fbdev, use_vesa;
} GLAMOCardInfo;

#define getGLAMOCardInfo(kd)	((GLAMOCardInfo *) ((kd)->card->driver))
#define GLAMOCardInfo(kd)		GLAMOCardInfo *glamoc = getGLAMOCardInfo(kd)

typedef struct _GLAMOCursor {
	int		width, height;
	int		xhot, yhot;

	Bool		has_cursor;
	CursorPtr	pCursor;
	Pixel		source, mask;
	KdOffscreenArea *area;
} GLAMOCursor;

typedef struct _GLAMOPortPriv {
	int brightness;
	int saturation;
	RegionRec clip;
	CARD32 size;
	KdOffscreenArea *off_screen;
	DrawablePtr pDraw;
	PixmapPtr pPixmap;

	CARD32 src_offset;
	CARD32 src_pitch;
	CARD8 *src_addr;

	int id;
	int src_x1, src_y1, src_x2, src_y2;
	int dst_x1, dst_y1, dst_x2, dst_y2;
	int src_w, src_h, dst_w, dst_h;
} GLAMOPortPrivRec, *GLAMOPortPrivPtr;

typedef struct _dmaBuf {
	int size;
	int used;
	void *address;
#ifdef USE_DRI
	drmBufPtr drmBuf;
#endif
} dmaBuf;

typedef struct _GLAMOScreenInfo {
	union {
#ifdef KDRIVEFBDEV
		FbdevScrPriv fbdev;
#endif
#ifdef KDRIVEVESA
		VesaScreenPrivRec vesa;
#endif
	} backend_priv;
	KaaScreenInfoRec kaa;

	GLAMOCardInfo *glamoc;
	KdScreenInfo *screen;

	int		scratch_offset;
	int		scratch_next;
	KdOffscreenArea *scratch_area;

	GLAMOCursor	cursor;

	KdVideoAdaptorPtr pAdaptor;
	int		num_texture_ports;

	Bool		using_dri;	/* If we use the DRM for DMA. */

	KdOffscreenArea *dma_space;	/* For "DMA" from framebuffer. */
	CARD16		*ring_addr;	/* Beginning of ring buffer. */
	int		ring_write;	/* Index of write ptr in ring. */
	int		ring_read;	/* Index of read ptr in ring. */
	int		ring_len;

	dmaBuf		*indirectBuffer;
	int		indirectStart;

#ifdef USE_DRI
	Bool		dma_started;

	drmSize         registerSize;
	drmHandle       registerHandle;
	drmHandle       fbHandle;

	drmSize		gartSize;
	drmHandle	agpMemHandle;		/* Handle from drmAgpAlloc */
	unsigned long	gartOffset;
	unsigned char	*AGP;			/* Map */
	int		agpMode;
	drmSize         pciSize;
	drmHandle       pciMemHandle;

	/* ring buffer data */
	unsigned long	ringStart;		/* Offset into AGP space */
	drmHandle	ringHandle;		/* Handle from drmAddMap */
	drmSize		ringMapSize;		/* Size of map */
	int		ringSize;		/* Size of ring (MB) */
	unsigned char	*ring;			/* Map */

	unsigned long	ringReadOffset;		/* Offset into AGP space */
	drmHandle	ringReadPtrHandle;	/* Handle from drmAddMap */
	drmSize		ringReadMapSize;	/* Size of map */
	unsigned char	*ringReadPtr;		/* Map */

	/* vertex/indirect buffer data */
	unsigned long	bufStart;		/* Offset into AGP space */
	drmHandle	bufHandle;		/* Handle from drmAddMap */
	drmSize		bufMapSize;		/* Size of map */
	int		bufSize;		/* Size of buffers (MB) */
	unsigned char	*buf;			/* Map */
	int		bufNumBufs;		/* Number of buffers */
	drmBufMapPtr	buffers;		/* Buffer map */

	/* AGP Texture data */
	unsigned long	gartTexStart;		/* Offset into AGP space */
	drmHandle	gartTexHandle;		/* Handle from drmAddMap */
	drmSize		gartTexMapSize;		/* Size of map */
	int		gartTexSize;		/* Size of AGP tex space (MB) */
	unsigned char	*gartTex;		/* Map */
	int		log2GARTTexGran;

	int		DMAusecTimeout;   /* CCE timeout in usecs */

	/* DRI screen private data */
	int		frontOffset;
	int		frontPitch;
	int		backOffset;
	int		backPitch;
	int		depthOffset;
	int		depthPitch;
	int		spanOffset;
	int		textureOffset;
	int		textureSize;
	int		log2TexGran;

	int		irqEnabled;

	int		serverContext;

	DRIInfoPtr	pDRIInfo;
#ifdef GLXEXT
	int		numVisualConfigs;
	__GLXvisualConfig *pVisualConfigs;
	GLAMOConfigPrivPtr pVisualConfigsPriv;
#endif /* GLXEXT */
#endif /* USE_DRI */
} GLAMOScreenInfo;

#define getGLAMOScreenInfo(kd)	((GLAMOScreenInfo *) ((kd)->screen->driver))
#define GLAMOScreenInfo(kd)	GLAMOScreenInfo *glamos = getGLAMOScreenInfo(kd)

typedef union { float f; CARD32 i; } fi_type;

/* Surely there's a better way to go about this */
static inline CARD32
GLAMOFloatAsInt(float val)
{
	fi_type fi;

	fi.f = val;
	return fi.i;
}

#define GET_FLOAT_BITS(x) GLAMOFloatAsInt(x)

static inline void
MMIOSetBitMask(char *mmio, CARD32 reg, CARD16 mask, CARD16 val)
{
	CARD16 tmp;

	val &= mask;

	tmp = MMIO_IN16(mmio, reg);
	tmp &= ~mask;
	tmp |= val;

	MMIO_OUT16(mmio, reg, tmp);
}

/* glamo.c */
Bool
GLAMOMapReg(KdCardInfo *card, GLAMOCardInfo *glamoc);

void
GLAMOUnmapReg(KdCardInfo *card, GLAMOCardInfo *glamoc);

/* glamo_draw.c */
void
GLAMODrawSetup(ScreenPtr pScreen);

Bool
GLAMODrawInit(ScreenPtr pScreen);

void
GLAMODrawEnable(ScreenPtr pScreen);

void
GLAMODrawDisable(ScreenPtr pScreen);

void
GLAMODrawFini(ScreenPtr pScreen);

/* glamo_dri.c */
#ifdef USE_DRI
Bool
GLAMODRIScreenInit(ScreenPtr pScreen);

void
GLAMODRICloseScreen(ScreenPtr pScreen);

void
GLAMODRIDMAStart(GLAMOScreenInfo *glamos);

void
GLAMODRIDMAStop(GLAMOScreenInfo *glamos);

void
GLAMODRIDMAReset(GLAMOScreenInfo *glamos);

void
GLAMODRIDispatchIndirect(GLAMOScreenInfo *glamos, Bool discard);

drmBufPtr
GLAMODRIGetBuffer(GLAMOScreenInfo *glamos);

#endif /* USE_DRI */

/* glamo_cursor.c */
Bool
GLAMOCursorInit(ScreenPtr pScreen);

void
GLAMOCursorEnable(ScreenPtr pScreen);

void
GLAMOCursorDisable(ScreenPtr pScreen);

void
GLAMOCursorFini(ScreenPtr pScreen);

void
GLAMORecolorCursor(ScreenPtr pScreen, int ndef, xColorItem *pdef);

int
GLAMOLog2(int val);

/* glamo_video.c */
Bool
GLAMOInitVideo(ScreenPtr pScreen);

void
GLAMOFiniVideo(ScreenPtr pScreen);

extern KdCardFuncs GLAMOFuncs;

#endif /* _GLAMO_H_ */
