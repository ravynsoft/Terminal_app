APP=	        Terminal
SRCS=	        AppDelegate.m \
                TerminalView.m \
                main.m \
                tmt.c
RESOURCES=	${.CURDIR}/Terminal.png

MK_WERROR=	no
CFLAGS+=	-fobjc-arc -O3
LDFLAGS+=	-framework AppKit -framework Foundation -lobjc -lSystem -lutil

.include <rvn.app.mk>
