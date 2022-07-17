APP=	Terminal
SRCS=	wlterm/wlterm.c wlterm/wlt_font.c wlterm/wlt_render.c wlterm/shl_htable.c wlterm/shl_pty.c 
RESOURCES=	${.CURDIR}/Terminal.png ${.CURDIR}/_build/lib/libtsm.so.3

MK_WERROR=	no
GTK!=	        pkg-config --cflags gtk+-3.0 cairo pango pangocairo xkbcommon
GTKL!=	        pkg-config --libs gtk+-3.0 cairo pango pangocairo xkbcommon
CFLAGS+=	-g -fobjc-arc -I${.CURDIR}/_build/include -D_GNU_SOURCE ${GTK}
LDFLAGS+=	\
                -L${.CURDIR}/_build/lib ${GTKL} -ltsm -lm -lepoll-shim \
                -Wl,-R\$$ORIGIN/../Resources

${.CURDIR}/_build/lib/libtsm.so.3::
	mkdir -p ${.CURDIR}/_build
	cd ${.CURDIR}/libtsm-3 && ./configure --prefix=${.CURDIR}/_build
	cd ${.CURDIR}/libtsm-3 && gmake && gmake install

clear:
	rm -rfv ${.CURDIR}/_build ${.CURDIR}/Terminal.app

.include <rvn.app.mk>

clean: clear
${APP_DIR}: ${.CURDIR}/_build/lib/libtsm.so.3
