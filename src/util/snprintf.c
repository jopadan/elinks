/* Own portable snprintf() implementation */

/* These sources aren't the officially distributed version, they are modified
 * by us (ELinks coders) and some other third-party hackers. See ELinks
 * ChangeLog for details about changes we made here, comments bellow may give
 * you some indices as well. --pasky */

/*
 * Copyright Patrick Powell 1995
 * This code is based on code written by Patrick Powell (papowell@astart.com)
 * It may be used for any purpose as long as this notice remains intact
 * on all source code distributions
 */

/**************************************************************
 * Original:
 * Patrick Powell Tue Apr 11 09:48:21 PDT 1995
 * A bombproof version of doprnt (dopr) included.
 * Sigh.  This sort of thing is always nasty do deal with.  Note that
 * the version here does not include floating point...
 *
 * snprintf() is used instead of sprintf() as it does limit checks
 * for string length.  This covers a nasty loophole.
 *
 * The other functions are there to prevent NULL pointers from
 * causing nast effects.
 *
 * More Recently:
 *  Brandon Long <blong@fiction.net> 9/15/96 for mutt 0.43
 *  This was ugly.  It is still ugly.  I opted out of floating point
 *  numbers, but the formatter understands just about everything
 *  from the normal C string format, at least as far as I can tell from
 *  the Solaris 2.5 printf(3S) man page.
 *
 *  Brandon Long <blong@fiction.net> 10/22/97 for mutt 0.87.1
 *    Ok, added some minimal floating point support, which means this
 *    probably requires libm on most operating systems.  Don't yet
 *    support the exponent (e,E) and sigfig (g,G).  Also, fmtint()
 *    was pretty badly broken, it just wasn't being exercised in ways
 *    which showed it, so that's been fixed.  Also, formated the code
 *    to mutt conventions, and removed dead code left over from the
 *    original.  Also, there is now a builtin-test, just compile with:
 *           gcc -DTEST_SNPRINTF -o snprintf snprintf.c -lm
 *    and run snprintf for results.
 *
 *  Thomas Roessler <roessler@guug.de> 01/27/98 for mutt 0.89i
 *    The PGP code was using unsigned hexadecimal formats.
 *    Unfortunately, unsigned formats simply didn't work.
 *
 *  Michael Elkins <me@cs.hmc.edu> 03/05/98 for mutt 0.90.8
 *    The original code assumed that both snprintf() and vsnprintf() were
 *    missing.  Some systems only have snprintf() but not vsnprintf(), so
 *    the code is now broken down under HAVE_SNPRINTF and HAVE_VSNPRINTF.
 *
 * === Samba hacking:
 *
 *  Andrew Tridgell (tridge@samba.org) Oct 1998
 *    fixed handling of %.0f
 *    added test for HAVE_LONG_DOUBLE
 *
 * tridge@samba.org, idra@samba.org, April 2001
 *    got rid of fcvt code (twas buggy and made testing harder)
 *    added C99 semantics
 *
 * === About ELinks changes see ELinks' ChangeLog.
 *
 **************************************************************/

#ifdef HAVE_CONFIG_H /* for some tests */
#include "config.h"
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "elinks.h"

#include "util/snprintf.h"


/* #define TEST_SNPRINTF */ /* For compiling a standlone test binary. */


#if defined(HAVE_SNPRINTF) && defined(HAVE_VSNPRINTF) && defined(HAVE_C99_VSNPRINTF)

/* only include stdio.h if we are not re-defining snprintf or vsnprintf */
#include <stdio.h>
 /* make the compiler happy with an empty file */

void dummy_snprintf(void);
void dummy_snprintf(void) {}

#else /* defined(HAVE_SNPRINTF) && defined(HAVE_VSNPRINTF) && defined(HAVE_C99_VSNPRINTF) */

/* TODO: Don't do all this if we just need to get own snprintf() wrapper to
 * system-provided vsnprintf(). (What an unlikely case, isn't it?) */

#ifdef HAVE_LONG_DOUBLE
#define LDOUBLE long double
#else
#define LDOUBLE double
#endif

#ifdef HAVE_LONG_LONG
#define LLONG long long
#else
#define LLONG long
#endif

static size_t dopr(char *buffer, size_t maxlen, const char *format,
		   va_list args_in);
static void fmtstr(char *buffer, size_t *currlen, size_t maxlen,
		    char *value, int flags, int min, int max);
