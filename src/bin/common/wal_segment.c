/*
 * src/bin/common/wal_segment.c
 *   See wal_segment.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "postgres_fe.h"

#include "file_utils.h"
#include "parsing.h"
#include "system_utils.h"
#include "wal_segment.h"

#define WS_WAL_FNAME_LEN 24


/*
 * wal_segments_per_xlogid mirrors XLogSegmentsPerXLogId(wal_segsz_bytes)
 * (xlog_internal.h:99-100): how many segments make up one "logId" --
 * the high 32 bits of a segment number, in the traditional
 * "%08X%08X%08X" (tli/logId/seg) WAL filename split.
 */
uint64_t
wal_segments_per_xlogid(uint64_t segSize)
{
	return UINT64CONST(0x100000000) / segSize;
}


/*
 * wal_segment_name_is_valid mirrors IsXLogFileName (xlog_internal.h:
 * 178-183): true when name is exactly 24 hexadecimal digits, no more, no
 * less (so a ".partial"/".backup"/etc suffixed name is excluded).
 */
bool
wal_segment_name_is_valid(const char *name)
{
	return strlen(name) == WS_WAL_FNAME_LEN &&
		   strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN;
}


/*
 * wal_segment_name_is_partial mirrors IsPartialXLogFileName
 * (xlog_internal.h:190-196): true when name is a complete WAL segment
 * name (wal_segment_name_is_valid()) immediately followed by ".partial" --
 * the shape pg_receivewal (and this project's own embedded receivewal
 * worker) uses for a segment still being written.
 */
bool
wal_segment_name_is_partial(const char *name)
{
	const char *partialSuffix = ".partial";
	size_t partialLen = strlen(partialSuffix);

	return strlen(name) == WS_WAL_FNAME_LEN + partialLen &&
		   strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN &&
		   strcmp(name + WS_WAL_FNAME_LEN, partialSuffix) == 0;
}


/*
 * wal_backup_history_name_is_valid mirrors IsBackupHistoryFileName
 * (xlog_internal.h:251-257): true when name has the
 * "<24hex><8hex>.backup" shape pg_basebackup's own backup history file
 * uses. Deliberately matches upstream's own lenient suffix-only check
 * (anything of length > 24 hex digits ending in ".backup", not a fixed
 * total length) -- see this function's own callers for which shape each
 * one actually needs.
 */
bool
wal_backup_history_name_is_valid(const char *name)
{
	const char *backupSuffix = ".backup";
	size_t len = strlen(name);
	size_t suffixLen = strlen(backupSuffix);

	return len > WS_WAL_FNAME_LEN &&
		   strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN &&
		   len > suffixLen &&
		   strcmp(name + len - suffixLen, backupSuffix) == 0;
}


/*
 * wal_segment_name_format mirrors XLogFileName (xlog_internal.h:164-170):
 * formats the 24-hex-digit WAL segment filename for timeline/segno/segSize
 * into dest (at least 25 bytes). Argument order is timeline, segno,
 * segSize, dest, destSize -- dest/destSize last, matching this project's
 * other WAL-segment functions (unlike sformat(), which takes dest first).
 */
void
wal_segment_name_format(uint32_t timeline, uint64_t segno, uint64_t segSize,
						char *dest, size_t destSize)
{
	uint64_t perXLogId = wal_segments_per_xlogid(segSize);
	uint32_t logId = (uint32_t) (segno / perXLogId);
	uint32_t seg = (uint32_t) (segno % perXLogId);

	sformat(dest, destSize, "%08X%08X%08X", timeline, logId, seg);
}


/*
 * wal_segment_name_parse mirrors XLogFromFileName (xlog_internal.h:
 * 198-206): decodes a 24-hex WAL segment name (or the leading 24-hex
 * prefix of a ".partial"/".backup" name) back into its own timeline and
 * 0-based segment number. Does not itself validate name's shape -- callers
 * that need that check first via wal_segment_name_is_valid()/_is_partial()/
 * wal_backup_history_name_is_valid(), exactly as upstream's own
 * XLogFromFileName() callers already validate via IsXLogFileName() first.
 */
