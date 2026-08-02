/*
 * src/bin/pg_autoctl/step_socket.c
 *   A small Unix-domain-socket protocol used to drive a running
 *   pg_autoctl node-active service one FSM transition at a time, from
 *   an external "pg_autoctl manual fsm step" client, when the node is
 *   suspended (PG_AUTOCTL_SUSPENDED). This gives precise, gdb-step-like
 *   external control over the keeper's FSM, without having to run a
 *   fresh one-shot CLI process per step (see keeper_fsm_step(), which
 *   this module calls into from within the already-running service).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "pgsetup.h"
#include "step_socket.h"

#define STEP_SOCKET_SUFFIX ".step"


/*
 * step_socket_path derives the suspended-node control socket path from the
 * node's existing pidfile path, so that no new pathname needs to be
 * computed, persisted, or kept in sync with PGDATA independently. Both
 * the server (service_keeper.c) and the client (cli_do_fsm.c) call this
 * same function to agree on where the socket lives.
 */
bool
step_socket_path(const char *pidFilePath, char *path, size_t size)
{
	int n = sformat(path, size, "%s%s", pidFilePath, STEP_SOCKET_SUFFIX);

	return n > 0 && (size_t) n < size;
}


/*
 * step_socket_listen creates, binds, and starts listening on the
 * suspended-node control socket. Returns true and sets *listenFd on success.
 */
bool
step_socket_listen(const char *pidFilePath, char *path, size_t pathSize,
				   int *listenFd)
{
	struct sockaddr_un addr = { 0 };

	if (!step_socket_path(pidFilePath, path, pathSize))
	{
		log_error("Failed to build the suspended-node socket path from \"%s\": "
				  "path is too long", pidFilePath);
		return false;
	}

	if (strlen(path) >= sizeof(addr.sun_path))
	{
		log_error("Suspended-node socket path \"%s\" is too long for a "
				  "Unix-domain socket (max %lu bytes)",
				  path, (unsigned long) sizeof(addr.sun_path) - 1);
		return false;
	}

	/* remove a stale socket file possibly left behind by an unclean exit */
	unlink(path);

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (fd < 0)
	{
		log_error("Failed to create the suspended-node control socket: %m");
		return false;
	}

	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

	if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0)
	{
		log_error("Failed to bind the suspended-node control socket \"%s\": %m", path);
		close(fd);
		return false;
	}

	if (listen(fd, 1) != 0)
	{
		log_error("Failed to listen on the suspended-node control socket \"%s\": %m",
				  path);
		close(fd);
		return false;
	}

	*listenFd = fd;
	return true;
}


/*
 * step_socket_close closes the listening socket and removes the socket
 * file from the filesystem.
 */
void
step_socket_close(int listenFd, const char *path)
{
	if (listenFd >= 0)
	{
		close(listenFd);
	}

	if (path != NULL && !IS_EMPTY_STRING_BUFFER(path))
	{
		unlink(path);
	}
}


/*
 * step_socket_wait_for_command polls the suspended-node listening socket for up
 * to timeoutMs milliseconds waiting for a client to connect and send a
 * command. Returns true and sets *clientFd to an open connection, with
 * command (a buffer of at least commandSize bytes) set to the exact command
 * string received (one of STEP/REPORT/ADVANCE), when a recognized command
 * was received; returns false (with *clientFd left at -1) on timeout, a
 * transient error, or an unrecognized command -- the caller treats all of
 * those identically to "no command yet, keep going", so a misbehaving
 * client can never wedge the main loop.
 */
bool
step_socket_wait_for_command(int listenFd, int timeoutMs, int *clientFd,
							 char *command, size_t commandSize)
{
	*clientFd = -1;

	struct pollfd pfd = { .fd = listenFd, .events = POLLIN };

	int ret = poll(&pfd, 1, timeoutMs);

	if (ret <= 0)
	{
		/* timeout, or interrupted/failed (most likely EINTR from a signal) */
		return false;
	}

	int fd = accept(listenFd, NULL, NULL);

	if (fd < 0)
	{
		log_warn("Failed to accept a suspended-node client connection: %m");
		return false;
	}

	char buffer[BUFSIZE] = { 0 };
	ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);

	if (bytes <= 0)
	{
		close(fd);
		return false;
	}

	while (bytes > 0 && (buffer[bytes - 1] == '\n' || buffer[bytes - 1] == '\r'))
	{
		buffer[--bytes] = '\0';
	}

	if (strcmp(buffer, STEP_SOCKET_COMMAND_STEP) != 0 &&
		strcmp(buffer, STEP_SOCKET_COMMAND_REPORT) != 0 &&
		strcmp(buffer, STEP_SOCKET_COMMAND_ADVANCE) != 0)
	{
		log_warn("Ignoring unrecognized suspended-node command: \"%s\"", buffer);
		(void) write(fd, "ERROR unrecognized command\n", 28);
		close(fd);
		return false;
	}

	strlcpy(command, buffer, commandSize);

	*clientFd = fd;
	return true;
}


/*
 * step_socket_respond_ok reports a successful FSM step back to the client.
 */
void
step_socket_respond_ok(int clientFd, const char *oldRole, const char *newRole)
{
	char response[BUFSIZE] = { 0 };
	int n = sformat(response, sizeof(response), "OK %s %s\n", oldRole, newRole);

	if (n > 0)
	{
		(void) write(clientFd, response, strlen(response));
	}
}


/*
 * step_socket_respond_error reports a failed FSM step back to the client.
 */
void
step_socket_respond_error(int clientFd, const char *message)
{
	char response[BUFSIZE] = { 0 };
	int n = sformat(response, sizeof(response), "ERROR %s\n", message);

	if (n > 0)
	{
		(void) write(clientFd, response, strlen(response));
	}
}


/*
 * step_socket_send_command connects to the suspended-node control socket at
 * path, sends command, and reads back the response into response (which
 * must be at least responseSize bytes). Returns true when a response was
 * received, regardless of whether it was an OK or ERROR response -- the
 * caller distinguishes those by inspecting the response text itself.
 */
bool
step_socket_send_command(const char *path, const char *command,
						 char *response, size_t responseSize)
{
	struct sockaddr_un addr = { 0 };

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (fd < 0)
	{
		log_error("Failed to create a suspended-node client socket: %m");
		return false;
	}

	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0)
	{
		log_error("Failed to connect to the suspended-node control socket \"%s\": %m",
				  path);
		close(fd);
		return false;
	}

	char commandLine[BUFSIZE];
	sformat(commandLine, sizeof(commandLine), "%s\n", command);

	if (write(fd, commandLine, strlen(commandLine)) < 0)
	{
		log_error("Failed to send command to the suspended-node control socket: %m");
		close(fd);
		return false;
	}

	ssize_t bytes = read(fd, response, responseSize - 1);

	close(fd);

	if (bytes <= 0)
	{
		log_error("Failed to read a response from the suspended-node control socket");
		return false;
	}

	response[bytes] = '\0';

	while (bytes > 0 &&
		   (response[bytes - 1] == '\n' || response[bytes - 1] == '\r'))
	{
		response[--bytes] = '\0';
	}

	return true;
}
