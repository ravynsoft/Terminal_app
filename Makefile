APP=	Terminal
SRCS=	main.m
RESOURCES=	${.CURDIR}/Terminal.png

MK_WERROR=	no
CFLAGS+=	-g -fobjc-arc -I${.CURDIR}/_build/include
LDFLAGS+=	-framework AppKit -framework Foundation -lobjc \
                -L${.CURDIR}/_build/lib -ltsm -lSystem \
                -Wl,-R\$$ORIGIN/../Resources

libtsm::
	rm -rf ${.CURDIR}/_build
	mkdir -p ${.CURDIR}/_build
	cd ${.CURDIR}/libtsm-3 && ./configure --prefix=${.CURDIR}/_build
	cd ${.CURDIR}/libtsm-3 && gmake && gmake install

.include <rvn.app.mk>

all: libtsm
	cp -fv ${.CURDIR}/Info.plist ${APP_DIR}/Contents/
	cp -afv ${.CURDIR}/_build/lib/libtsm.so ${APP_DIR}/Contents/Resources/
	cp -afv ${.CURDIR}/_build/lib/libtsm.so.3 ${APP_DIR}/Contents/Resources/
