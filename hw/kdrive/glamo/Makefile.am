INCLUDES = 					\
	@KDRIVE_INCS@				\
	@KDRIVE_CFLAGS@                         \
	-I$(srcdir)/../../../exa                \
	-I$(srcdir)/../fbdev

bin_PROGRAMS = Xglamo

noinst_LIBRARIES = libglamo.a

libglamo_a_SOURCES = 	\
	glamo-log.h	\
	glamo-cmdq.c	\
	glamo-cmdq.h	\
	glamo-draw.c	\
	glamo-draw.h	\
	glamo.c		\
	glamo.h		\
	glamo-regs.h	\
	glamo-video.c \
	glamo-funcs.c \
	glamo-funcs.h

Xglamo_SOURCES = \
	glamo-stub.c

GLAMO_LIBS =				\
	libglamo.a 			\
	@KDRIVE_LIBS@

Xglamo_LDADD = \
	$(GLAMO_LIBS)			\
	../../../exa/libexa.la		\
	../fbdev/libfbdev.a             \
	@KDRIVE_LIBS@

Xglamo_DEPENDENCIES =	\
	libglamo.a 			\
	@KDRIVE_LOCAL_LIBS@

relink:
	rm -f $(bin_PROGRAMS) && make $(bin_PROGRAMS)
