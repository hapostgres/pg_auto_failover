/*
 * runprogram.h
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef RUN_PROGRAM_H
#define RUN_PROGRAM_H

#include <stdarg.h>
#include <stdbool.h>

#define BUFSIZE			1024
#define ARGS_INCREMENT	12

#if defined(WIN32) && !defined(__CYGWIN__)
#define DEV_NULL "NUL"
#else
#define DEV_NULL "/dev/null"
#endif

#define MAX(a,b) (((a)>(b))?(a):(b))

typedef struct
{
	char *program;
	char **args;
	bool setsid;				/* shall we call setsid() ? */

	int error;					/* save errno when something's gone wrong */
	int returnCode;

	char *stdout;
	char *stderr;
} Program;

Program run_program(const char *program, ...);
Program initialize_program(char **args, bool setsid);
void execute_program(Program *prog);
void free_program(Program *prog);
int snprintf_program_command_line(Program *prog, char *buffer, int size);

#endif	/* RUN_PROGRAM_H */
