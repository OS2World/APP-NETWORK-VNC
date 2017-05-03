#ifdef DEBUG_CODE

void cominit(void);
void logstr(char far * message);
void logc(char c);
void logbuf(unsigned char far * buf, unsigned short len);
void logs(unsigned short value);
void logl(unsigned long value);
void logn(void);

#else

#define cominit()
#define logstr(msg)
#define logc(c)
#define logbuf(buf, len)
#define logs(value)
#define logl(value)
#define logn()

#endif
