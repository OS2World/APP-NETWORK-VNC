LIB = ../vncserver.a

# rfbcrypto_gnutls.c rfbcrypto_polarssl.c rfbssl_gnutls.c rfbcrypto_included.c
# rfbssl_none.c

SRCS = auth.c cargs.c corre.c cursor.c cutpaste.c draw.c font.c hextile.c \
       httpd.c main.c rfbregion.c rfbserver.c \
       rre.c scale.c selbox.c \
       sockets.c stats.c \
       tight.c translate.c ultra.c websockets.c ws_decode.c zlib.c zrle.c \
       zrlepalettehelper.c zrleoutstream.c zrlepalettehelper.c \
       ../common/d3des.c ../common/vncauth.c ../common/minilzo.c \
       ../common/turbojpeg.c ../common/sha1.c \
       rfbssl_openssl.c ../common/rfbcrypto_openssl.c

#       rfbssl_gnutls.c ../common/rfbcrypto_gnutls.c
#       ../common/rfbcrypto_included.c \



# SRCS+= ./tightvnc-filetransfer/filelistinfo.c \
#        ./tightvnc-filetransfer/filetransfermsg.c \
#        ./tightvnc-filetransfer/handlefiletransferrequest.c \
#        ./tightvnc-filetransfer/rfbtightserver.c

OBJS = $(SRCS:.c=.o)

DEFS += -D_DIGI
# -D_DIGI_NO_TCP_NODELAY

CFLAGS = -I.. -I../common -I. $(DEFS) -march=pentium -pipe -O2

$(LIB): $(OBJS)
	$(AR) rc $(LIB) $(OBJS)

.c.obj: .AUTODEPEND
	$(CC) $(CFLAGS) -c $<

clean:
	@rm -f $(OBJS) $(LIB)
