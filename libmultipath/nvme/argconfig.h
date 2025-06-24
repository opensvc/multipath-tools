// SPDX-License-Identifier: GPL-2.0-or-later
////////////////////////////////////////////////////////////////////////
//
// Copyright 2014 PMC-Sierra, Inc.
//
////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////
//
//   Author: Logan Gunthorpe <logang@deltatee.com>
//           Logan Gunthorpe
//
//   Date:   Oct 23 2014
//
//   Description:
//     Header file for argconfig.c
//
////////////////////////////////////////////////////////////////////////

#ifndef NVME_ARGCONFIG_H_INCLUDED
#define NVME_ARGCONFIG_H_INCLUDED

#include <string.h>
#include <getopt.h>
#include <stdarg.h>

enum argconfig_types {
	CFG_NONE,
	CFG_STRING,
	CFG_INT,
	CFG_SIZE,
	CFG_LONG,
	CFG_LONG_SUFFIX,
	CFG_DOUBLE,
	CFG_BOOL,
	CFG_BYTE,
	CFG_SHORT,
	CFG_POSITIVE,
	CFG_INCREMENT,
	CFG_SUBOPTS,
	CFG_FILE_A,
	CFG_FILE_W,
	CFG_FILE_R,
	CFG_FILE_AP,
	CFG_FILE_WP,
	CFG_FILE_RP,
};

struct argconfig_commandline_options {
	const char *option;
	const char short_option;
	const char *meta;
	enum argconfig_types config_type;
	void *default_value;
	int argument_type;
	const char *help;
};

#define CFG_MAX_SUBOPTS 500
#define MAX_HELP_FUNC 20

#ifdef __cplusplus
extern "C" {
#endif

typedef void argconfig_help_func();
void argconfig_append_usage(const char *str);
void argconfig_print_help(const char *program_desc,
			  const struct argconfig_commandline_options *options);
int argconfig_parse(int argc, char *argv[], const char *program_desc,
		    const struct argconfig_commandline_options *options,
		    void *config_out, size_t config_size);
int argconfig_parse_subopt_string(char *string, char **options,
				  size_t max_options);
unsigned argconfig_parse_comma_sep_array(char *string, int *ret,
					 unsigned max_length);
unsigned argconfig_parse_comma_sep_array_long(char *string,
					      unsigned long long *ret,
					      unsigned max_length);
void argconfig_register_help_func(argconfig_help_func * f);

void print_word_wrapped(const char *s, int indent, int start);
#ifdef __cplusplus
}
#endif
#endif
