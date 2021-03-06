/*
 * Copyright (c) Ian F. Darwin 1986-1995.
 * Software written by Ian F. Darwin and others;
 * maintained 1995-present by Christos Zoulas and others.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * file - find type of a file or files - main program.
 */

#include "file.h"

#ifndef	lint
FILE_RCSID("@(#)$File: file.c,v 1.144 2011/05/10 17:08:14 christos Exp $")
#endif	/* lint */

#include "magic.h"

#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#ifdef RESTORE_TIME
# if (__COHERENT__ >= 0x420)
#  include <sys/utime.h>
# else
#  ifdef USE_UTIMES
#   include <sys/time.h>
#  else
#   include <utime.h>
#  endif
# endif
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>	/* for read() */
#endif
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_WCHAR_H
#include <wchar.h>
#endif

#if defined(HAVE_GETOPT_H) && defined(HAVE_STRUCT_OPTION)
#include <getopt.h>
#ifndef HAVE_GETOPT_LONG
int getopt_long(int argc, char * const *argv, const char *optstring, const struct option *longopts, int *longindex);
#endif
#else
#include "mygetopt.h"
#endif

/* 如果系统支持符号链接 */
#ifdef S_IFLNK
#define FILE_FLAGS "-bchikLlNnprsvzt0"
#else
#define FILE_FLAGS "-bciklNnprsvzt0"
#endif

#define USAGE  \
    "Usage: %s [" FILE_FLAGS \
	"] [--apple] [--mime-encoding] [--mime-type]\n" \
    "            [-e testname] [-F separator] [-f namefile] [-m magicfiles]\n" \
    "            [-t typesfile] "\
    "file ...\n" \
    "       %s -C [-m magicfiles]\n" \
    "       %s [--help]\n"

private int 		/* Global command-line options 		*/
	bflag = 0,	/* brief output format	 		*/
	nopad = 0,	/* Don't pad output			*/
	nobuffer = 0,   /* Do not buffer stdout 		*/
	nulsep = 0;	/* Append '\0' to the separator		*/

private const char *separator = ":";	/* Default field separator	*/

/* 长参数返回短参数，只有长参数返回0 */
private const struct option long_options[] = {
#define OPT(shortname, longname, opt, doc)      \
    {longname, opt, NULL, shortname},
#define OPT_LONGONLY(longname, opt, doc)        \
    {longname, opt, NULL, 0},
#include "file_opts.h"
#undef OPT
#undef OPT_LONGONLY
    {0, 0, NULL, 0}
};

#define OPTSTRING	"bcCde:f:F:hiklLm:nNprsvzt:0"

/* 支持的测试选项 */
private const struct {
	const char *name;
	int value;
} nv[] = {
	{ "apptype",	MAGIC_NO_CHECK_APPTYPE },
	{ "ascii",	MAGIC_NO_CHECK_ASCII },
	{ "cdf",	MAGIC_NO_CHECK_CDF },
	{ "compress",	MAGIC_NO_CHECK_COMPRESS },
	{ "elf",	MAGIC_NO_CHECK_ELF },
	{ "encoding",	MAGIC_NO_CHECK_ENCODING },
	{ "soft",	MAGIC_NO_CHECK_SOFT },
	{ "tar",	MAGIC_NO_CHECK_TAR },
	{ "text",	MAGIC_NO_CHECK_TEXT },	/* synonym for ascii */
	{ "tokens",	MAGIC_NO_CHECK_TOKENS },
};

/* specify types file that vas needed */
private char* types_file;

private char *progname;		/* used throughout */

private void usage(void);
private void help(void);
int main(int, char *[]);

private int unwrap(struct magic_set *, const char *);
private int process(struct magic_set *ms, const char *, int);
private struct magic_set *load(const char *, int);


/*
 * main - parse arguments and handle options
 */
