APP=	        Terminal
SRCS=	        AppDelegate.m \
                TerminalView.m \
                main.m \
                tmt.c
RESOURCES=	${.CURDIR}/Terminal.png

MK_WERROR=	no
CFLAGS+=	-g -fobjc-arc -O0
LDFLAGS+=	-framework AppKit -framework CoreGraphics \
                -framework Foundation -lobjc -lSystem /lib/libutil.so.9

.include <rvn.app.mk>