static void fmtint(char *buffer, size_t *currlen, size_t maxlen,
		    long value, int base, int min, int max, int flags);
static void fmtfp(char *buffer, size_t *currlen, size_t maxlen,
		   LDOUBLE fvalue, int min, int max, int flags);
static void dopr_outch(char *buffer, size_t *currlen, size_t maxlen, char c);

/*
 * dopr(): poor man's version of doprintf
 */

/* format read states */
#define DP_S_DEFAULT 0
#define DP_S_FLAGS   1
#define DP_S_MIN     2
#define DP_S_DOT     3
#define DP_S_MAX     4
#define DP_S_MOD     5
#define DP_S_CONV    6
#define DP_S_DONE    7

/* format flags - Bits */
#define DP_F_MINUS 	(1 << 0)
#define DP_F_PLUS  	(1 << 1)
#define DP_F_SPACE 	(1 << 2)
#define DP_F_NUM   	(1 << 3)
#define DP_F_ZERO  	(1 << 4)
#define DP_F_UP    	(1 << 5)
#define DP_F_UNSIGNED 	(1 << 6)

/* Conversion Flags */
#define DP_C_SHORT   1
#define DP_C_LONG    2
#define DP_C_LDOUBLE 3
#define DP_C_LLONG   4

#define char_to_int(p) ((p)- '0')

static char hexnumbers[] = "0123456789abcdef";
static char HEXnumbers[] = "0123456789ABCDEF";

static inline void
dopr_outch(char *buffer, size_t *currlen, size_t maxlen, char c)
{
	ELOG
	if (*currlen < maxlen)
		buffer[(*currlen)] = c;

	(*currlen)++;
}

