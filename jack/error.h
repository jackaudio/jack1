#ifndef __jack_error_h__
#define __jack_error_h__

extern void (*jack_error)(const char *fmt, ...);
void jack_set_error_function (void (*func)(const char *, ...));


#endif /* __jack_error_h__ */
