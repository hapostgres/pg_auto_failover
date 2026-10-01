/*
 * src/bin/pg_walserver/framing.c
 *   See framing.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "framing.h"
#include "tls.h"
#include "log.h"

/* startup-packet body larger than this is rejected outright as malformed */
#define WS_MAX_STARTUP_PACKET_SIZE 10000

bool ws_connection_close_after_command = false;

#define SSL_REQUEST_CODE 80877103
#define GSS_REQUEST_CODE 80877104
#define CANCEL_REQUEST_CODE 80877102


/*
 * ws_read_bytes reads exactly len bytes from sock into buf, looping over
 * short reads (retrying on EINTR) until the buffer is full. Returns false on
 * any read error or on a clean peer close before len bytes were seen.
 */
bool
ws_read_bytes(int sock, void *buf, size_t len)
{
	char *ptr = (char *) buf;
	size_t remaining = len;

	while (remaining > 0)
	{
		ssize_t n = ws_io_read(sock, ptr, remaining);

		if (n < 0)
		{
			/* EAGAIN/ETIMEDOUT and every other error end the connection */
			if (errno == EINTR)
			{
				continue;
			}
			return false;
		}

		if (n == 0)
		{
			/* peer closed the connection */
			return false;
		}

		ptr += n;
		remaining -= n;
	}

	return true;
}


/*
 * ws_write_bytes writes exactly len bytes from buf to sock, looping over
 * short writes (retrying on EINTR) until everything has been sent. Returns
 * false on any write error.
 */
bool
ws_write_bytes(int sock, const void *buf, size_t len)
{
	const char *ptr = (const char *) buf;
	size_t remaining = len;

	while (remaining > 0)
	{
		ssize_t n = ws_io_write(sock, ptr, remaining);

		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return false;
		}

		ptr += n;
		remaining -= n;
	}

	return true;
}


/*
 * ws_write_raw_byte writes a single byte, unframed (no message type/length
 * header) -- used for the pre-startup 'S'/'N' SSLRequest and 'N' GSSRequest
 * replies, which are single raw bytes rather than framed protocol messages.
 */
bool
ws_write_raw_byte(int sock, char c)
{
	return ws_write_bytes(sock, &c, 1);
}


/*
 * ws_read_startup_payload reads a pre-startup message body: a 4-byte
 * big-endian length (which, unlike ws_read_message(), includes itself but
 * has no leading type byte -- this is the StartupMessage/SSLRequest/
 * CancelRequest wire shape), followed by that many bytes of payload.
 * *payload is malloc'd and NUL-terminated; the caller frees it. Returns
 * false on a read error or a length outside [4, WS_MAX_STARTUP_PACKET_SIZE].
 */
bool
ws_read_startup_payload(int sock, char **payload, int32_t *payloadLen)
{
	unsigned char lenBuf[4];

	*payload = NULL;
	*payloadLen = 0;

	if (!ws_read_bytes(sock, lenBuf, 4))
	{
		return false;
	}

	int32_t len = (int32_t) (((uint32_t) lenBuf[0] << 24) |
							 ((uint32_t) lenBuf[1] << 16) |
							 ((uint32_t) lenBuf[2] << 8) |
							 (uint32_t) lenBuf[3]);

	if (len < 4 || len > WS_MAX_STARTUP_PACKET_SIZE)
	{
		log_error("Received an invalid startup packet length: %d", len);
		return false;
	}

	int32_t bodyLen = len - 4;
	char *buf = (char *) malloc(bodyLen + 1);

	if (buf == NULL)
	{
		log_error("Failed to allocate %d bytes for a startup packet: %m", bodyLen);
		return false;
	}

	if (bodyLen > 0 && !ws_read_bytes(sock, buf, bodyLen))
	{
		free(buf);
		return false;
	}

	buf[bodyLen] = '\0';

	*payload = buf;
	*payloadLen = bodyLen;

	return true;
}


