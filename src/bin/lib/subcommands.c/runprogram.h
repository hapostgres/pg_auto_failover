/*
 * runprogram.h
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifdef RUN_PROGRAM_IMPLEMENTATION
#undef RUN_PROGRAM_IMPLEMENTATION

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>

#include "pqexpbuffer.h"

#define BUFSIZE 1024
#define ARGS_INCREMENT 12

#if defined(WIN32) && !defined(__CYGWIN__)
#define DEV_NULL "NUL"
#else
#define DEV_NULL "/dev/null"
#endif

#define MAX(a, b) (((a) > (b)) ? (a) : (b))

typedef struct
{
	char *program;
	char **args;
	bool setsid;                /* shall we call setsid() ? */

	int error;                  /* save errno when something's gone wrong */
	int returnCode;

	char *stdOut;
	char *stdErr;
} Program;

Program run_program(const char *program, ...);
Program initialize_program(char **args, bool setsid);
void execute_program(Program *prog);
void free_program(Program *prog);
int snprintf_program_command_line(Program *prog, char *buffer, int size);
static void read_from_pipes(Program *prog,
							pid_t childPid, int *outpipe, int *errpipe);
static size_t read_into_buf(int filedes, PQExpBuffer buffer);


/*
 * Run a program using fork() and exec(), get the stdOut and stdErr output from
 * the run and then return a Program struct instance with the result of running
 * the program.
 */
Program
run_program(const char *program, ...)
{
	int nb_args = 0;
	va_list args;
	const char *param;
	Program prog;

	prog.program = strdup(program);
	prog.returnCode = -1;
	prog.error = 0;
	prog.setsid = false;
	prog.stdOut = NULL;
	prog.stdErr = NULL;

	prog.args = (char **) malloc(ARGS_INCREMENT * sizeof(char *));
	prog.args[nb_args++] = prog.program;

	va_start(args, program);
	while ((param = va_arg(args, const char *)) != NULL)
	{
		if (nb_args % ARGS_INCREMENT == 0)
		{
			char **newargs = (char **) malloc((ARGS_INCREMENT *
											   (nb_args / ARGS_INCREMENT + 1)) *
											  sizeof(char *));
			for (int i = 0; i < nb_args; i++)
			{
				newargs[i] = prog.args[i];
			}
			free(prog.args);

			prog.args = newargs;
		}
		prog.args[nb_args++] = strdup(param);
	}
	va_end(args);
	prog.args[nb_args] = NULL;

	execute_program(&prog);

	return prog;
}


/*
 * Initialize a program structure that can be executed later, allowing the
 * caller to manipulate the structure for itself. Safe to change are program,
 * args and setsid structure slots.
 */
Program
initialize_program(char **args, bool setsid)
{
	int argsIndex, nb_args = 0;
	Program prog;

	prog.returnCode = -1;
	prog.error = 0;
	prog.setsid = setsid;
	prog.stdOut = NULL;
	prog.stdErr = NULL;

	for (argsIndex = 0; args[argsIndex] != NULL; argsIndex++)
	{
		++nb_args;
	}

	/* add another one nb_args for the terminating NULL entry */
	prog.args = (char **) malloc(++nb_args * sizeof(char *));
	memset(prog.args, 0, nb_args * sizeof(char *));

	for (argsIndex = 0; args[argsIndex] != NULL; argsIndex++)
	{
		prog.args[argsIndex] = strdup(args[argsIndex]);
	}
	prog.program = prog.args[0];

	return prog;
}


/*
 * Run given program with its args, by doing the fork()/exec() dance, and also
 * capture the subprocess output by installing pipes. We accimulate the output
 * into an SDS data structure (Simple Dynamic Strings library).
 */
