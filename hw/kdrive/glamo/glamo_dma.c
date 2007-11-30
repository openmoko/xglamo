/*
 * Copyright © 2007 OpenMoko, Inc.
 *
 * This driver is based on Xati,
 * Copyright © 2004 Eric Anholt
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

#include <sys/time.h>

#include "glamo.h"
#include "glamo-regs.h"
#include "glamo_dma.h"
#include "glamo_draw.h"

#ifdef USE_DRI
#include "radeon_common.h"
#include "glamo_sarea.h"
#endif /* USE_DRI */

#define DEBUG_FIFO 1

#if DEBUG_FIFO
static void
GLAMODebugFifo(GLAMOScreenInfo *glamos)
{
	GLAMOCardInfo *glamoc = glamos->glamoc;
	char *mmio = glamoc->reg_base;
	CARD32 offset;

	ErrorF("GLAMO_REG_CQ_STATUS: 0x%04x\n",
	    MMIO_IN16(mmio, GLAMO_REG_CQ_STATUS));

	offset = MMIO_IN16(mmio, GLAMO_REG_CQ_WRITE_ADDRL);
	offset |= (MMIO_IN16(mmio, GLAMO_REG_CQ_WRITE_ADDRH) << 16) & 0x7;
	ErrorF("GLAMO_REG_CQ_WRITE_ADDR: 0x%08x\n", (unsigned int) offset);

	offset = MMIO_IN16(mmio, GLAMO_REG_CQ_READ_ADDRL);
	offset |= (MMIO_IN16(mmio, GLAMO_REG_CQ_READ_ADDRH) << 16) & 0x7;
	ErrorF("GLAMO_REG_CQ_READ_ADDR: 0x%08x\n", (unsigned int) offset);
}
#endif

void
GLAMOEngineReset(ScreenPtr pScreen, enum glamo_engine engine)
{
	KdScreenPriv(pScreen);
	GLAMOCardInfo(pScreenPriv);
	CARD32 reg;
	CARD16 mask;
	char *mmio = glamoc->reg_base;

	if (!mmio)
		return;

	switch (engine) {
	case GLAMO_ENGINE_ISP:
		reg = GLAMO_REG_CLOCK_ISP;
		mask = GLAMO_CLOCK_ISP2_RESET;
		break;
	case GLAMO_ENGINE_CQ:
		reg = GLAMO_REG_CLOCK_2D;
		mask = GLAMO_CLOCK_2D_CQ_RESET;
		break;
	case GLAMO_ENGINE_2D:
		reg = GLAMO_REG_CLOCK_2D;
		mask = GLAMO_CLOCK_2D_RESET;
		break;
	}

	MMIOSetBitMask(mmio, reg, mask, 0xffff);
	usleep(1000);
	MMIOSetBitMask(mmio, reg, mask, 0);
	usleep(1000);
}

void
GLAMOEngineDisable(ScreenPtr pScreen, enum glamo_engine engine)
{
	KdScreenPriv(pScreen);
	GLAMOCardInfo(pScreenPriv);
	char *mmio = glamoc->reg_base;

	if (!mmio)
		return;

	return;
}

void
GLAMOEngineEnable(ScreenPtr pScreen, enum glamo_engine engine)
{
	KdScreenPriv(pScreen);
	GLAMOCardInfo(pScreenPriv);
	char *mmio = glamoc->reg_base;

	if (!mmio)
		return;

	switch (engine) {
	case GLAMO_ENGINE_ISP:
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_ISP,
			       GLAMO_CLOCK_ISP_EN_M2CLK |
			       GLAMO_CLOCK_ISP_EN_I1CLK,
			       0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_2,
			       GLAMO_CLOCK_GEN52_EN_DIV_ICLK,
			       0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
			       GLAMO_CLOCK_GEN51_EN_DIV_JCLK,
			       0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
			       GLAMO_HOSTBUS2_MMIO_EN_ISP,
			       0xffff);
		break;
	case GLAMO_ENGINE_CQ:
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
			       GLAMO_CLOCK_2D_EN_M6CLK,
			       0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
			       GLAMO_HOSTBUS2_MMIO_EN_CQ,
			       0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
			       GLAMO_CLOCK_GEN51_EN_DIV_MCLK,
			       0xffff);
		break;
	case GLAMO_ENGINE_2D:
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_2D,
				GLAMO_CLOCK_2D_EN_M7CLK |
				GLAMO_CLOCK_2D_EN_GCLK |
				GLAMO_CLOCK_2D_DG_M7CLK |
				GLAMO_CLOCK_2D_DG_GCLK,
				0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_HOSTBUS(2),
			       GLAMO_HOSTBUS2_MMIO_EN_2D,
			       0xffff);
		MMIOSetBitMask(mmio, GLAMO_REG_CLOCK_GEN5_1,
			       GLAMO_CLOCK_GEN51_EN_DIV_GCLK,
			       0xffff);
		break;
	}
}