/*
 * ws_read_message reads one regular (post-startup) protocol message: a
 * 1-byte type, a 4-byte big-endian length (including itself, per the wire
 * protocol), and that many minus 4 bytes of body, malloc'd into *payload and
 * NUL-terminated (the caller frees it). maxLen caps the body size, checked
 * before any allocation happens, like real Postgres's pq_getmessage(); an
 * oversize or truncated length sends an ErrorResponse and returns false, as
 * does any read error.
 */
bool
ws_read_message(int sock, char *type, char **payload, int32_t *payloadLen,
				int32_t maxLen)
{
	*payload = NULL;
	*payloadLen = 0;

	if (!ws_read_bytes(sock, type, 1))
	{
		return false;
	}

	unsigned char lenBuf[4];

	if (!ws_read_bytes(sock, lenBuf, 4))
	{
		return false;
	}

	int32_t len = (int32_t) (((uint32_t) lenBuf[0] << 24) |
							 ((uint32_t) lenBuf[1] << 16) |
							 ((uint32_t) lenBuf[2] << 8) |
							 (uint32_t) lenBuf[3]);

	/*
	 * Like pq_getmessage(): the length is checked against the caller's
	 * maximum BEFORE anything is allocated, and an oversize message is a
	 * protocol violation that ends the connection.
	 */
	if (len < 4 || len - 4 > maxLen)
	{
		log_error("Received an invalid message length %d for a message of "
				  "type 0x%02x", len, (unsigned char) *type);
		(void) ws_send_error_response(sock, "08P01", "invalid message length");
		return false;
	}

	int32_t bodyLen = len - 4;
	char *buf = (char *) malloc(bodyLen + 1);

	if (buf == NULL)
	{
		log_error("Failed to allocate %d bytes for a protocol message: %m", bodyLen);
		return false;
	}

	if (bodyLen > 0 && !ws_read_bytes(sock, buf, bodyLen))
	{
		free(buf);
		return false;
	}

	buf[bodyLen] = '\0';

	*payload = buf;
	*payloadLen = bodyLen;

	return true;
}


/*
 * ws_send_message writes one framed protocol message: a 1-byte type, a
 * 4-byte big-endian length (data plus the 4 length bytes themselves), and
 * dataLen bytes of body. The building block every other ws_send_*() function
 * in this file uses.
 */
bool
ws_send_message(int sock, char type, const char *data, int32_t dataLen)
{
	char header[5];
	int32_t netLen = htonl(dataLen + 4);

	header[0] = type;
	memcpy(header + 1, &netLen, 4); /* IGNORE-BANNED */

	if (!ws_write_bytes(sock, header, 5))
	{
		return false;
	}

	if (dataLen > 0 && !ws_write_bytes(sock, data, dataLen))
	{
		return false;
	}

	return true;
}


/* ws_send_authentication_ok sends the 'R' AuthenticationOk message. */
bool
ws_send_authentication_ok(int sock)
{
	int32_t zero = 0;

	return ws_send_message(sock, 'R', (const char *) &zero, 4);
}


/*
 * ws_send_parameter_status sends one 'S' ParameterStatus message
 * (name/value, both NUL-terminated), the same message real Postgres uses to
 * report things like server_version and TimeZone during startup.
 */
