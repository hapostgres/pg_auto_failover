/*
 * src/bin/pg_walserver/cmd_archive_file.c
 *   See cmd_archive_file.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cmd_archive_file.h"
#include "cmd_fetch_file.h"
#include "file_crc32c.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "string_utils.h"
#include "wal_dir_scan.h"


/*
 * archive_file_write_local writes exactly len bytes from buf to the local
 * temporary file fd, looping over short writes (retrying on EINTR).
 * Deliberately NOT framing.c's own ws_write_bytes(): that function writes
 * to *the client connection* (plain write() with no active TLS handshake,
 * SSL_write() otherwise, via ws_io_write()'s global activeSsl -- see
 * tls.c), which is exactly wrong here -- fd below is this cluster's own
 * on-disk temporary file, never the socket, and calling ws_write_bytes()
 * on it would (with TLS active) silently SSL_write() the received bytes
 * back out to the client instead of ever reaching disk, desyncing the
 * CopyIn stream instead of storing the file. See cmd_fetch_file.c's own
 * read side for the mirror image of this same local-file-vs-connection
 * distinction.
 */
static bool
archive_file_write_local(int fd, const void *buf, size_t len)
{
	const char *ptr = (const char *) buf;
	size_t remaining = len;

	while (remaining > 0)
	{
		ssize_t n = write(fd, ptr, remaining);

		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return false;
		}

		ptr += n;
		remaining -= (size_t) n;
	}

	return true;
}


/*
 * cmd_archive_file implements ARCHIVE_FILE: validate the filename against
 * the same allow-list FETCH_FILE's read side uses, receive the whole CopyIn
 * into a same-directory temporary file (capped at the cluster's own
 * wal_segment_size plus WS_ARCHIVE_FILE_SIZE_SLACK, checked as bytes
 * arrive so an oversized push never gets to write the whole thing), then
 * decide what to do with it purely from a fresh CRC32C/size comparison
 * against whatever is already on disk under that name -- never from
 * anything the client claimed via CHECK_FILE. See cmd_archive_file.h's own
 * header comment for the full contract.
 */
