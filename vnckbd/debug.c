#include <conio.h>

#define COM1 0x3f8

static const char hextab[]="0123456789ABCDEF";

extern void cominit(void)
{
	outp(COM1 + 3, 0x83);  
	outp(COM1 + 0, 12);  
	outp(COM1 + 1, 0);  
	outp(COM1 + 3, 0x03);  
	outp(COM1 + 1, 0);  
	outp(COM1 + 2, 0);  
	outp(COM1 + 4, 0x03);  
	outp(COM1 + 5, 0x60);  
	outp(COM1 + 6, 0);  
}

static void comput(unsigned char c)
{
	unsigned char inbyte;

	inbyte = inp((0x3f8 + 0x05));
	while ((inbyte & 0x20) != 0x20) 
	{
//		iodelay(1);
		inbyte = inp((0x3f8 + 0x05));
	}
	outp(0x3f8, c);
}

void logstr (char far * message)
{
	char far *p = message;
	if (message == 0) 
	{
		return;
	}
	while (*p)
	{
		 comput(*p++);
	}
}

void logc (char c)
{
	comput(hextab[(c >> 4) & 0x0F]);
	comput(hextab[c & 0x0F]);
}

void logbuf (unsigned char far * buf, unsigned short len)
{
	int i;
	if (buf == 0) 
	{
		return;
	}
	for (i = 0; i < len; i++)
	{
		comput(' ');
		comput(hextab[(buf[i] >> 4) & 0x0F]);
		comput(hextab[buf[i] & 0x0F]);
	}
}

void logs (unsigned short value)
{
	comput('0');
	comput('x');
	comput(hextab[(value >> 12) & 0x0F]);
	comput(hextab[(value >> 8) & 0x0F]);
	comput(hextab[(value >> 4) & 0x0F]);
	comput(hextab[value & 0x0F]);
}

void logl (unsigned long value)
{
	comput('0');
	comput('x');
	comput(hextab[(value >> 28) & 0x0F]);
	comput(hextab[(value >> 24) & 0x0F]);
	comput(hextab[(value >> 20) & 0x0F]);
	comput(hextab[(value >> 16) & 0x0F]);
	comput(hextab[(value >> 12) & 0x0F]);
	comput(hextab[(value >> 8) & 0x0F]);
	comput(hextab[(value >> 4) & 0x0F]);
	comput(hextab[value & 0x0F]);
}
void logn (void)
{
	comput('\r');
	comput('\n');
}

#if 0
void logprintf(const char *fmtStr, ... )
{
	USHORT far *p = (USHORT far *)&fmtStr;
	UCHAR *fmt = (char FAR *)fmtStr;
	short lead0;
	short fWidth;
	short charCnt;

	if (!fmt)
		return;

	while (*fmt) {

		charCnt	=
		fWidth	=
		lead0	= 0;

		if (*fmt != '%') {
			comput(*fmt++);
			continue;
		}
		
		fmt++;
		p++;

formatLoop:
		switch (*fmt) {

			case '0':
				lead0++;
				fmt++;
				goto formatLoop;

			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				fWidth = (short)(*fmt - '9');
				/* no break */
			case '-':
				while (*fmt && ((*fmt >= '1' && *fmt <= '9') || *fmt == '-'))
					fmt++;
				goto formatLoop;

			case 'c':
				charCnt++;
				comput((UCHAR)*p);
				break;

			case 'd':
			{
				USHORT i = 10000;
				USHORT remainder;
				USHORT oneDigitSeen = 0;
				USHORT num = *p;
				for (; i; i /= 10) {
					remainder = num / i;
					if (remainder || oneDigitSeen || i==1) {
						charCnt++;
						comput((UCHAR)'0'+(UCHAR)remainder);
						num -= (remainder*i);
						oneDigitSeen++;
					}
					else if (lead0) {
						charCnt++;
						comput((UCHAR)'0');
					}
				}
				break;
			}

			case 's':
			{
				UCHAR FAR *s = (UCHAR FAR *)*p;
				while (*s) {
				 	charCnt++;
					comput(*s++);
				}
				break;
			}

			case 'x':
			{
				USHORT i = 0x1000;
				USHORT remainder;
				USHORT oneDigitSeen = 0;
				USHORT num = *p;
				for (; i; i/= 0x10) {
					remainder = num / i;
					if (remainder || oneDigitSeen || i==1) {
						charCnt++;
						comput( (UCHAR)remainder + (((UCHAR)remainder < 0x0a) ? '0' : ('a'-10)));
						num -= (remainder*i);
						oneDigitSeen++;
					}
					else if (lead0) {
						charCnt++;
						comput((UCHAR)'0');
					}
				}
				break;
			}

			case '\0':
				return;

			default:
				comput(*(fmt-1));
				comput(*fmt);
				charCnt = fWidth;
				p--;
				break;
		}
		for (;charCnt<fWidth;charCnt++)
			comput((UCHAR)' ');
		fmt++;
	}
	return;
}
#endif