int
main(int argc, char *argv[])
{
	int c; /* 选项字符 */
	size_t i; /* 计数 */
	int action = 0, didsomefiles = 0, errflg = 0;
	int flags = 0, e = 0;
	struct magic_set *magic = NULL;
	int longindex; /*长参数在参数数组中的位置 */
	const char *magicfile = NULL;		/* where the magic is	*/

	/* makes islower etc work for other langs */
	(void)setlocale(LC_CTYPE, "");

	#ifdef __EMX__
	/* sh-like wildcard expansion! Shouldn't hurt at least ... */
	_wildcard(&argc, &argv);
	#endif

	/* 取得程序名称‘file’ */
	if ((progname = strrchr(argv[0], '/')) != NULL)
		progname++;
	else
		progname = argv[0];

	#ifdef S_IFLNK
	flags |= getenv("POSIXLY_CORRECT") ? MAGIC_SYMLINK : 0;
	#endif
	while ((c = getopt_long(argc, argv, OPTSTRING, long_options, &longindex)) != -1)
		switch (c) {
		case 0 :
			/* 根据长选项在选项数组中的位置 */
			switch (longindex) {
			case 0:
				help();
				break;
			case 10:
				flags |= MAGIC_APPLE;
				break;
			case 11:
				flags |= MAGIC_MIME_TYPE;
				break;
			case 12:
				flags |= MAGIC_MIME_ENCODING;
				break;
			}
			break;
		case '0':
			nulsep = 1;
			break;
        case 't':
            types_file = optarg;
            break;
		case 'b':
			bflag++;
			break;
		case 'c':
			action = FILE_CHECK;
			break; case 'C':
			action = FILE_COMPILE;
			break;
		case 'd':
			flags |= MAGIC_DEBUG|MAGIC_CHECK;
			break;
		case 'e':
			for (i = 0; i < sizeof(nv) / sizeof(nv[0]); i++)
				if (strcmp(nv[i].name, optarg) == 0)
					break;

			if (i == sizeof(nv) / sizeof(nv[0]))
				errflg++;
			else
				flags |= nv[i].value;
			break;

		case 'f':
			if(action)
				usage();
			if (magic == NULL)
				if ((magic = load(magicfile, flags)) == NULL)
					return 1;
			e |= unwrap(magic, optarg);
			++didsomefiles;
			break;
		case 'F':
			separator = optarg;
			break;
		case 'i':
			flags |= MAGIC_MIME;
			break;
		case 'k':
			flags |= MAGIC_CONTINUE;
			break;
		case 'l':
			action = FILE_LIST;
			break;
		case 'm':
			magicfile = optarg;
			break;
		case 'n':
			++nobuffer;
			break;
		case 'N':
			++nopad;
			break;
		#if defined(HAVE_UTIME) || defined(HAVE_UTIMES)
		case 'p':
			flags |= MAGIC_PRESERVE_ATIME;
			break;
		#endif
		case 'r':
			flags |= MAGIC_RAW;
			break;
		case 's':
			flags |= MAGIC_DEVICES;
			break;
		case 'v':
			if (magicfile == NULL)
				magicfile = magic_getpath(magicfile, action);
			(void)fprintf(stdout, "%s-%s\n", progname, VERSION);
			(void)fprintf(stdout, "magic file from %s\n",
				       magicfile);
			return 1;
		case 'z':
			flags |= MAGIC_COMPRESS;
			break;
		#ifdef S_IFLNK
		case 'L':
			flags |= MAGIC_SYMLINK;
			break;
		case 'h':
			flags &= ~MAGIC_SYMLINK;
			break;
		#endif
		case '?':
		default:
			errflg++;
			break;
		}

	if (errflg) {
		usage();
	}
	if (e)
		return e;

	/**
	 * 根据action进行不同的magic操作
	 * 没有magic操作时，默认获取magic_set用于后面的处理
	 */
	switch(action) {
	case FILE_CHECK:
	case FILE_COMPILE:
	case FILE_LIST:
		/*
		 * Don't try to check/compile ~/.magic unless we explicitly
		 * ask for it.
		 */
		magic = magic_open(flags|MAGIC_CHECK);
		if (magic == NULL) {
			(void)fprintf(stderr, "%s: %s\n", progname,
			    strerror(errno));
			return 1;
		}
		switch(action) {
		case FILE_CHECK:
			c = magic_check(magic, magicfile);
			break;
		case FILE_COMPILE:
			c = magic_compile(magic, magicfile);
			break;
		case FILE_LIST:
			c = magic_list(magic, magicfile);
			break;
		default:
			abort();
		}
		if (c == -1) {
			(void)fprintf(stderr, "%s: %s\n", progname,
			    magic_error(magic));
			return 1;
		}
		return 0;
	default:
		if (magic == NULL)
			if ((magic = load(magicfile, flags)) == NULL)
				return 1;
		break;
	}

	if (optind == argc) {
		if (!didsomefiles)
			usage();
	}
	else {
		size_t j, wid, nw;
        /* get max file name length for tab, make the print info align */
		for (wid = 0, j = (size_t)optind; j < (size_t)argc; j++) {
			nw = file_mbswidth(argv[j]);
			if (nw > wid)
				wid = nw;
		}
		/*
		 * If bflag is only set twice, set it depending on
		 * number of files [this is undocumented, and subject to change]
		 */
		if (bflag == 2) {
			bflag = optind >= argc - 1;
		}
		for (; optind < argc; optind++)
			e |= process(magic, argv[optind], wid);
	}

	if (magic)
		magic_close(magic);
	return e;
}


private struct magic_set *
/*ARGSUSED*/
load(const char *magicfile, int flags)
{
	struct magic_set *magic = magic_open(flags);
	if (magic == NULL) {
		(void)fprintf(stderr, "%s: %s\n", progname, strerror(errno));
		return NULL;
	}
	if (magic_load(magic, magicfile) == -1) {
		(void)fprintf(stderr, "%s: %s\n",
		    progname, magic_error(magic));
		magic_close(magic);
		return NULL;
	}
	return magic;
}

/*
 * unwrap -- read a file of filenames, do each one.
 */