static size_t
dopr(char *buffer, size_t maxlen, const char *format, va_list args_in)
{
	ELOG
	char ch;
	LLONG value;
	LDOUBLE fvalue;
	char *strvalue;
	int min;
	int max;
	int state;
	int flags;
	int cflags;
	size_t currlen;
	va_list args;

	va_copy(args, args_in);

	state = DP_S_DEFAULT;
	currlen = 0;
	flags = cflags = min = 0;
	max = -1;
	ch = *format++;

	while (state != DP_S_DONE) {
		if (ch == '\0')
			state = DP_S_DONE;

		switch (state) {
		case DP_S_DEFAULT:
			if (ch == '%')
				state = DP_S_FLAGS;
			else
				dopr_outch(buffer, &currlen, maxlen, ch);
			ch = *format++;
			break;
		case DP_S_FLAGS:
			switch (ch) {
			case '-':
				flags |= DP_F_MINUS;
				ch = *format++;
				break;
			case '+':
				flags |= DP_F_PLUS;
				ch = *format++;
				break;
			case ' ':
				flags |= DP_F_SPACE;
				ch = *format++;
				break;
			case '#':
				flags |= DP_F_NUM;
				ch = *format++;
				break;
			case '0':
				flags |= DP_F_ZERO;
				ch = *format++;
				break;
			default:
				state = DP_S_MIN;
				break;
			}
			break;
		case DP_S_MIN:
			if (isdigit((unsigned char) ch)) {
				min = 10 * min + char_to_int(ch);
				ch = *format++;
			} else if (ch == '*') {
				min = va_arg(args, int);
				ch = *format++;
				state = DP_S_DOT;
			} else {
				state = DP_S_DOT;
			}
			break;
		case DP_S_DOT:
			if (ch == '.') {
				state = DP_S_MAX;
				ch = *format++;
			} else {
				state = DP_S_MOD;
			}
			break;
		case DP_S_MAX:
			if (isdigit((unsigned char) ch)) {
				if (max < 0)
					max = 0;
				max = 10 * max + char_to_int(ch);
				ch = *format++;
			} else if (ch == '*') {
				max = va_arg(args, int);
				ch = *format++;
				state = DP_S_MOD;
			} else {
				state = DP_S_MOD;
			}
			break;
		case DP_S_MOD:
			switch (ch) {
			case 'h':
				cflags = DP_C_SHORT;
				ch = *format++;
				break;
			case 'l':
				cflags = DP_C_LONG;
				ch = *format++;
				if (ch == 'l') {	/* It's a long long */
					cflags = DP_C_LLONG;
					ch = *format++;
				}
				break;
			case 'L':
				cflags = DP_C_LDOUBLE;
				ch = *format++;
				break;
			default:
				break;
			}
			state = DP_S_CONV;
			break;
		case DP_S_CONV:
			switch (ch) {
			case 'd':
			case 'i':
				if (cflags == DP_C_SHORT)
					value = va_arg(args, int);
				else if (cflags == DP_C_LONG)
					value = va_arg(args, long);
				else if (cflags == DP_C_LLONG)
					value = va_arg(args, LLONG);
				else
					value = va_arg(args, int);
				fmtint(buffer, &currlen, maxlen, value, 10, min, max, flags);
				break;
			case 'o':
				flags |= DP_F_UNSIGNED;
				if (cflags == DP_C_SHORT)
					value = va_arg(args, unsigned int);
				else if (cflags == DP_C_LONG)
					value = (long) va_arg(args, unsigned long);
				else if (cflags == DP_C_LLONG)
					value = (long) va_arg(args, unsigned LLONG);
				else
					value = (long) va_arg(args, unsigned int);
				fmtint(buffer, &currlen, maxlen, value, 8, min, max, flags);
				break;
			case 'u':
				flags |= DP_F_UNSIGNED;
				if (cflags == DP_C_SHORT)
					value = va_arg(args, unsigned int);
				else if (cflags == DP_C_LONG)
					value = (long) va_arg(args, unsigned long);
				else if (cflags == DP_C_LLONG)
					value = (LLONG) va_arg(args, unsigned LLONG);
				else
					value = (long) va_arg(args, unsigned int);
				fmtint(buffer, &currlen, maxlen, value, 10, min, max, flags);
				break;
			case 'X':
				flags |= DP_F_UP;
			case 'x':
				flags |= DP_F_UNSIGNED;
				if (cflags == DP_C_SHORT)
					value = va_arg(args, unsigned int);
				else if (cflags == DP_C_LONG)
					value = (long) va_arg(args, unsigned long);
				else if (cflags == DP_C_LLONG)
					value = (LLONG) va_arg(args, unsigned LLONG);
				else
					value = (long) va_arg(args, unsigned int);
				fmtint(buffer, &currlen, maxlen, value, 16, min, max, flags);
				break;
			case 'f':
				if (cflags == DP_C_LDOUBLE)
					fvalue = va_arg(args, LDOUBLE);
				else
					fvalue = va_arg(args, double);
				/* um, floating point? */
				fmtfp(buffer, &currlen, maxlen, fvalue, min, max, flags);
				break;
			case 'E':
				flags |= DP_F_UP;
			case 'e':
				if (cflags == DP_C_LDOUBLE)
					fvalue = va_arg(args, LDOUBLE);
				else
					fvalue = va_arg(args, double);
				fmtfp(buffer, &currlen, maxlen, fvalue, min, max, flags);
				break;
			case 'G':
				flags |= DP_F_UP;
			case 'g':
				if (cflags == DP_C_LDOUBLE)
					fvalue = va_arg(args, LDOUBLE);
				else
					fvalue = va_arg(args, double);
				fmtfp(buffer, &currlen, maxlen, fvalue, min, max, flags);
				break;
			case 'c':
				dopr_outch(buffer, &currlen, maxlen, va_arg(args, int));
				break;
			case 's':
				strvalue = va_arg(args, char *);
				if (!strvalue) strvalue = (char *)"(NULL)";
				if (max == -1) {
					max = strlen(strvalue);
				}
				if (min > 0 && max >= 0 && min > max) max = min;
				fmtstr(buffer, &currlen, maxlen, strvalue, flags, min, max);
				break;
			case 'p':
				strvalue = (char *)va_arg(args, void *);
				fmtint(buffer, &currlen, maxlen, (long) strvalue, 16, min, max, flags);
				break;
			case 'n':
				if (cflags == DP_C_SHORT) {
					short *num;

					num = va_arg(args, short *);
					*num = currlen;
				} else if (cflags == DP_C_LONG) {
					long *num;

					num = va_arg(args, long *);
					*num = (long) currlen;
				} else if (cflags == DP_C_LLONG) {
					LLONG *num;

					num = va_arg(args, LLONG *);
					*num = (LLONG) currlen;
				} else {
					int *num;

					num = va_arg(args, int *);
					*num = currlen;
				}
				break;
			case '%':
				dopr_outch(buffer, &currlen, maxlen, ch);
				break;
			case 'w':
				/* not supported yet, treat as next char */
				ch = *format++;
				break;
			default:
				/* Unknown, skip */
				break;
			}
			ch = *format++;
			state = DP_S_DEFAULT;
			flags = cflags = min = 0;
			max = -1;
			break;
		case DP_S_DONE:
			break;
		default:
			/* hmm? */
			break; /* some picky compilers need this */
		}
	}
	if (maxlen != 0) {
		if (currlen < maxlen - 1)
			buffer[currlen] = '\0';
		else if (maxlen > 0)
			buffer[maxlen - 1] = '\0';
	}

	return currlen;
}

