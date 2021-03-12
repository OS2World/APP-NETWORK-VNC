#
# GCC makefile
#

LIB = ../vncclient.a

SRCS = tls_openssl.c cursor.c listen.c rfbproto.c sockets.c vncviewer.c \
       ../common/crypto_openssl.c ../common/minilzo.c ../common/turbojpeg.c

OBJS = $(SRCS:.c=.o)

#LIBS = -lpthread -llibcrypto -llibssl -lz -ljpeg

DEFS = -DLIBVNCSERVER_HAVE_LIBSSL
DEFS += -D_DIGI -D_DIGI_NO_TCP_NODELAY

#
# _DIGI - bug fixes and OS/2 VNC Client/VNC Server related changes.
#
# _DIGI_NO_TCP_NODELAY - do not set socket option TCP_NODELAY (used in original
# code). Like:
#   setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one) );
#
# [ IBM OS/2 DEVELOPER'S TOOLKIT ]
# TCP_NODELAY Disables the buffering algorithm so that the client's TCP sends
# small packets as soon as possible. This often has no performance effects on
# LANs, but can degrade performance on WANs. 
#

CFLAGS = -I.. -I../common $(DEFS) -march=pentium -pipe -O2

$(LIB): $(OBJS)
	$(AR) rc $(LIB) $(OBJS)

.c.obj: .AUTODEPEND
	$(CC) $(CFLAGS) -c $<

clean:
	@rm -f $(OBJS) $(LIB)