private int
unwrap(struct magic_set *ms, const char *fn)
{
	FILE *f;
	ssize_t len;
	char *line = NULL;
	size_t llen = 0;
	int wid = 0, cwid;
	int e = 0;

	if (strcmp("-", fn) == 0) {
		f = stdin;
		wid = 1;
	} else {
		if ((f = fopen(fn, "r")) == NULL) {
			(void)fprintf(stderr, "%s: Cannot open `%s' (%s).\n",
			    progname, fn, strerror(errno));
			return 1;
		}

		while ((len = getline(&line, &llen, f)) > 0) {
			if (line[len - 1] == '\n')
				line[len - 1] = '\0';
			cwid = file_mbswidth(line);
			if (cwid > wid)
				wid = cwid;
		}

		rewind(f);
	}

	while ((len = getline(&line, &llen, f)) > 0) {
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		e |= process(ms, line, wid);
		if(nobuffer)
			(void)fflush(stdout);
	}

	free(line);
	(void)fclose(f);
	return e;
}

/**
 * 判断是否是vas需要的文件类型
 */
int type_vas_need(char* type) {
    
    char temp[15];

    FILE *f;

    if (types_file == NULL) {
        if ((f = fopen("types.conf", "r")) == NULL) {
            printf("Cann't open file! Types file error!");
            return 0;
        }           
    } else {
         if ((f = fopen(types_file, "r")) == NULL) {
            printf("Cann't open file! Types file error!");
            return 0;
        } 
    }

    while (!feof(f)) {
        fgets(temp, 15, f);
        /* change the '\n' at the end of each line to '\0' */
        temp[strlen(temp) - 1] = '\0';
        if (!strcmp(type, temp))
            return 1;
    }

    fclose(f);

    return 0;
}

/**
 * vasunknown: HTML document, ISO-8859 text, with very long lines
 *
 * vasscript: ASCII text, with CRLF line terminators
 *      There's "text" before the first comma.   
 * @Para inname filename
 */
int vas_script_type(char* inname) {

    FILE *fp;
    char buffer[255];

    /* call the file command of the system(file filename) */
    char command[255] = "file -b ";
//    char command = (char*)malloc
  
    strcat(command, inname);

    fp = popen(command, "r");

    fgets(buffer, sizeof(buffer), fp);

    char* type = strtok(buffer, ",");

    if(strstr(type, "text"))
        return 1;
    else
        return 0;
}

/*
 * Called for each input file on the command line (or in a list of files)
 */
private int
process(struct magic_set *ms, const char *inname, int wid)
{   
	const char *type;
	int std_in = strcmp(inname, "-") == 0;

	if (wid > 0 && !bflag) {
		(void)printf("%s", std_in ? "/dev/stdin" : inname);
		if (nulsep)
			(void)putc('\0', stdout);
		(void)printf("%s", separator);
		(void)printf("%*s ",
		    (int) (nopad ? 0 : (wid - file_mbswidth(inname))), "");
	}

	/* get file type */
	type = magic_file(ms, std_in ? NULL : inname);

    if (type == NULL) {
	    (void)printf("ERROR: %s\n", magic_error(ms));
		return 1;
	} 
        
    /** 
     * there be 2 judgement
     * first: if exe, dll or sys, end
     *      if not these type, type is vasunknown
     * second: if type is vasunknown, and if the new type is vasscript
     *      type is vasscript
     */
    if (!type_vas_need(type)) {
        type = "vasunknown";
    } 
    
    if(type == "vasunknown") {
        if(vas_script_type(inname)) {
            type = "vasscript";
        }
    }

    (void)printf("%s\n", type);
    return 0;
}

/**
 * get file name length
 */
size_t
file_mbswidth(const char *s)
{
#if defined(HAVE_WCHAR_H) && defined(HAVE_MBRTOWC) && defined(HAVE_WCWIDTH)
	size_t bytesconsumed, old_n, n, width = 0;
	mbstate_t state;
	wchar_t nextchar;
	(void)memset(&state, 0, sizeof(mbstate_t));
	old_n = n = strlen(s);

	while (n > 0) {
		bytesconsumed = mbrtowc(&nextchar, s, n, &state);
		if (bytesconsumed == (size_t)(-1) ||
		    bytesconsumed == (size_t)(-2)) {
			/* Something went wrong, return something reasonable */
			return old_n;
		}
		if (s[0] == '\n') {
			/*
			 * do what strlen() would do, so that caller
			 * is always right
			 */
			width++;
		} else
			width += wcwidth(nextchar);

		s += bytesconsumed, n -= bytesconsumed;
	}
	return width;
#else
	return strlen(s);
#endif
}

private void
usage(void)
{
	(void)fprintf(stderr, USAGE, progname, progname, progname);
	exit(1);
}

private void
help(void)
{
	(void)fputs(
"Usage: file [OPTION...] [FILE...]\n"
"Determine type of FILEs.\n"
"\n", stdout);
#define OPT(shortname, longname, opt, doc)      \
	fprintf(stdout, "  -%c, --" longname doc, shortname);
#define OPT_LONGONLY(longname, opt, doc)        \
	fprintf(stdout, "      --" longname doc);
#include "file_opts.h"
#undef OPT
#undef OPT_LONGONLY
	fprintf(stdout, "\nReport bugs to http://bugs.gw.com/\n");
	exit(0);
}