bool
ws_send_parameter_status(int sock, const char *name, const char *value)
{
	PQExpBuffer buf = createPQExpBuffer();

	appendBinaryPQExpBuffer(buf, name, strlen(name) + 1);
	appendBinaryPQExpBuffer(buf, value, strlen(value) + 1);

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, 'S', buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


/*
 * ws_send_backend_key_data sends the 'K' BackendKeyData message (process id
 * and cancellation secret key), needed so real clients can issue
 * CancelRequest, even though this server never actually acts on one.
 */
bool
ws_send_backend_key_data(int sock, int32_t pid, int32_t secret)
{
	char data[8];
	int32_t netPid = htonl(pid);
	int32_t netSecret = htonl(secret);

	memcpy(data, &netPid, 4); /* IGNORE-BANNED */
	memcpy(data + 4, &netSecret, 4); /* IGNORE-BANNED */

	return ws_send_message(sock, 'K', data, 8);
}


/*
 * ws_send_negotiate_protocol_version sends the 'v' NegotiateProtocolVersion
 * message. Per the wire protocol, the first Int32 is *not* a bare minor
 * version -- it's the full negotiated protocol version (major<<16|minor),
 * exactly like the version code in a StartupMessage; real libpq's
 * pqGetNegotiateProtocolVersion3() compares it against PG_PROTOCOL(3, 0)
 * and rejects anything smaller as "downgrade to pre-3.0 protocol version".
 * newestMinor is the highest minor protocol version we actually support
 * (always 0 -- only protocol 3.0 is implemented), combined here with major
 * version 3. unsupportedOptions/nUnsupportedOptions lists any "_pq_.*"
 * startup options the client asked for that we don't recognize (we don't
 * parse any, so this is every "_pq_.*" key seen) -- real libpq's own
 * protocol-GREASE self-test requires the server to echo back
 * "_pq_.test_protocol_negotiation" here, or it fails the connection with
 * "server did not report the unsupported ... parameter". See startup.c's
 * own caller for why this exists.
 */
bool
ws_send_negotiate_protocol_version(int sock, int32_t newestMinor,
								   const char **unsupportedOptions,
								   int nUnsupportedOptions)
{
	PQExpBuffer buf = createPQExpBuffer();

	int32_t netVersion = htonl((3 << 16) | (newestMinor & 0xFFFF));
	int32_t netOptionCount = htonl(nUnsupportedOptions);

	appendBinaryPQExpBuffer(buf, (const char *) &netVersion, 4);
	appendBinaryPQExpBuffer(buf, (const char *) &netOptionCount, 4);

	for (int i = 0; i < nUnsupportedOptions; i++)
	{
		appendBinaryPQExpBuffer(buf, unsupportedOptions[i],
								strlen(unsupportedOptions[i]) + 1);
	}

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, 'v', buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


/*
 * ws_send_ready_for_query sends the 'Z' ReadyForQuery message with
 * transaction status 'I' (idle) -- this server has no transactions, so it is
 * always idle between commands.
 */
bool
ws_send_ready_for_query(int sock)
{
	char status = 'I';

	return ws_send_message(sock, 'Z', &status, 1);
}


/*
 * ws_send_error_response sends an 'E' ErrorResponse with just the three
 * fields every real client actually looks at: Severity ('S' = ERROR),
 * SQLSTATE Code ('C'), and Message ('M'), terminated by the required empty
 * field.
 */
bool
ws_send_error_response(int sock, const char *sqlstate, const char *message)
{
	PQExpBuffer buf = createPQExpBuffer();

	appendPQExpBufferChar(buf, 'S');
	appendBinaryPQExpBuffer(buf, "ERROR", strlen("ERROR") + 1);

	appendPQExpBufferChar(buf, 'C');
	appendBinaryPQExpBuffer(buf, sqlstate, strlen(sqlstate) + 1);

	appendPQExpBufferChar(buf, 'M');
	appendBinaryPQExpBuffer(buf, message, strlen(message) + 1);

	appendPQExpBufferChar(buf, '\0');   /* terminates the field list */

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, 'E', buf->data, buf->len);

	destroyPQExpBuffer(buf);

	log_debug("walsender: sent ErrorResponse %s: %s", sqlstate, message);

	return ok;
}


/*
 * ws_send_command_complete sends a 'C' CommandComplete message with the
 * given command tag (e.g. "SELECT 1", or a replication command's own tag).
 */
bool
ws_send_command_complete(int sock, const char *tag)
{
	return ws_send_message(sock, 'C', tag, strlen(tag) + 1);
}


/*
 * ws_send_row_description sends a 'T' RowDescription message for ncols
 * columns. Every column always reports table Oid 0, attnum 0, type modifier
 * -1 and text format (0): this server never sends anything a client
 * decodes structurally, only display strings, so those fields are filler
 * that no real client inspects.
 */
bool
ws_send_row_description(int sock, const WsColumn *columns, int ncols)
{
	PQExpBuffer buf = createPQExpBuffer();
	int16_t n = htons((int16_t) ncols);

	appendBinaryPQExpBuffer(buf, (const char *) &n, 2);

	for (int i = 0; i < ncols; i++)
	{
		int32_t zero32 = 0;
		int16_t zero16 = 0;
		int32_t typeOid = htonl(columns[i].typeOid);
		int16_t typeLen = htons(columns[i].typeLen);
		int32_t typeMod = htonl(-1);
		int16_t format = 0;   /* text */

		appendBinaryPQExpBuffer(buf, columns[i].name, strlen(columns[i].name) + 1);
		appendBinaryPQExpBuffer(buf, (const char *) &zero32, 4);   /* table Oid */
		appendBinaryPQExpBuffer(buf, (const char *) &zero16, 2);   /* column attnum */
		appendBinaryPQExpBuffer(buf, (const char *) &typeOid, 4);
		appendBinaryPQExpBuffer(buf, (const char *) &typeLen, 2);
		appendBinaryPQExpBuffer(buf, (const char *) &typeMod, 4);
		appendBinaryPQExpBuffer(buf, (const char *) &format, 2);
	}

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, 'T', buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


/*
 * ws_send_data_row sends a 'D' DataRow message for ncols text-format values;
 * a NULL entry in values[] is encoded as the protocol's -1 length (SQL
 * NULL), everything else as its own byte length followed by its raw bytes.
 */
bool
ws_send_data_row(int sock, const char **values, int ncols)
{
	PQExpBuffer buf = createPQExpBuffer();
	int16_t n = htons((int16_t) ncols);

	appendBinaryPQExpBuffer(buf, (const char *) &n, 2);

	for (int i = 0; i < ncols; i++)
	{
		if (values[i] == NULL)
		{
			int32_t neg1 = htonl(-1);

			appendBinaryPQExpBuffer(buf, (const char *) &neg1, 4);
		}
		else
		{
			int32_t len = htonl((int32_t) strlen(values[i]));

			appendBinaryPQExpBuffer(buf, (const char *) &len, 4);
			appendBinaryPQExpBuffer(buf, values[i], strlen(values[i]));
		}
	}

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, 'D', buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


static bool
ws_send_copy_response(int sock, char type, int ncols)
{
	PQExpBuffer buf = createPQExpBuffer();

	/* overall format code: 0 (textual) -- we only ever send raw bytes, not
	 * a real column, so this is a formality real clients don't inspect for
	 * a CopyBoth/CopyOut stream driven by BASE_BACKUP/START_REPLICATION */
	appendPQExpBufferChar(buf, 0);

	int16_t n = htons((int16_t) ncols);

	appendBinaryPQExpBuffer(buf, (const char *) &n, 2);

	for (int i = 0; i < ncols; i++)
	{
		int16_t fmt = 0;

		appendBinaryPQExpBuffer(buf, (const char *) &fmt, 2);
	}

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, type, buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


/* ws_send_copy_out_response sends an 'H' CopyOutResponse (server-to-client
 * only), via the shared ws_send_copy_response() builder. */
bool
ws_send_copy_out_response(int sock, int ncols)
{
	return ws_send_copy_response(sock, 'H', ncols);
}


/* ws_send_copy_both_response sends a 'W' CopyBothResponse (bidirectional,
 * used by START_REPLICATION's own streaming), via ws_send_copy_response(). */
bool
ws_send_copy_both_response(int sock, int ncols)
{
	return ws_send_copy_response(sock, 'W', ncols);
}


/* ws_send_copy_in_response sends a 'G' CopyInResponse (client-to-server
 * only, used by ARCHIVE_FILE's own push), via ws_send_copy_response(). */
bool
ws_send_copy_in_response(int sock, int ncols)
{
	return ws_send_copy_response(sock, 'G', ncols);
}


/*
 * ws_send_copy_data sends one 'd' CopyData message carrying dataLen raw
 * bytes -- WAL bytes during streaming, tar bytes during a base backup.
 */
bool
ws_send_copy_data(int sock, const char *data, int32_t dataLen)
{
	return ws_send_message(sock, 'd', data, dataLen);
}


/* ws_send_copy_done sends a 'c' CopyDone message, ending a copy sub-protocol. */
bool
ws_send_copy_done(int sock)
{
	return ws_send_message(sock, 'c', NULL, 0);
}