static void
fmtstr(char *buffer, size_t *currlen, size_t maxlen,
       char *value, int flags, int min, int max)
{
	ELOG
	int padlen, strln;     /* amount to pad */
	int cnt = 0;

#ifdef DEBUG_SNPRINTF
	printf("fmtstr min=%d max=%d s=[%s]\n", min, max, value);
#endif
	if (value == 0) value = (char *)"<NULL>";

	for (strln = 0; value[strln]; ++strln); /* strlen */
	padlen = min - strln;
	if (padlen < 0)
		padlen = 0;
	if (flags & DP_F_MINUS)
		padlen = -padlen; /* Left Justify */

	while ((padlen > 0) && (cnt < max)) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		--padlen;
		++cnt;
	}
	while (*value && (cnt < max)) {
		dopr_outch(buffer, currlen, maxlen, *value++);
		++cnt;
	}
	while ((padlen < 0) && (cnt < max)) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		++padlen;
		++cnt;
	}
}

/* Have to handle DP_F_NUM (ie 0x and 0 alternates) */
static void
fmtint(char *buffer, size_t *currlen, size_t maxlen,
       long value, int base, int min, int max, int flags)
{
	ELOG
	int signvalue = 0;
	unsigned long uvalue;
	char convert[20];
	char *numbers = (char *) &hexnumbers;
	int place = 0;
	int spadlen = 0; /* amount to space pad */
	int zpadlen = 0; /* amount to zero pad */

	if (max < 0)
		max = 0;

	uvalue = value;

	if (!(flags & DP_F_UNSIGNED)) {
		if (value < 0) {
			signvalue = '-';
			uvalue = -value;
		} else {
			if (flags & DP_F_PLUS)  /* Do a sign (+/i) */
				signvalue = '+';
			else if (flags & DP_F_SPACE)
				signvalue = ' ';
		}
	}

	if (flags & DP_F_UP) {
		 /* Should characters be upper case? */
		numbers = (char *) &HEXnumbers;
	}

	do {
		convert[place++] = numbers[uvalue % (unsigned) base];
		uvalue = (uvalue / (unsigned) base);
	} while (uvalue && (place < 20));
	if (place == 20) place--;
	convert[place] = 0;

	zpadlen = max - place;
	spadlen = min - MAX(max, place) - (signvalue ? 1 : 0);
	if (zpadlen < 0) zpadlen = 0;
	if (spadlen < 0) spadlen = 0;
	if (flags & DP_F_ZERO) {
		zpadlen = MAX(zpadlen, spadlen);
		spadlen = 0;
	}
	if (flags & DP_F_MINUS)
		spadlen = -spadlen; /* Left Justifty */

#ifdef DEBUG_SNPRINTF
	printf("zpad: %d, spad: %d, min: %d, max: %d, place: %d\n",
	       zpadlen, spadlen, min, max, place);
#endif

	/* Spaces */
	while (spadlen > 0) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		--spadlen;
	}

	/* Sign */
	if (signvalue)
		dopr_outch(buffer, currlen, maxlen, signvalue);

	/* Zeros */
	while (zpadlen > 0) {
		dopr_outch(buffer, currlen, maxlen, '0');
		--zpadlen;
	}

	/* Digits */
	while (place > 0)
		dopr_outch(buffer, currlen, maxlen, convert[--place]);

	/* Left Justified spaces */
	while (spadlen < 0) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		++spadlen;
	}
}

static LDOUBLE
my_abs(LDOUBLE value)
{
	ELOG
	LDOUBLE result = value;

	if (value < 0)
		result = -value;

	return result;
}

static LDOUBLE
my_pow10(int exp)
{
	ELOG
	LDOUBLE result = 1;

	while (exp) {
		result *= 10;
		exp--;
	}

	return result;
}

