/*
 * src/bin/pg_autoctl/step_socket.h
 *   A small Unix-domain-socket protocol used to drive a running
 *   pg_autoctl node-active service one FSM transition at a time.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef STEP_SOCKET_H
#define STEP_SOCKET_H

#include <stdbool.h>
#include <stddef.h>

#define STEP_SOCKET_COMMAND_STEP "STEP"
#define STEP_SOCKET_COMMAND_REPORT "REPORT"
#define STEP_SOCKET_COMMAND_ADVANCE "ADVANCE"

bool step_socket_path(const char *pidFilePath, char *path, size_t size);
bool step_socket_listen(const char *pidFilePath, char *path, size_t pathSize,
						int *listenFd);
void step_socket_close(int listenFd, const char *path);
bool step_socket_wait_for_command(int listenFd, int timeoutMs, int *clientFd,
								  char *command, size_t commandSize);
void step_socket_respond_ok(int clientFd, const char *oldRole, const char *newRole);
void step_socket_respond_error(int clientFd, const char *message);
bool step_socket_send_command(const char *path, const char *command,
							  char *response, size_t responseSize);

#endif /* STEP_SOCKET_H */