void
wal_segment_name_parse(const char *name, uint64_t segSize,
					   uint32_t *timelineOut, uint64_t *segnoOut)
{
	uint64_t perXLogId = wal_segments_per_xlogid(segSize);

	char tliHex[9] = { 0 };
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(tliHex, name, 8); /* IGNORE-BANNED */
	memcpy(logIdHex, name + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, name + 16, 8); /* IGNORE-BANNED */

	uint32_t tli = (uint32_t) strtoul(tliHex, NULL, 16);
	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	*timelineOut = tli;
	*segnoOut = (uint64_t) logId * perXLogId + seg;
}


/*
 * wal_segment_name_extract_prefix extracts the 24-hex WAL-segment prefix a
 * filename's retention/inventory decision is keyed on, recognizing the
 * three shapes real pg_archivecleanup's own
 * SetWALFileNameForCleanup()/CleanupPriorWALFiles()
 * (src/bin/pg_archivecleanup/pg_archivecleanup.c) recognize: a plain
 * segment (wal_segment_name_is_valid()), a ".partial" segment
 * (wal_segment_name_is_partial()), or a "<24hex>.<8hex>.backup" backup
 * history file (wal_backup_history_name_is_valid(), with the stricter
 * exact-length shape this project's callers need -- see that function's
 * own comment). prefixOut (at least 25 bytes) receives the 24-hex prefix
 * on success. Returns false (not one of these three shapes) for anything
 * else, including a "<8hex>.history" timeline history file, which carries
 * no WAL-segment prefix of this kind at all.
 */
bool
wal_segment_name_extract_prefix(const char *name, char *prefixOut)
{
	if (wal_segment_name_is_valid(name) || wal_segment_name_is_partial(name))
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	/*
	 * "<24hex>.<8hex>.backup": the exact shape real BackupHistoryFileName()
	 * produces (xlog_internal.h), stricter than wal_backup_history_name_
	 * is_valid()'s own upstream-mirrored, suffix-only check -- this
	 * project's own retention/inventory callers need the exact shape to
	 * safely pull out the 8-hex "offset" component, not merely confirm a
	 * ".backup" suffix.
	 */
	size_t len = strlen(name);
	const char *backupSuffix = ".backup";

	if (len == WS_WAL_FNAME_LEN + 1 + 8 + strlen(backupSuffix) &&
		name[WS_WAL_FNAME_LEN] == '.' &&
		strspn(name, "0123456789ABCDEF") == WS_WAL_FNAME_LEN &&
		strspn(name + WS_WAL_FNAME_LEN + 1, "0123456789ABCDEF") == 8 &&
		strcmp(name + WS_WAL_FNAME_LEN + 1 + 8, backupSuffix) == 0)
	{
		memcpy(prefixOut, name, WS_WAL_FNAME_LEN); /* IGNORE-BANNED */
		prefixOut[WS_WAL_FNAME_LEN] = '\0';
		return true;
	}

	return false;
}


/*
 * wal_lsn_to_segno mirrors XLByteToSeg (xlog_internal.h:116-117) composed
 * with real Postgres's own pg_parse_lsn validation rule (this project's
 * own parseLSN(), src/bin/common/parsing.c): parses lsn ("%X/%X") and
 * divides it by segSize to get its 0-based segment number. Returns false
 * (segnoOut untouched) when lsn fails to parse -- parseLSN()'s own
 * validation (1-8 hex digits per component, exactly one '/', no trailing
 * junk) is strictly more careful than a bare "%X/%X" scanf-style parse
 * would be.
 */
bool
wal_lsn_to_segno(const char *lsn, uint64_t segSize, uint64_t *segnoOut)
{
	uint64_t lsnValue;

	if (!parseLSN(lsn, &lsnValue))
	{
		return false;
	}

	*segnoOut = lsnValue / segSize;

	return true;
}


/*
 * wal_lsn_to_segment_name composes wal_lsn_to_segno() and
 * wal_segment_name_format(): parses lsn, divides by segSize, and formats
 * the resulting segment number (on timeline) into segmentOut (at least 25
 * bytes). Returns false (segmentOut untouched) exactly when wal_lsn_to_
 * segno() itself would.
 */