static LLONG
my_round(LDOUBLE value)
{
	ELOG
	LLONG intpart;

	intpart = (LLONG) value;
	value = value - intpart;
	if (value >= 0.5) intpart++;

	return intpart;
}

/* a replacement for modf that doesn't need the math library. Should
   be portable, but slow */
static double
my_modf(double x0, double *iptr)
{
	ELOG
	int i;
	long l;
	double x = x0;
	double f = 1.0;

	for (i = 0; i < 100; i++) {
		l = (long) x;
		if (l <= (x + 1) && l >= (x - 1)) break;
		x *= 0.1;
		f *= 10.0;
	}

	if (i == 100) {
		/* yikes! the number is beyond what we can handle. What do we do? */
		(*iptr) = 0;
		return 0;
	}

	if (i != 0) {
		double i2;
		double ret;

		ret = my_modf(x0 - l * f, &i2);
		(*iptr) = l * f + i2;
		return ret;
	}

	(*iptr) = l;
	return x - (*iptr);
}

static void
fmtfp(char *buffer, size_t *currlen, size_t maxlen,
      LDOUBLE fvalue, int min, int max, int flags)
{
	ELOG
	int signvalue = 0;
	double ufvalue;
	char iconvert[311];
	char fconvert[311];
	char *numbers = (char *) &hexnumbers;
	int iplace = 0;
	int fplace = 0;
	int padlen = 0; /* amount to pad */
	int zpadlen = 0;
	int index;
	double intpart;
	double fracpart;
	double temp;
	LDOUBLE pow;

	/*
	 * AIX manpage says the default is 0, but Solaris says the default
	 * is 6, and sprintf on AIX defaults to 6
	 */
	if (max < 0)
		max = 6;

	ufvalue = my_abs(fvalue);

	if (fvalue < 0) {
		signvalue = '-';
	} else {
		if (flags & DP_F_PLUS) { /* Do a sign (+/i) */
			signvalue = '+';
		} else {
			if (flags & DP_F_SPACE)
				signvalue = ' ';
		}
	}

#if 0
	if (flags & DP_F_UP) {
		caps = 1; /* Should characters be upper case? */
		numbers = (char *) &HEXnumbers;
	}
#endif

#if 0
	 if (max == 0) ufvalue += 0.5; /* if max = 0 we must round */
#endif

	/*
	 * Sorry, we only support 16 digits past the decimal because of our
	 * conversion method
	 */
	if (max > 16)
		max = 16;

	pow = my_pow10(max);

	/* We "cheat" by converting the fractional part to integer by
	 * multiplying by a factor of 10
	 */

	temp = ufvalue;
	my_modf(temp, &intpart);

	fracpart = my_round(pow * (ufvalue - intpart));

	if (fracpart >= pow) {
		intpart++;
		fracpart -= pow;
	}

	/* Convert integer part */
	do {
		temp = intpart * 0.1;
		my_modf(temp, &intpart);
		index = (int) ((temp - intpart + 0.05) * 10.0);
#if 0
		index = (int) (((double) (temp*0.1) -intpart +0.05) *10.0);
		printf ("%llf, %f, %x\n", temp, intpart, index);
#endif
		iconvert[iplace++] = numbers[index];
	} while (intpart && (iplace < 311));
	if (iplace == 311) iplace--;
	iconvert[iplace] = 0;

	/* Convert fractional part */
	if (fracpart) {
		do {
			temp = fracpart * 0.1;
			my_modf(temp, &fracpart);
			index = (int) ((temp - fracpart + 0.05) * 10.0);
#if 0
			index = (int) ((((temp/10) -fracpart) +0.05) *10);
			printf ("%lf, %lf, %ld\n", temp, fracpart, index);
#endif
			fconvert[fplace++] = numbers[index];
		} while (fracpart && (fplace < 311));
		if (fplace == 311) fplace--;
	}
	fconvert[fplace] = 0;

	/* -1 for decimal point, another -1 if we are printing a sign */
	padlen = min - iplace - max - 1 - ((signvalue) ? 1 : 0);
	zpadlen = max - fplace;
	if (zpadlen < 0) zpadlen = 0;
	if (padlen < 0)
		padlen = 0;
	if (flags & DP_F_MINUS)
		padlen = -padlen; /* Left Justifty */

	if ((flags & DP_F_ZERO) && (padlen > 0)) {
		if (signvalue) {
			dopr_outch(buffer, currlen, maxlen, signvalue);
			--padlen;
			signvalue = 0;
		}
		while (padlen > 0) {
			dopr_outch(buffer, currlen, maxlen, '0');
			--padlen;
		}
	}
	while (padlen > 0) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		--padlen;
	}
	if (signvalue)
		dopr_outch(buffer, currlen, maxlen, signvalue);

	while (iplace > 0)
		dopr_outch(buffer, currlen, maxlen, iconvert[--iplace]);

