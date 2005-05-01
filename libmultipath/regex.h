/* Definitions for data structures and routines for the regular
   expression library, version 0.12.

   Copyright (C) 1985, 1989, 1990, 1991, 1992, 1993
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef __REGEXP_LIBRARY_H__
#define __REGEXP_LIBRARY_H__

typedef long s_reg_t;
typedef unsigned long active_reg_t;

typedef unsigned long reg_syntax_t;

#define RE_BACKSLASH_ESCAPE_IN_LISTS	(1L)
#define RE_BK_PLUS_QM			(RE_BACKSLASH_ESCAPE_IN_LISTS << 1)
#define RE_CHAR_CLASSES			(RE_BK_PLUS_QM << 1)
#define RE_CONTEXT_INDEP_ANCHORS	(RE_CHAR_CLASSES << 1)
#define RE_CONTEXT_INDEP_OPS		(RE_CONTEXT_INDEP_ANCHORS << 1)
#define RE_CONTEXT_INVALID_OPS		(RE_CONTEXT_INDEP_OPS << 1)
#define RE_DOT_NEWLINE			(RE_CONTEXT_INVALID_OPS << 1)
#define RE_DOT_NOT_NULL			(RE_DOT_NEWLINE << 1)
#define RE_HAT_LISTS_NOT_NEWLINE	(RE_DOT_NOT_NULL << 1)
#define RE_INTERVALS			(RE_HAT_LISTS_NOT_NEWLINE << 1)
#define RE_LIMITED_OPS			(RE_INTERVALS << 1)
#define RE_NEWLINE_ALT			(RE_LIMITED_OPS << 1)
#define RE_NO_BK_BRACES			(RE_NEWLINE_ALT << 1)
#define RE_NO_BK_PARENS			(RE_NO_BK_BRACES << 1)
#define RE_NO_BK_REFS			(RE_NO_BK_PARENS << 1)
#define RE_NO_BK_VBAR			(RE_NO_BK_REFS << 1)
#define RE_NO_EMPTY_RANGES		(RE_NO_BK_VBAR << 1)
#define RE_UNMATCHED_RIGHT_PAREN_ORD	(RE_NO_EMPTY_RANGES << 1)
#define RE_NO_GNU_OPS			(RE_UNMATCHED_RIGHT_PAREN_ORD << 1)

extern reg_syntax_t re_syntax_options;

#define RE_SYNTAX_EMACS 0

#define RE_SYNTAX_AWK							  \
	(RE_BACKSLASH_ESCAPE_IN_LISTS	| RE_DOT_NOT_NULL		| \
	 RE_NO_BK_PARENS		| RE_NO_BK_REFS			| \
	 RE_NO_BK_VBAR			| RE_NO_EMPTY_RANGES		| \
	 RE_DOT_NEWLINE			| RE_CONTEXT_INDEP_ANCHORS	| \
	 RE_UNMATCHED_RIGHT_PAREN_ORD	| RE_NO_GNU_OPS)

#define RE_SYNTAX_GNU_AWK 						  \
	((RE_SYNTAX_POSIX_EXTENDED	| RE_BACKSLASH_ESCAPE_IN_LISTS)	| \
	& ~(RE_DOT_NOT_NULL | RE_INTERVALS | RE_CONTEXT_INDEP_OPS))

#define RE_SYNTAX_POSIX_AWK 						  \
	(RE_SYNTAX_POSIX_EXTENDED	| RE_BACKSLASH_ESCAPE_IN_LISTS	| \
	 RE_INTERVALS			| RE_NO_GNU_OPS)

#define RE_SYNTAX_GREP							  \
	(RE_BK_PLUS_QM			| RE_CHAR_CLASSES		| \
	 RE_HAT_LISTS_NOT_NEWLINE	| RE_INTERVALS			| \
	 RE_NEWLINE_ALT)

#define RE_SYNTAX_EGREP							  \
	(RE_CHAR_CLASSES		| RE_CONTEXT_INDEP_ANCHORS	| \
	 RE_CONTEXT_INDEP_OPS		| RE_HAT_LISTS_NOT_NEWLINE	| \
	 RE_NEWLINE_ALT			| RE_NO_BK_PARENS		| \
	 RE_NO_BK_VBAR)

#define RE_SYNTAX_POSIX_EGREP						  \
	(RE_SYNTAX_EGREP		| RE_INTERVALS			| \
	 RE_NO_BK_BRACES)

#define RE_SYNTAX_ED RE_SYNTAX_POSIX_BASIC

#define RE_SYNTAX_SED RE_SYNTAX_POSIX_BASIC

#define _RE_SYNTAX_POSIX_COMMON						  \
	(RE_CHAR_CLASSES		| RE_DOT_NEWLINE		| \
	 RE_DOT_NOT_NULL		| RE_INTERVALS			| \
	 RE_NO_EMPTY_RANGES)

#define RE_SYNTAX_POSIX_BASIC						  \
	(_RE_SYNTAX_POSIX_COMMON	| RE_BK_PLUS_QM)

#define RE_SYNTAX_POSIX_MINIMAL_BASIC					  \
	(_RE_SYNTAX_POSIX_COMMON	| RE_LIMITED_OPS)

#define RE_SYNTAX_POSIX_EXTENDED					  \
	(_RE_SYNTAX_POSIX_COMMON	| RE_CONTEXT_INDEP_ANCHORS	| \
	 RE_CONTEXT_INDEP_OPS		| RE_NO_BK_BRACES		| \
	 RE_NO_BK_PARENS		| RE_NO_BK_VBAR			| \
	 RE_UNMATCHED_RIGHT_PAREN_ORD)

#define RE_SYNTAX_POSIX_MINIMAL_EXTENDED				  \
	(_RE_SYNTAX_POSIX_COMMON	| RE_CONTEXT_INDEP_ANCHORS	| \
	 RE_CONTEXT_INVALID_OPS		| RE_NO_BK_BRACES		| \
	 RE_NO_BK_PARENS		| RE_NO_BK_REFS			| \
	 RE_NO_BK_VBAR			| RE_UNMATCHED_RIGHT_PAREN_ORD)

/* Maximum number of duplicates an interval can allow */
#define RE_DUP_MAX  (0x7fff)