void
execute_program(Program *prog)
{
	pid_t pid;
	int outpipe[2] = { 0, 0 };
	int errpipe[2] = { 0, 0 };

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* create the pipe now */
	if (pipe(outpipe) < 0)
	{
		prog->returnCode = -1;
		prog->error = errno;
		return;
	}

	if (pipe(errpipe) < 0)
	{
		prog->returnCode = -1;
		prog->error = errno;
		return;
	}

	pid = fork();

	switch (pid)
	{
		case -1:
		{
			/* fork failed */
			prog->returnCode = -1;
			prog->error = errno;
			return;
		}

		case 0:
		{
			/* fork succeeded, in child */

			/*
			 * We redirect /dev/null into stdIn rather than closing stdin,
			 * because apparently closing it may cause undefined behavior if
			 * any read was to happen.
			 */
			int stdIn = open(DEV_NULL, O_RDONLY);

			dup2(stdIn, STDIN_FILENO);
			dup2(outpipe[1], STDOUT_FILENO);
			dup2(errpipe[1], STDERR_FILENO);

			close(stdIn);
			close(outpipe[0]);
			close(outpipe[1]);
			close(errpipe[0]);
			close(errpipe[1]);

			/*
			 * When asked to do so, before creating the child process, we call
			 * setsid() to create our own session group and detach from the
			 * terminal. That's useful when starting a service in the
			 * background.
			 */
			if (prog->setsid)
			{
				if (setsid() == -1)
				{
					prog->returnCode = -1;
					prog->error = errno;
					return;
				}
			}

			if (execv(prog->program, prog->args) == -1)
			{
				prog->returnCode = -1;
				prog->error = errno;

				fprintf(stdout, "%s\n", strerror(errno));
				fprintf(stderr, "%s\n", strerror(errno));
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			return;
		}

		default:
		{
			/* fork succeeded, in parent */
			read_from_pipes(prog, pid, outpipe, errpipe);
			return;
		}
	}
}


/*
 * Free our memory.
 */
void
free_program(Program *prog)
{
	/* don't free prog->program, it's the same pointer as prog->args[0] */
	for (int i = 0; prog->args[i] != NULL; i++)
	{
		free(prog->args[i]);
	}
	free(prog->args);

	if (prog->stdOut != NULL)
	{
		free(prog->stdOut);
	}

	if (prog->stdErr != NULL)
	{
		free(prog->stdErr);
	}
}


/*
 * read_from_pipes reads the output from the child process and sets the Program
 * slots stdOut and stdErr with the accumulated output we read.
 */
static void
read_from_pipes(Program *prog, pid_t childPid, int *outpipe, int *errpipe)
{
	bool doneReading = false;
	int status;
	int countFdsReadyToRead, nfds; /* see man select(3) */
	fd_set readFileDescriptorSet;
	ssize_t bytes_out = BUFSIZE, bytes_err = BUFSIZE;
	PQExpBuffer outbuf, errbuf;

	/* We read from the other side of the pipe, close that part.  */
	close(outpipe[1]);
	close(errpipe[1]);

	nfds = MAX(outpipe[0], errpipe[0]) + 1;

	/*
	 * Ok. the child process is running, let's read the pipes content.
	 */
	outbuf = createPQExpBuffer();
	errbuf = createPQExpBuffer();

	while (!doneReading)
	{
		FD_ZERO(&readFileDescriptorSet);

		/* if we read 0 bytes on the previous run, we've reached EOF */
		if (bytes_out > 0)
		{
			FD_SET(outpipe[0], &readFileDescriptorSet);
		}

		if (bytes_err > 0)
		{
			FD_SET(errpipe[0], &readFileDescriptorSet);
		}

		countFdsReadyToRead =
			select(nfds, &readFileDescriptorSet, NULL, NULL, NULL);

		if (countFdsReadyToRead == -1)
		{
			switch (errno)
			{
				case EAGAIN:
				case EINTR:
				{
					/* just loop again */
					break;
				}

				case EBADF:
				case EINVAL:
				case ENOMEM:
				default:
				{
					/* that's unexpected, act as if doneReading */
					log_error("Failed to read from command \"%s\": %s",
							  prog->program, strerror(errno));
					doneReading = true;
					break;
				}
			}
		}
		else if (countFdsReadyToRead == 0)
		{
			continue;
		}
		else
		{
			if (FD_ISSET(outpipe[0], &readFileDescriptorSet))
			{
				bytes_out = read_into_buf(outpipe[0], outbuf);

				if (bytes_out == -1 && errno != 0)
				{
					prog->returnCode = -1;
					prog->error = errno;
				}
			}

			if (FD_ISSET(errpipe[0], &readFileDescriptorSet))
			{
				bytes_err = read_into_buf(errpipe[0], errbuf);

				if (bytes_err == -1 && errno != 0)
				{
					prog->returnCode = -1;
					prog->error = errno;
				}
			}
			doneReading = (bytes_out < BUFSIZE && bytes_err < BUFSIZE);
		}
	}

	/*
	 * Now we're done reading from both stdOut and stdErr of the child
	 * process, so close the file descriptors and prepare the char *
	 * strings output in our Program structure.
	 */
	close(outpipe[0]);
	close(errpipe[0]);

	if (outbuf->len > 0)
	{
		prog->stdOut = strndup(outbuf->data, outbuf->len);
	}

	if (errbuf->len > 0)
	{
		prog->stdErr = strndup(errbuf->data, errbuf->len);
	}

	destroyPQExpBuffer(outbuf);
	destroyPQExpBuffer(errbuf);

	/*
	 * Now, wait until the child process is done.
	 */
	do {
		if (waitpid(childPid, &status, WUNTRACED) == -1)
		{
			prog->returnCode = -1;
			prog->error = errno;
			return;
		}
	} while (!WIFEXITED(status) && !WIFSIGNALED(status));

	prog->returnCode = WEXITSTATUS(status);
}


/*
 * Read from a file descriptor and directly appends to our buffer string.
 */
static size_t
read_into_buf(int filedes, PQExpBuffer buffer)
{
	char temp_buffer[BUFSIZE] = { 0 };
	size_t bytes = read(filedes, temp_buffer, BUFSIZE);

	if (bytes > 0)
	{
		appendPQExpBufferStr(buffer, temp_buffer);
	}
	return bytes;
}


/*
 * Writes the full command line of the given program into the given
 * pre-allocated buffer of given size, and returns how many bytes would have
 * been written in the buffer if it was large enough, like snprintf would do.
 */
int
snprintf_program_command_line(Program *prog, char *buffer, int size)
{
	char *currentPtr = buffer;
	int index, remainingBytes = BUFSIZE;

	if (prog->args[0] == NULL)
	{
		return 0;
	}

	for (index = 0; prog->args[index] != NULL; index++)
	{
		int n = snprintf(currentPtr, remainingBytes, " %s", prog->args[index]);

		if (n >= remainingBytes)
		{
			return BUFSIZE - remainingBytes + n;
		}
		currentPtr += n;
		remainingBytes -= n;
	}
	return BUFSIZE - remainingBytes;
}


#endif  /* RUN_PROGRAM_IMPLEMENTATION */