#ifdef DEBUG_SNPRINTF
	printf("fmtfp: fplace=%d zpadlen=%d\n", fplace, zpadlen);
#endif

	/*
	 * Decimal point.  This should probably use locale to find the correct
	 * char to print out.
	 */
	if (max > 0) {
		dopr_outch(buffer, currlen, maxlen, '.');

		while (zpadlen > 0) {
			dopr_outch(buffer, currlen, maxlen, '0');
			--zpadlen;
		}

		while (fplace > 0)
			dopr_outch(buffer, currlen, maxlen, fconvert[--fplace]);
	}

	while (padlen < 0) {
		dopr_outch(buffer, currlen, maxlen, ' ');
		++padlen;
	}
}


/* Yes the following preprocessor conditions really must be a ||. Don't muck
 * with this. -- tridge
 *
 * The logic for these is that we need our own definition if the OS *either*
 * has no definition of *sprintf, or if it does have one that doesn't work
 * properly according to the autoconf test. -- mbp */

#if !defined(HAVE_VSNPRINTF) || !defined(HAVE_C99_VSNPRINTF)
int
elinks_vsnprintf(char *str, size_t count, const char *fmt, va_list args)
{
	ELOG
	return dopr(str, count, fmt, args);
}
#endif

#if !defined(HAVE_SNPRINTF) || !defined(HAVE_C99_VSNPRINTF)
int
elinks_snprintf(char *str, size_t count, const char *fmt, ...)
{
	ELOG
	size_t ret;
	va_list ap;

	va_start(ap, fmt);
	/* snprintf.h makes this to expand to the right thing. */
	ret = vsnprintf(str, count, fmt, ap);
	va_end(ap);

	return ret;
}
#endif

#endif /* defined(HAVE_SNPRINTF) && defined(HAVE_VSNPRINTF) && defined(HAVE_C99_VSNPRINTF) */


#ifndef HAVE_VASPRINTF
int
elinks_vasprintf(char **ptr, const char *format, va_list ap)
{
	ELOG
	int ret;
	va_list ap2;

	va_copy(ap2, ap);

	ret = vsnprintf(NULL, 0, format, ap2);
	if (ret <= 0) return ret;

	/* Should we use mem_alloc() here instead of malloc() ??? --Zas */
	/* No: This is an alternative to the system vasprintf.
	 * When the system provides vasprintf, it will use malloc(),
	 * which means that the string will need to be freed with free().
	 * If we did use mem_alloc here in our alternative to the system
	 * vasprintf, we would need to change all users of the memory
	 * allocated by vasprintf to take into account whether the system
	 * vasprintf (and therefore malloc) or our own vasprintf
	 * (and therefore mem_alloc) were used. -- Miciah */
	(*ptr) = (char *)malloc(ret + 1);
	if (!*ptr) return -1;

	va_copy(ap2, ap);

	return vsnprintf(*ptr, ret + 1, format, ap2);
}
#endif


#ifndef HAVE_ASPRINTF
int
elinks_asprintf(char **ptr, const char *format, ...)
{
	ELOG
	va_list ap;
	int ret;

	*ptr = NULL;
	va_start(ap, format);
	ret = vasprintf(ptr, format, ap);
	va_end(ap);

	return ret;
}
#endif

char *
asprintfa(const char *fmt, ...)
{
	ELOG
	char *str;
	va_list ap;

	va_start(ap, fmt);
	str = vasprintfa(fmt, ap);
	va_end(ap);

	return str;
}

/* Test program */
#ifdef TEST_SNPRINTF

#include <math.h>

int sprintf(char *str,const char *fmt,...);

