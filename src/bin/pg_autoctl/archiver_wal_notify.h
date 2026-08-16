/*
 * src/bin/pg_autoctl/archiver_wal_notify.h
 *   A small Unix-domain-socket protocol for registering newly-completed
 *   WAL segments without scanning the WAL cache directory -- see
 *   archiver_wal_notify.c for the full design.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_WAL_NOTIFY_H
#define ARCHIVER_WAL_NOTIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keeper_config.h"

void archiver_wal_notify_socket_path(KeeperConfig *config, char *dest, size_t destSize);

bool archiver_wal_notify_send(const char *socketPath,
							  const char *walFileName,
							  const char *lsn,
							  uint64_t systemIdentifier);

typedef struct ArchiverWalNotifyListener
{
	int listenFd;
	char socketPath[MAXPGPATH];
} ArchiverWalNotifyListener;

bool archiver_wal_notify_listener_open(KeeperConfig *config,
									   ArchiverWalNotifyListener *listener);
void archiver_wal_notify_listener_close(ArchiverWalNotifyListener *listener);

/*
 * Called once per newly-drained notification, in arrival order. Return
 * false to stop draining early (e.g. the monitor call failed) -- whatever
 * wasn't drained this round is still queued in the OS socket buffer (or,
 * if nothing is listening at all yet, simply never sent -- see
 * service_archiver_wal_scanner.c, the periodic scan this is paired with)
 * and will be seen again on the next drain.
 */
typedef bool (*ArchiverWalNotifyCallback) (void *context,
										   const char *walFileName,
										   const char *lsn,
										   uint64_t systemIdentifier);

bool archiver_wal_notify_listener_drain(ArchiverWalNotifyListener *listener,
										ArchiverWalNotifyCallback callback,
										void *context);

#endif                          /* ARCHIVER_WAL_NOTIFY_H */