bool
wal_lsn_to_segment_name(const char *lsn, uint32_t timeline, uint64_t segSize,
						char *segmentOut, size_t segmentOutSize)
{
	uint64_t segno;

	if (!wal_lsn_to_segno(lsn, segSize, &segno))
	{
		return false;
	}

	wal_segment_name_format(timeline, segno, segSize, segmentOut, segmentOutSize);

	return true;
}


/*
 * wal_segment_size_string formats segSize (a byte count) the way the
 * wal_segment_size GUC prints it ("16MB", "1GB"), the format
 * pg_receivewal/pg_basebackup's own RetrieveWalSegSize() expects to parse
 * back out of a SHOW wal_segment_size reply. Distinct from common/
 * system_utils.c's pretty_print_bytes(), which uses a space ("16 MB") for
 * human display -- not interchangeable with this wire-format string.
 *
 * Built on common/system_utils.c's own pretty_print_bytes_scaled(), the
 * same unit-scaling mechanism pretty_print_bytes() uses, but switching
 * units at exactly 1024 rather than 10240: segSize is always an exact
 * power of two between 1 MiB and 1 GiB (see ws_cluster_wal_segment_size()'s
 * own validation), so this reproduces the GUC's own "16MB"/"1GB" wire
 * format exactly, with no rounding -- equivalent to, and verified against,
 * the previous direct bit-shift (segSize >> 20 / segSize >> 30)
 * implementation for every valid segment size.
 */
void
wal_segment_size_string(uint64_t segSize, char *dest, size_t destSize)
{
	pretty_print_bytes_scaled(dest, destSize, segSize, 1024, false);
}


#define WS_MAX_TIMELINE_CHAIN 64

/*
 * wal_history_read_last_line reads "<clusterPath>/%08X.history" (timeline)
 * and returns, in *parentTliOut/lsnOut, the parent timeline and switchpoint
 * LSN from its last non-blank, non-comment line -- the entry that records
 * where *this* timeline itself branched off from *parentTliOut* (a history
 * file may carry more than one line, one per ancestor further back, but
 * the last line is always the immediate parent, exactly how real
 * Postgres's own readTimeLineHistory()/tliOfPointInHistory() reasoning
 * works). Returns false if the file is missing, empty, or has no
 * parseable line.
 */
bool
wal_history_read_last_line(const char *clusterPath, uint32_t timeline,
						   uint32_t *parentTliOut, char *lsnOut,
						   size_t lsnOutSize)
{
	char path[MAXPGPATH] = { 0 };

	sformat(path, sizeof(path), "%s/%08X.history", clusterPath, timeline);

	char *contents = NULL;
	long size = 0;

	if (!read_file_if_exists(path, &contents, &size) ||
		contents == NULL || size == 0)
	{
		return false;
	}

	bool found = false;
	char *line = contents;

	while (line != NULL && *line != '\0')
	{
		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		char *p = line;

		while (*p == ' ' || *p == '\t')
		{
			p++;
		}

		if (*p != '\0' && *p != '#')
		{
			unsigned int tli;
			char lsn[64] = { 0 };

			if (sscanf(p, "%u\t%63s", &tli, lsn) == 2 || /* IGNORE-BANNED */
				sscanf(p, "%u %63s", &tli, lsn) == 2) /* IGNORE-BANNED */
			{
				*parentTliOut = (uint32_t) tli;
				strlcpy(lsnOut, lsn, lsnOutSize);
				found = true;
			}
		}

		line = (nl != NULL) ? nl + 1 : NULL;
	}

	free(contents);

	return found;
}


/*
 * wal_check_range verifies that every WAL segment number from startSegno
 * (on startTli) through endSegno (on endTli, inclusive) is present on
 * disk under clusterPath, resolving any intervening timeline switch(es) via
 * "%08X.history" files. On the first missing segment, or the first
 * ancestry fact that can't be established, fills *problem and returns --
 * callers only need to check problem->hasProblem.
 */