int
main(void)
{
	ELOG
	char buf1[1024];
	char buf2[1024];
	char *fp_fmt[] = {
		"%1.1f",
		"%-1.5f",
		"%1.5f",
		"%123.9f",
		"%10.5f",
		"% 10.5f",
		"%+22.9f",
		"%+4.9f",
		"%01.3f",
		"%4f",
		"%3.1f",
		"%3.2f",
		"%.0f",
		"%f",
		"-16.16f",
		NULL
	};
	double fp_nums[] = { 6442452944.1234, -1.5, 134.21, 91340.2,
			     341.1234, 0203.9, 0.96, 0.996,
			     0.9996, 1.996, 4.136, 5.030201, 0};
	char *int_fmt[] = {
		"%-1.5d",
		"%1.5d",
		"%123.9d",
		"%5.5d",
		"%10.5d",
		"% 10.5d",
		"%+22.33d",
		"%01.3d",
		"%4d",
		"%d",
		NULL
	};
	long int_nums[] = { -1, 134, 91340, 341, 0203, 0};
	char *str_fmt[] = {
		"10.5s",
		"5.10s",
		"10.1s",
		"0.10s",
		"10.0s",
		"1.10s",
		"%s",
		"%.1s",
		"%.10s",
		"%10s",
		NULL
	};
	char *str_vals[] = {"hello", "a", "", "a longer string", NULL};
	int x, y;
	int fail = 0;
	int num = 0;

	printf ("Testing snprintf format codes against system sprintf...\n");

	for (x = 0; fp_fmt[x] ; x++) {
		for (y = 0; fp_nums[y] != 0 ; y++) {
			int l1 = snprintf(NULL, 0, fp_fmt[x], fp_nums[y]);
			int l2 = snprintf(buf1, sizeof(buf1), fp_fmt[x], fp_nums[y]);

			sprintf(buf2, fp_fmt[x], fp_nums[y]);
			if (strcmp(buf1, buf2)) {
				printf("snprintf doesn't match Format: %s\n\t"
				       "snprintf = [%s]\n\t sprintf = [%s]\n",
				       fp_fmt[x], buf1, buf2);
				fail++;
			}
			if (l1 != l2) {
				printf("snprintf l1 != l2 (%d %d) %s\n", l1, l2, fp_fmt[x]);
				fail++;
			}
			num++;
		}
	}

	for (x = 0; int_fmt[x] ; x++) {
		for (y = 0; int_nums[y] != 0 ; y++) {
			int l1 = snprintf(NULL, 0, int_fmt[x], int_nums[y]);
			int l2 = snprintf(buf1, sizeof(buf1), int_fmt[x], int_nums[y]);

			sprintf(buf2, int_fmt[x], int_nums[y]);
			if (strcmp(buf1, buf2)) {
				printf("snprintf doesn't match Format: %s\n\t"
				       "snprintf = [%s]\n\t sprintf = [%s]\n",
				       int_fmt[x], buf1, buf2);
				fail++;
			}
			if (l1 != l2) {
				printf("snprintf l1 != l2 (%d %d) %s\n", l1, l2, int_fmt[x]);
				fail++;
			}
			num++;
		}
	}

	for (x = 0; str_fmt[x] ; x++) {
		for (y = 0; str_vals[y] != 0 ; y++) {
			int l1 = snprintf(NULL, 0, str_fmt[x], str_vals[y]);
			int l2 = snprintf(buf1, sizeof(buf1), str_fmt[x], str_vals[y]);

			sprintf(buf2, str_fmt[x], str_vals[y]);
			if (strcmp(buf1, buf2)) {
				printf("snprintf doesn't match Format: %s\n\t"
				       "snprintf = [%s]\n\t sprintf = [%s]\n",
				       str_fmt[x], buf1, buf2);
				fail++;
			}
			if (l1 != l2) {
				printf("snprintf l1 != l2 (%d %d) %s\n", l1, l2, str_fmt[x]);
				fail++;
			}
			num++;
		}
	}

	printf ("%d tests failed out of %d.\n", fail, num);

	printf("seeing how many digits we support\n");
	{
		double v0 = 0.12345678901234567890123456789012345678901;

		for ( x = 0; x < 100; x++) {
			double p = pow(10, x);
			double r = v0*p;

			snprintf(buf1, sizeof(buf1), "%1.1f", r);
			sprintf(buf2,"%1.1f", r);
			if (strcmp(buf1, buf2)) {
				printf("we seem to support %d digits\n", x-1);
				break;
			}
		}
	}

	return 0;
}
#endif /* SNPRINTF_TEST */