void
GLAMOWaitIdle(GLAMOScreenInfo *glamos)
{
	GLAMOCardInfo *glamoc = glamos->glamoc;
	char *mmio = glamoc->reg_base;
	CARD16 status;
	TIMEOUT_LOCALS;

	if (glamos->indirectBuffer != NULL)
		GLAMOFlushIndirect(glamos, 0);

#ifdef USE_DRI
	if (glamos->using_dri) {
		int ret = 0;
		int cmd = (glamoc->is_3362 ? DRM_RADEON_CP_IDLE :
		    DRM_R128_CCE_IDLE);
		WHILE_NOT_TIMEOUT(2) {
			ret = drmCommandNone(glamoc->drmFd, cmd);
			if (ret != -EBUSY)
				break;
		}
		if (TIMEDOUT()) {
			GLAMODebugFifo(glamos);
			FatalError("Timed out idling CCE (card hung)\n");
		}
		if (ret != 0)
			ErrorF("Failed to idle DMA, returned %d\n", ret);
		return;
	}
#endif

	WHILE_NOT_TIMEOUT(.5) {
		status = MMIO_IN16(mmio, GLAMO_REG_CQ_STATUS);
		if ((status & (1 << 2)) && !(status & (1 << 8)))
			break;
	}
	if (TIMEDOUT()) {
		ErrorF("Timeout idling accelerator, resetting...\n");
		GLAMOEngineReset(glamos->screen->pScreen, GLAMO_ENGINE_CQ);
		GLAMODrawSetup(glamos->screen->pScreen);
	}

#if DEBUG_FIFO
	ErrorF("Idle?\n");
	GLAMODebugFifo(glamos);
#endif
}

dmaBuf *
GLAMOGetDMABuffer(GLAMOScreenInfo *glamos)
{
	dmaBuf *buf;

	buf = (dmaBuf *)xalloc(sizeof(dmaBuf));
	if (buf == NULL)
		return NULL;

#ifdef USE_DRI
	if (glamos->using_dri) {
		buf->drmBuf = GLAMODRIGetBuffer(glamos);
		if (buf->drmBuf == NULL) {
			xfree(buf);
			return NULL;
		}
		buf->size = buf->drmBuf->total;
		buf->used = buf->drmBuf->used;
		buf->address = buf->drmBuf->address;
		return buf;
	}
#endif /* USE_DRI */

	buf->size = glamos->ring_len / 2;
	buf->address = xalloc(buf->size);
	if (buf->address == NULL) {
		xfree(buf);
		return NULL;
	}
	buf->used = 0;

	return buf;
}

static void
GLAMODispatchIndirectDMA(GLAMOScreenInfo *glamos)
{
	GLAMOCardInfo *glamoc = glamos->glamoc;
	dmaBuf *buf = glamos->indirectBuffer;
	char *mmio = glamoc->reg_base;
	CARD16 *addr;
	int count, ring_count;
	TIMEOUT_LOCALS;

	addr = (CARD16 *)((char *)buf->address + glamos->indirectStart);
	count = (buf->used - glamos->indirectStart) / 2;
	ring_count = glamos->ring_len / 2;

	WHILE_NOT_TIMEOUT(.5) {
		if (count <= 0)
			break;

		glamos->ring_addr[glamos->ring_write++] = *addr++;
		if (glamos->ring_write >= ring_count)
			glamos->ring_write = 0;

		while (glamos->ring_write == glamos->ring_read)
		{
			glamos->ring_read =
				MMIO_IN16(mmio, GLAMO_REG_CQ_READ_ADDRL);
			glamos->ring_read |=
				(MMIO_IN16(mmio, GLAMO_REG_CQ_READ_ADDRH) & 0x7) << 16;
		}

		count--;
	}
	if (TIMEDOUT()) {
		ErrorF("Timeout submitting packets, resetting...\n");
		GLAMOEngineReset(glamos->screen->pScreen, GLAMO_ENGINE_CQ);
		GLAMODrawSetup(glamos->screen->pScreen);
	}

	MMIO_OUT16(mmio, GLAMO_REG_CQ_WRITE_ADDRH,
			 (glamos->ring_write >> 15) & 0x7);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_WRITE_ADDRL,
			 (glamos->ring_write <<  1) & 0xffff);
}