void
wal_check_range(const char *clusterPath, uint64_t segSize,
				uint32_t startTli, uint64_t startSegno,
				uint32_t endTli, uint64_t endSegno,
				WalContinuityProblem *problem)
{
	problem->hasProblem = false;
	problem->detail[0] = '\0';

	if (endTli < startTli)
	{
		sformat(problem->detail, sizeof(problem->detail),
				"cannot verify WAL continuity: the newer boundary is on "
				"timeline %u, older than the earlier boundary's timeline "
				"%u -- this should never happen",
				endTli, startTli);
		problem->hasProblem = true;
		return;
	}

	uint32_t chainTli[WS_MAX_TIMELINE_CHAIN];
	uint64_t chainLower[WS_MAX_TIMELINE_CHAIN];
	int chainLen = 1;

	chainTli[0] = endTli;

	uint32_t cur = endTli;

	while (cur != startTli)
	{
		uint32_t parentTli = 0;
		char lsn[64] = { 0 };

		if (!wal_history_read_last_line(clusterPath, cur, &parentTli, lsn, sizeof(lsn)))
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity across a timeline switch: "
					"\"%08X.history\" is missing or unreadable under \"%s\", "
					"needed to confirm timeline %u's own ancestry back to "
					"timeline %u", cur, clusterPath, cur, startTli);
			problem->hasProblem = true;
			return;
		}

		uint64_t switchSegno;

		if (!wal_lsn_to_segno(lsn, segSize, &switchSegno))
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity: \"%08X.history\" under "
					"\"%s\" has an unparseable switchpoint LSN (\"%s\")",
					cur, clusterPath, lsn);
			problem->hasProblem = true;
			return;
		}

		chainLower[chainLen - 1] = switchSegno;

		if (parentTli >= cur || parentTli < startTli)
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity: \"%08X.history\" under "
					"\"%s\" names an implausible parent timeline %u",
					cur, clusterPath, parentTli);
			problem->hasProblem = true;
			return;
		}

		cur = parentTli;

		if (chainLen >= WS_MAX_TIMELINE_CHAIN)
		{
			sformat(problem->detail, sizeof(problem->detail),
					"cannot verify WAL continuity: more than %d timeline "
					"switches between timeline %u and timeline %u",
					WS_MAX_TIMELINE_CHAIN, startTli, endTli);
			problem->hasProblem = true;
			return;
		}

		chainTli[chainLen] = cur;
		chainLen++;
	}

	chainLower[chainLen - 1] = startSegno;

	/* walk oldest (startTli) to newest (endTli), each timeline owning the
	 * segment-number range [chainLower[k], next boundary) within the
	 * overall [startSegno, endSegno] range being checked */
	for (int k = chainLen - 1; k >= 0; k--)
	{
		uint64_t lower = chainLower[k];

		/* an older timeline whose immediate successor switched away at
		 * segno 0 owns nothing at all within this range -- guard the
		 * subtraction below rather than underflow an unsigned bound */
		if (k > 0 && chainLower[k - 1] == 0)
		{
			continue;
		}

		uint64_t upper = (k == 0) ? endSegno : (chainLower[k - 1] - 1);

		if (lower > upper)
		{
			continue;
		}

		for (uint64_t segno = lower; segno <= upper; segno++)
		{
			char segName[WS_WAL_FNAME_LEN + 1] = { 0 };

			wal_segment_name_format(chainTli[k], segno, segSize,
									segName, sizeof(segName));

			char segPath[MAXPGPATH] = { 0 };

			sformat(segPath, sizeof(segPath), "%s/%s", clusterPath, segName);

			if (!file_exists(segPath))
			{
				sformat(problem->detail, sizeof(problem->detail),
						"missing WAL segment \"%s\" (needed between "
						"\"%08X%08X%08X\" and \"%08X%08X%08X\")",
						segName,
						startTli, (uint32_t) (startSegno >> 32),
						(uint32_t) startSegno,
						endTli, (uint32_t) (endSegno >> 32),
						(uint32_t) endSegno);
				problem->hasProblem = true;
				return;
			}
		}
	}
}