void
cmd_archive_file(int sock, const WsCluster *cluster, const char *filename)
{
	if (!ws_fetch_filename_is_servable(filename))
	{
		char safeName[64];

		sanitizeForLog(filename, safeName, sizeof(safeName));
		log_warn("Rejecting ARCHIVE_FILE request for filename \"%s\"", safeName);
		ws_send_error_response(sock, "22023", "invalid filename");
		return;
	}

	if (cluster == NULL || cluster->path[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this cluster");
		return;
	}

	char finalPath[MAXPGPATH];
	char tmpPath[MAXPGPATH];

	sformat(finalPath, sizeof(finalPath), "%s/%s", cluster->path, filename);
	sformat(tmpPath, sizeof(tmpPath), "%s/.archive_tmp.%s.%d",
			cluster->path, filename, (int) getpid());

	uint64_t cap = ws_cluster_wal_segment_size(cluster) + WS_ARCHIVE_FILE_SIZE_SLACK;

	int fd = open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

	if (fd < 0)
	{
		log_error("ARCHIVE_FILE: failed to create \"%s\": %m", tmpPath);
		ws_send_error_response(sock, "58030", "failed to create a temporary file");
		return;
	}

	if (!ws_send_copy_in_response(sock, 0))
	{
		close(fd);
		unlink(tmpPath);
		ws_connection_close_after_command = true;
		return;
	}

	uint64_t total = 0;
	bool oversized = false;
	bool writeFailed = false;
	bool clientFailed = false;   /* the client itself sent CopyFail */
	bool done = false;

	while (!done)
	{
		char type;
		char *payload = NULL;
		int32_t payloadLen = 0;

		if (!ws_read_message(sock, &type, &payload, &payloadLen,
							 WS_MAX_COMMAND_MESSAGE_LEN))
		{
			free(payload);
			close(fd);
			unlink(tmpPath);

			/* the read itself failed: the protocol cannot be resynchronized
			 * from here, same as every other mid-COPY failure in this
			 * codebase (see ws_connection_close_after_command's own comment) */
			ws_connection_close_after_command = true;
			return;
		}

		switch (type)
		{
			case 'd':   /* CopyData */
			{
				if (!oversized && !writeFailed)
				{
					total += (uint64_t) payloadLen;

					if (total > cap)
					{
						log_error("ARCHIVE_FILE: \"%s\" exceeds the "
								  "%" PRIu64 "-byte cap for \"%s\"",
								  filename, cap, cluster->path);
						oversized = true;
					}
					else if (payloadLen > 0 &&
							 !archive_file_write_local(fd, payload, (size_t) payloadLen))
					{
						log_error("ARCHIVE_FILE: failed to write \"%s\": %m",
								  tmpPath);
						writeFailed = true;
					}
				}

				break;
			}

			case 'c':   /* CopyDone */
			{
				done = true;
				break;
			}

			case 'f':   /* CopyFail: the client itself aborted the push */
			{
				clientFailed = true;
				done = true;
				break;
			}

			default:
			{
				log_error("ARCHIVE_FILE: unexpected message type 0x%02x "
						  "during a CopyIn", (unsigned char) type);
				free(payload);
				close(fd);
				unlink(tmpPath);
				ws_connection_close_after_command = true;
				return;
			}
		}

		free(payload);
	}

	close(fd);

	if (clientFailed)
	{
		unlink(tmpPath);
		log_info("ARCHIVE_FILE: client aborted the push of \"%s\" (CopyFail)",
				 filename);
		ws_send_error_response(sock, "57014",
							   "ARCHIVE_FILE cancelled by the client");
		return;
	}

	if (oversized || writeFailed)
	{
		unlink(tmpPath);
		ws_send_error_response(sock, oversized ? "54000" : "58030",
							   oversized
							   ? "file exceeds this cluster's maximum archive size"
							   : "failed to write the received file");
		return;
	}

	/*
	 * Overwrite-safety: never trust the client's own CHECK_FILE checksum
	 * (that would let a lying client push whatever it wants past this
	 * check) -- re-derive it here from the real bytes just received and
	 * whatever is really on disk already, matching PostgreSQL's own
	 * archive_command contract exactly (identical -> success/idempotent
	 * retry, different -> reject).
	 */
	uint64_t receivedSize = 0;
	uint32_t receivedCrc = 0;

	if (!file_crc32c(tmpPath, &receivedSize, &receivedCrc))
	{
		log_error("ARCHIVE_FILE: failed to re-read \"%s\" after receiving "
				  "it: %m", tmpPath);
		unlink(tmpPath);
		ws_send_error_response(sock, "58030", "failed to verify the received file");
		return;
	}

	uint64_t existingSize = 0;
	uint32_t existingCrc = 0;
	bool haveExisting = file_crc32c(finalPath, &existingSize, &existingCrc);

	if (haveExisting)
	{
		if (existingSize == receivedSize && existingCrc == receivedCrc)
		{
			/* byte-identical: an idempotent retry after a crash mid-archive
			 * is expected and normal, exactly like PostgreSQL's own
			 * archive_command contract requires */
			unlink(tmpPath);
			log_info("ARCHIVE_FILE: \"%s\" already matches what's on disk "
					 "under \"%s\" (idempotent retry)", filename, cluster->path);
			(void) ws_send_command_complete(sock, "ARCHIVE_FILE");
			return;
		}

		unlink(tmpPath);
		log_warn("ARCHIVE_FILE: refusing to overwrite \"%s\": it already "
				 "exists with different content (%" PRIu64 " bytes, CRC32C "
														   "%08X) than what was just received (%"
				 PRIu64 " bytes, "
						"CRC32C %08X)",
				 finalPath, existingSize, existingCrc, receivedSize, receivedCrc);
		ws_send_error_response(sock, "23505",
							   "a different file already exists under this name");
		return;
	}

	if (rename(tmpPath, finalPath) != 0)
	{
		log_error("ARCHIVE_FILE: failed to rename \"%s\" to \"%s\": %m",
				  tmpPath, finalPath);
		unlink(tmpPath);
		ws_send_error_response(sock, "58030", "failed to store the received file");
		return;
	}

	log_info("ARCHIVE_FILE: stored \"%s\" (%" PRIu64 " bytes, CRC32C %08X) "
													 "under \"%s\"", filename,
			 receivedSize, receivedCrc, cluster->path);

	(void) ws_send_command_complete(sock, "ARCHIVE_FILE");
}
