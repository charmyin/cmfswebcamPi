# Makefile for fswebcam
# [25/12/2009]

SHELL = /bin/sh

prefix      = /usr/local
exec_prefix = ${prefix}
bindir      = ${exec_prefix}/bin
mandir      = ${datarootdir}/man
datarootdir = ${prefix}/share

CC      = gcc
CFLAGS  = -g -O2 -DHAVE_CONFIG_H
LDFLAGS = -lgd 

OBJS  = fswebcam.o log.o effects.o parse.o src.o src_test.o src_raw.o src_file.o src_v4l1.o src_v4l2.o
OBJS += dec_rgb.o dec_yuv.o dec_grey.o dec_bayer.o dec_jpeg.o dec_png.o
OBJS += dec_s561.o

all: fswebcam fswebcam.1.gz

install: all
	mkdir -p ${DESTDIR}${bindir}
	mkdir -p ${DESTDIR}${mandir}/man1
	install -m 755 fswebcam ${DESTDIR}${bindir}
	install -m 644 fswebcam.1.gz ${DESTDIR}${mandir}/man1

fswebcam: $(OBJS)
	$(CC) -o fswebcam $(OBJS) $(LDFLAGS)

.c.o:
	${CC} ${CFLAGS} -c $< -o $@

fswebcam.1.gz: fswebcam.1
	gzip -c --best fswebcam.1 > fswebcam.1.gz

clean:
	rm -f core* *.o fswebcam fswebcam.1.gz

distclean: clean
	rm -rf config.h *.cache config.log config.status Makefile *.jp*g *.png