void
GLAMOFlushIndirect(GLAMOScreenInfo *glamos, Bool discard)
{
	dmaBuf *buf = glamos->indirectBuffer;

	if ((glamos->indirectStart == buf->used) && !discard)
		return;

#if DEBUG_FIFO
	ErrorF("Dispatching %d DWORDS\n", (buf->used - glamos->indirectStart) /
	    4);
#endif

#ifdef USE_DRI
	if (glamos->using_dri) {
		buf->drmBuf->used = buf->used;
		GLAMODRIDispatchIndirect(glamos, discard);
		if (discard) {
			buf->drmBuf = GLAMODRIGetBuffer(glamos);
			buf->size = buf->drmBuf->total;
			buf->used = buf->drmBuf->used;
			buf->address = buf->drmBuf->address;
			glamos->indirectStart = 0;
		} else {
			/* Start on a double word boundary */
			glamos->indirectStart = buf->used = (buf->used + 7) & ~7;
		}
		return;
	}
#endif /* USE_DRI */

	GLAMODispatchIndirectDMA(glamos);

	buf->used = 0;
	glamos->indirectStart = 0;
}

static Bool
GLAMODMAInit(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);
	GLAMOCardInfo(pScreenPriv);
	char *mmio = glamoc->reg_base;
	int cq_len = 63;

	glamos->ring_len = (cq_len + 1) * 1024;

	glamos->dma_space = KdOffscreenAlloc(pScreen, glamos->ring_len + 4,
			                     16, TRUE, NULL, NULL);
	if (!glamos->dma_space)
		return FALSE;

	glamos->ring_addr = (CARD16 *) (pScreenPriv->screen->memory_base +
			                glamos->dma_space->offset);
	glamos->ring_read = 0;
	glamos->ring_write = 0;

	/* make the decoder happy? */
	glamos->ring_addr[glamos->ring_len / 2] = 0x0;
	glamos->ring_addr[glamos->ring_len / 2 + 1] = 0x0;

	GLAMOEngineEnable(glamos->screen->pScreen, GLAMO_ENGINE_CQ);
	GLAMOEngineReset(glamos->screen->pScreen, GLAMO_ENGINE_CQ);

	MMIO_OUT16(mmio, GLAMO_REG_CQ_BASE_ADDRL,
			 glamos->dma_space->offset & 0xffff);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_BASE_ADDRH,
			 (glamos->dma_space->offset >> 16) & 0x7f);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_LEN, cq_len);

	MMIO_OUT16(mmio, GLAMO_REG_CQ_WRITE_ADDRH, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_WRITE_ADDRL, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_READ_ADDRH, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_READ_ADDRL, 0);
	MMIO_OUT16(mmio, GLAMO_REG_CQ_CONTROL,
			 1 << 12 |
			 5 << 8 |
			 8 << 4);

	return TRUE;
}

void
GLAMODMASetup(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

#ifdef USE_DRI
	if (glamos->using_dri)
		GLAMODRIDMAStart(glamos);
#endif /* USE_DRI */

	if (!glamos->using_dri)
		GLAMODMAInit(pScreen);

	glamos->indirectBuffer = GLAMOGetDMABuffer(glamos);
	if (glamos->indirectBuffer == FALSE)
		FatalError("Failed to allocate DMA buffer.\n");

	if (glamos->using_dri)
		ErrorF("Initialized DRI DMA\n");
	else
		ErrorF("Initialized DMA\n");
}

void
GLAMODMATeardown(ScreenPtr pScreen)
{
	KdScreenPriv(pScreen);
	GLAMOScreenInfo(pScreenPriv);

	GLAMOWaitIdle(glamos);

#ifdef USE_DRI
	if (glamos->using_dri)
		GLAMODRIDMAStop(glamos);
#endif /* USE_DRI */

	xfree(glamos->indirectBuffer->address);
	xfree(glamos->indirectBuffer);
	glamos->indirectBuffer = NULL;
}
