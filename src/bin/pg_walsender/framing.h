/*
 * src/bin/pg_walsender/framing.h
 *   Hand-written wire-level primitives for the Postgres frontend/backend
 *   protocol's server side: message read/write, CopyData framing,
 *   RowDescription/DataRow, ReadyForQuery, ErrorResponse. This is the
 *   pqcomm.c + pqformat.c equivalent -- no reusable library exists for
 *   this anywhere in Postgres (see walsender.h's own header comment), so
 *   it's hand-rolled directly from the documented wire format.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_FRAMING_H
#define WS_FRAMING_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* well-known type Oids used in the RowDescriptions we hand back */
#define WS_TEXTOID 25
#define WS_INT4OID 23
#define WS_INT8OID 20

typedef struct WsColumn
{
	const char *name;
	int32_t typeOid;
	int16_t typeLen;        /* -1 for varlena types such as text */
} WsColumn;

/* raw byte I/O, EINTR-safe, short-read/short-write safe */
bool ws_read_bytes(int sock, void *buf, size_t len);
bool ws_write_bytes(int sock, const void *buf, size_t len);

/*
 * Startup-phase framing: before authentication, messages have no leading
 * type byte (StartupMessage, SSLRequest, GSSENCRequest, CancelRequest are
 * all just a length-prefixed body).
 */
bool ws_read_startup_payload(int sock, char **payload, int32_t *payloadLen);
bool ws_write_raw_byte(int sock, char c);

/*
 * Post-startup framing: 1-byte type + int32 length (length includes
 * itself, matching the real protocol) + payload. ws_read_message
 * NUL-terminates the returned payload for convenience (Query message
 * bodies are C strings); callers that need the raw length still get it.
 */
bool ws_read_message(int sock, char *type, char **payload, int32_t *payloadLen,
					 int32_t maxLen);

/*
 * Maximum message body sizes, as in PostgreSQL's pq_getmessage() callers:
 * before authentication only small messages are ever legitimate (the SCRAM
 * 'p' messages, PQ_SMALL_MESSAGE_LIMIT-like; SCRAM_MAX_MESSAGE_LEN is 1024),
 * afterwards a simple query or CopyData from the client is at most 1 MiB
 * (PQ_LARGE_MESSAGE_LIMIT is 1 GB in PostgreSQL, far more than a
 * replication client ever sends). An oversize message gets a protocol
 * error and the connection is closed, without allocating.
 */
#define WS_MAX_AUTH_MESSAGE_LEN 1024
#define WS_MAX_COMMAND_MESSAGE_LEN (1024 * 1024)

/*
 * Set by a command that failed once it had started a COPY (or otherwise
 * left the protocol in a state it cannot resynchronize): PostgreSQL's
 * walsender ends the connection with a FATAL there rather than going back
 * to ReadyForQuery, and the command loop (accept_loop.c) checks this flag
 * after every dispatch.
 */
extern bool ws_connection_close_after_command;
bool ws_send_message(int sock, char type, const char *data, int32_t dataLen);

bool ws_send_authentication_ok(int sock);
bool ws_send_parameter_status(int sock, const char *name, const char *value);
bool ws_send_backend_key_data(int sock, int32_t pid, int32_t secret);
bool ws_send_negotiate_protocol_version(int sock, int32_t newestMinor,
										const char **unsupportedOptions,
										int nUnsupportedOptions);
bool ws_send_ready_for_query(int sock);
bool ws_send_error_response(int sock, const char *sqlstate, const char *message);
bool ws_send_command_complete(int sock, const char *tag);

bool ws_send_row_description(int sock, const WsColumn *columns, int ncols);
bool ws_send_data_row(int sock, const char **values, int ncols);

bool ws_send_copy_out_response(int sock, int ncols);
bool ws_send_copy_both_response(int sock, int ncols);
bool ws_send_copy_data(int sock, const char *data, int32_t dataLen);
bool ws_send_copy_done(int sock);

#endif /* WS_FRAMING_H */
