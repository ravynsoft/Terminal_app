APP=	        Terminal
SRCS=	        AppDelegate.m \
                main.m \
                tmt.c
RESOURCES=	${.CURDIR}/Terminal.png

MK_WERROR=	no
CFLAGS+=	-g -fobjc-arc -O0
LDFLAGS+=	-framework AppKit -framework Foundation -lobjc -lSystem -lutil

.include <rvn.app.mk>