/*
 * wal_check_continuity runs wal_check_range() (above) for a chain of
 * already-resolved kept-backup WAL positions, ordered oldest (entries[0])
 * to newest (entries[entryCount - 1]): entry k's own end boundary is entry
 * k + 1's own start boundary, except for the newest entry, whose end
 * boundary is (latestTli, latestEndSegno) when haveLatest is true, or, when
 * haveLatest is false (nothing on disk to compare against at all -- a
 * brand new cluster, or every recognizable complete segment is gone), the
 * least that can still be required: that entry's own start segment is
 * itself still present.
 *
 * problems (entryCount-long, caller-allocated) receives one
 * WalContinuityProblem per entry, matched by position;
 * problems[k].hasProblem is false when entry k has no issue. Returns false
 * when any entry has a problem. Logging-free: callers decide how (and
 * whether) to report each problem, e.g. naming the specific backup/cluster
 * a generic entry corresponds to.
 */
bool
wal_check_continuity(const char *clusterPath, uint64_t segSize,
					 const WalContinuityEntry *entries, int entryCount,
					 bool haveLatest, uint32_t latestTli,
					 uint64_t latestEndSegno, WalContinuityProblem *problems)
{
	bool ok = true;

	for (int k = 0; k < entryCount; k++)
	{
		problems[k].hasProblem = false;
		problems[k].detail[0] = '\0';

		uint32_t endTli;
		uint64_t endSegno;
		bool haveEnd;

		if (k + 1 < entryCount)
		{
			endTli = entries[k + 1].startTli;
			endSegno = entries[k + 1].startSegno;
			haveEnd = true;
		}
		else
		{
			haveEnd = haveLatest;
			endTli = latestTli;
			endSegno = latestEndSegno;
		}

		if (!haveEnd)
		{
			char segName[WS_WAL_FNAME_LEN + 1] = { 0 };

			wal_segment_name_format(entries[k].startTli, entries[k].startSegno,
									segSize, segName, sizeof(segName));

			char segPath[MAXPGPATH] = { 0 };

			sformat(segPath, sizeof(segPath), "%s/%s", clusterPath, segName);

			if (!file_exists(segPath))
			{
				sformat(problems[k].detail, sizeof(problems[k].detail),
						"its own required starting WAL segment \"%s\" is "
						"missing, and no WAL segment at all is present "
						"under \"%s\" to compare against", segName,
						clusterPath);
				problems[k].hasProblem = true;
				ok = false;
			}

			continue;
		}

		wal_check_range(clusterPath, segSize, entries[k].startTli,
						entries[k].startSegno, endTli, endSegno,
						&(problems[k]));

		if (problems[k].hasProblem)
		{
			ok = false;
		}
	}

	return ok;
}


/*
 * wal_backup_label_parse_time parses this project's own base backup
 * directory naming scheme, "basebackup-<UTC timestamp>Z" (cli_basebackup.c's
 * own strftime("basebackup-%Y%m%dT%H%M%SZ", ...)), back into a time_t,
 * entirely with a fixed-format scan (IGNORE-BANNED below) and timegm() --
 * the same fixed-format-string style this project's own backup_label
 * parsing (cmd_base_backup.c's read_backup_label()) already uses, rather
 * than introducing strptime().
 */
bool
wal_backup_label_parse_time(const char *label, time_t *takenAt)
{
	const char *prefix = "basebackup-";
	size_t prefixLen = strlen(prefix);

	if (strncmp(label, prefix, prefixLen) != 0)
	{
		return false;
	}

	int y, mo, d, h, mi, s;

	if (sscanf(label + prefixLen, "%4d%2d%2dT%2d%2d%2dZ", /* IGNORE-BANNED */
			   &y, &mo, &d, &h, &mi, &s) != 6)
	{
		return false;
	}

	struct tm tm = { 0 };

	tm.tm_year = y - 1900;
	tm.tm_mon = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min = mi;
	tm.tm_sec = s;

	*takenAt = timegm(&tm);

	return *takenAt != (time_t) -1;
}