/* POSIX 'cflags' bits */
#define REG_EXTENDED	1
#define REG_ICASE	(REG_EXTENDED << 1)
#define REG_NEWLINE	(REG_ICASE << 1)
#define REG_NOSUB	(REG_NEWLINE << 1)


/* POSIX `eflags' bits */
#define REG_NOTBOL	1
#define REG_NOTEOL	(1 << 1)

/* If any error codes are removed, changed, or added, update the
   `re_error_msg' table in regex.c.  */
typedef enum
{
  REG_NOERROR = 0,	/* Success.  */
  REG_NOMATCH,		/* Didn't find a match (for regexec).  */

  /* POSIX regcomp return error codes */
  REG_BADPAT,		/* Invalid pattern.  */
  REG_ECOLLATE,		/* Not implemented.  */
  REG_ECTYPE,		/* Invalid character class name.  */
  REG_EESCAPE,		/* Trailing backslash.  */
  REG_ESUBREG,		/* Invalid back reference.  */
  REG_EBRACK,		/* Unmatched left bracket.  */
  REG_EPAREN,		/* Parenthesis imbalance.  */
  REG_EBRACE,		/* Unmatched \{.  */
  REG_BADBR,		/* Invalid contents of \{\}.  */
  REG_ERANGE,		/* Invalid range end.  */
  REG_ESPACE,		/* Ran out of memory.  */
  REG_BADRPT,		/* No preceding re for repetition op.  */

  /* Error codes we've added */
  REG_EEND,		/* Premature end.  */
  REG_ESIZE,		/* Compiled pattern bigger than 2^16 bytes.  */
  REG_ERPAREN		/* Unmatched ) or \); not returned from regcomp.  */
} reg_errcode_t;

#define REGS_UNALLOCATED	0
#define REGS_REALLOCATE		1
#define REGS_FIXED		2

/* This data structure represents a compiled pattern */
struct re_pattern_buffer
{
  unsigned char *buffer;
  unsigned long allocated;
  unsigned long used;
  reg_syntax_t syntax;
  char *fastmap;
  char *translate;
  size_t re_nsub;
  unsigned can_be_null : 1;
  unsigned regs_allocated : 2;
  unsigned fastmap_accurate : 1;
  unsigned no_sub : 1;
  unsigned not_bol : 1;
  unsigned not_eol : 1;
  unsigned newline_anchor : 1;
};

typedef struct re_pattern_buffer regex_t;

/* search.c (search_buffer) in Emacs needs this one opcode value.  It is
   defined both in `regex.c' and here.  */
#define RE_EXACTN_VALUE 1

/* Type for byte offsets within the string.  POSIX mandates this.  */
typedef int regoff_t;


/* This is the structure we store register match data in.  See
   regex.texinfo for a full description of what registers match.  */
struct re_registers
{
  unsigned num_regs;
  regoff_t *start;
  regoff_t *end;
};


#ifndef RE_NREGS
#define RE_NREGS 30
#endif


/* POSIX specification for registers.  Aside from the different names than
   `re_registers', POSIX uses an array of structures, instead of a
   structure of arrays.  */
typedef struct
{
  regoff_t rm_so;  /* Byte offset from string's start to substring's start.  */
  regoff_t rm_eo;  /* Byte offset from string's start to substring's end.  */
} regmatch_t;

/* Declarations for routines.  */

extern reg_syntax_t re_set_syntax (reg_syntax_t syntax);

extern const char *re_compile_pattern (const char *pattern, size_t length,
				       struct re_pattern_buffer *buffer);

extern int re_compile_fastmap (struct re_pattern_buffer *buffer);

extern int re_search (struct re_pattern_buffer *buffer, const char *string,
		      int length, int start, int range,
		      struct re_registers *regs);

extern int re_search_2 (struct re_pattern_buffer *buffer, const char *string1,
			int length1, const char *string2, int length2,
			int start, int range, struct re_registers *regs,
			int stop);

extern int re_match (struct re_pattern_buffer *buffer, const char *string,
		     int length, int start, struct re_registers *regs);

extern int re_match_2 (struct re_pattern_buffer *buffer, const char *string1,
		       int length1, const char *string2, int length2,
		       int start, struct re_registers *regs, int stop);

extern void re_set_registers (struct re_pattern_buffer *buffer,
			      struct re_registers *regs, unsigned num_regs,
			      regoff_t *starts, regoff_t *ends);

/* 4.2 bsd compatibility.  */
extern char *re_comp (const char *);
extern int re_exec (const char *);

/* POSIX compatibility.  */
extern int regcomp (regex_t *preg, const char *pattern, int cflags);

extern int regexec (const regex_t *preg, const char *string, size_t nmatch,
		    regmatch_t pmatch[], int eflags);

extern size_t regerror (int errcode, const regex_t *preg, char *errbuf,
			size_t errbuf_size);

extern void regfree (regex_t *preg);

#endif /* not __REGEXP_LIBRARY_H__ */
