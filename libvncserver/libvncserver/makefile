LIB = ../vncserver.a

# rfbcrypto_gnutls.c rfbcrypto_polarssl.c rfbssl_gnutls.c rfbcrypto_included.c
# rfbssl_none.c

SRCS = auth.c cargs.c corre.c cursor.c cutpaste.c draw.c font.c hextile.c \
       httpd.c main.c \
       rfbcrypto_openssl.c rfbregion.c rfbserver.c \
       rfbssl_openssl.c rre.c scale.c selbox.c \
       sockets.c stats.c \
       tight.c translate.c ultra.c websockets.c zlib.c zrle.c \
       zrleoutstream.c zrlepalettehelper.c ws_decode.c \
       ../common/d3des.c ../common/vncauth.c ../common/minilzo.c \
       ../common/turbojpeg.c ../common/sha1.c
# SRCS+= ./tightvnc-filetransfer/filelistinfo.c \
#        ./tightvnc-filetransfer/filetransfermsg.c \
#        ./tightvnc-filetransfer/handlefiletransferrequest.c \
#        ./tightvnc-filetransfer/rfbtightserver.c

OBJS = $(SRCS:.c=.o)

DEFS += -D_DIGI -D_DIGI_NO_TCP_NODELAY

CFLAGS = -I.. -I../common -I. $(DEFS) -march=pentium -pipe -O3

$(LIB): $(OBJS)
	$(AR) rc $(LIB) $(OBJS)

.c.obj: .AUTODEPEND
	$(CC) $(CFLAGS) -c $<

clean:
	@rm -f $(OBJS) $(DEF) $(BIN)
