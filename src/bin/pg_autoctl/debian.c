/*
 * src/bin/pg_autoctl/debian.c
 *
 *   Debian specific code to support registering a pg_autoctl node from a
 *   Postgres cluster created with pg_createcluster. We need to move the
 *   configuration files back to PGDATA.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <libgen.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "debian.h"
#include "keeper.h"
#include "keeper_config.h"
#include "parsing.h"

#define EDITED_BY_PG_AUTOCTL "# edited by pg_auto_failover \n"

static bool debian_find_postgres_configuration_files(PostgresSetup *pgSetup,
													 PostgresConfigFiles *pgConfigFiles);

static bool debian_init_postgres_config_files(PostgresSetup *pgSetup,
											  PostgresConfigFiles *pgConfFiles,
											  PostgresConfigurationKind confKind);

static bool buildDebianDataAndConfDirectoryNames(PostgresSetup *pgSetup,
												 DebianPathnames *debPathnames);

static bool expandDebianPatterns(DebianPathnames *debPathnames,
								 const char *dataDirectoryTemplate,
								 const char *confDirectoryTemplate);

static bool expandDebianPatternsInDirectoryName(char *pathname,
												int pathnameSize,
												const char *template,
												const char *versionName,
												const char *clusterName);

static void initPostgresConfigFiles(const char *dirname,
									PostgresConfigFiles *pgConfigFiles,
									PostgresConfigurationKind kind);

static bool postgresConfigFilesAllExist(PostgresConfigFiles *pgConfigFiles);

static bool move_configuration_files(PostgresConfigFiles *src,
									 PostgresConfigFiles *dst);

static bool comment_out_configuration_parameters(const char *srcConfPath,
												 const char *dstConfPath);

static bool disableAutoStart(PostgresConfigFiles *pgConfigFiles);


/*
 * keeper_ensure_pg_configuration_files_in_pgdata checks if postgresql.conf,
 * pg_hba.conf, pg_ident.conf files exist in $PGDATA, if not it tries to get
 * them from default location and modifies paths inside copied postgresql.conf.
 */
bool
keeper_ensure_pg_configuration_files_in_pgdata(PostgresSetup *pgSetup)
{
	PostgresConfigFiles pgConfigFiles = { 0 };

	if (!debian_find_postgres_configuration_files(pgSetup, &pgConfigFiles))
	{
		/* errors have already been logged */
		return false;
	}

	switch (pgConfigFiles.kind)
	{
		case PG_CONFIG_TYPE_POSTGRES:
		{
			/* that's it, we're good */
			return true;
		}

		case PG_CONFIG_TYPE_DEBIAN:
		{
			/*
			 * So now pgConfigFiles is the debian path for configuration files,
			 * and we're building a new pgdataConfigFiles for the Postgres
			 * configuration files in PGDATA.
			 */
			PostgresConfigFiles pgdataConfigFiles = { 0 };

			log_info("Found a debian style installation in PGDATA \"%s\" with "
					 "postgresql.conf located at \"%s\"",
					 pgSetup->pgdata,
					 pgConfigFiles.conf);

			initPostgresConfigFiles(pgSetup->pgdata,
									&pgdataConfigFiles,
									PG_CONFIG_TYPE_POSTGRES);

			log_info("Moving configuration files back to PGDATA at \"%s\"",
					 pgSetup->pgdata);

			/* move configuration files back to PGDATA, or die trying */
			if (!move_configuration_files(&pgConfigFiles,
										  &pgdataConfigFiles))
			{
				char *_dirname = dirname(pgConfigFiles.conf);

				log_fatal("Failed to move the debian configuration files from "
						  "\"%s\" back to PGDATA at \"%s\"",
						  _dirname,
						  pgSetup->pgdata);
				return false;
			}

			/* also disable debian auto start of the cluster we now own */
			if (!disableAutoStart(&pgConfigFiles))
			{
				log_fatal("Failed to disable debian auto-start behavior, "
						  "see above for details");
				return false;
			}

			return true;
		}

		case PG_CONFIG_TYPE_UNKNOWN:
		{
			log_fatal("Failed to find the \"postgresql.conf\" file. "
					  "It's not in PGDATA, and it's not in the debian "
					  "place we had a look at. See above for details");
			return false;
		}
	}

	/* This is a huge bug */
	log_error("BUG: some unknown PG_CONFIG enum value was encountered");
	return false;
}


/*
 * debian_find_postgres_configuration_files finds the Postgres configuration
 * files following the following strategies:
 *
 *  - first attempt to find the files where we expect them, in PGDATA
 *  - then attempt to find the files in the debian /etc/postgresql/%v/%c place
 *
 * At the moment we only have those two strategies, and with some luck that's
 * all we're ever going to need.
 */
static bool
debian_find_postgres_configuration_files(PostgresSetup *pgSetup,
										 PostgresConfigFiles *pgConfigFiles)
{
	PostgresConfigFiles postgresConfFiles = { 0 };
	PostgresConfigFiles debianConfFiles = { 0 };

	pgConfigFiles->kind = PG_CONFIG_TYPE_UNKNOWN;

	if (!pg_setup_pgdata_exists(pgSetup))
	{
		return PG_CONFIG_TYPE_UNKNOWN;
	}

	/* is it a Postgres core initdb style setup? */
	if (debian_init_postgres_config_files(pgSetup,
										  &postgresConfFiles,
										  PG_CONFIG_TYPE_POSTGRES))
	{
		/* so we're dealing with a "normal" Postgres installation */
		*pgConfigFiles = postgresConfFiles;

		return true;
	}

	/*
	 * Is it a debian postgresql-common style setup then?
	 *
	 * We only search for debian style setup when the main postgresql.conf file
	 * was not found. The previous call to debian_init_postgres_config_files
	 * might see a partial failure because of e.g. missing only pg_ident.conf.
	 */
	if (!file_exists(postgresConfFiles.conf))
	{
		if (debian_init_postgres_config_files(pgSetup,
											  &debianConfFiles,
											  PG_CONFIG_TYPE_DEBIAN))
		{
			/* so we're dealing with a "debian style" Postgres installation */
			*pgConfigFiles = debianConfFiles;

			return true;
		}
	}

	/* well that's all we know how to detect at this point */
	return false;
}


/*
 * debian_init_postgres_config_files initializes the given PostgresConfigFiles
 * structure with the location of existing files as found on-disk given a
 * Postgres configuration kind.
 */
static bool
debian_init_postgres_config_files(PostgresSetup *pgSetup,
								  PostgresConfigFiles *pgConfigFiles,
								  PostgresConfigurationKind confKind)
{
	const char *pgdata = pgSetup->pgdata;

	switch (confKind)
	{
		case PG_CONFIG_TYPE_UNKNOWN:
		{
			/* that's a bug really */
			log_error("BUG: debian_init_postgres_config_files "
					  "called with UNKNOWN conf kind");
			return false;
		}

		case PG_CONFIG_TYPE_POSTGRES:
		{
			initPostgresConfigFiles(pgdata, pgConfigFiles,
									PG_CONFIG_TYPE_POSTGRES);

			return postgresConfigFilesAllExist(pgConfigFiles);
		}

		case PG_CONFIG_TYPE_DEBIAN:
		{
			DebianPathnames debPathnames = { 0 };

			if (!buildDebianDataAndConfDirectoryNames(pgSetup, &debPathnames))
			{
				log_warn("Failed to match PGDATA at \"%s\" with a debian "
						 "setup following the data_directory template "
						 "'/var/lib/postgresql/%%v/%%c'",
						 pgSetup->pgdata);
				return false;
			}

			initPostgresConfigFiles(debPathnames.confDirectory,
									pgConfigFiles, PG_CONFIG_TYPE_DEBIAN);

			return postgresConfigFilesAllExist(pgConfigFiles);
		}
	}

	/* This is a huge bug */
	log_error("BUG: some unknown PG_CONFIG enum value was encountered");
	return false;
}


/*
 * buildDebianDataAndConfDirectoryNames builds the debian specific directory
 * pathnames from the pgSetup pgdata location.
 *
 * For a debian cluster, we first have to extract the "cluster" name (%c) and
 * then find the configuration files in /etc/postgresql/%v/%c with %v being the
 * version number.
 *
 * Note that debian's /etc/postgresql-common/createcluster.conf defaults to
 * using the following setup, and that's the only one we support at this
 * moment.
 *
 *   data_directory = '/var/lib/postgresql/%v/%c'
 *
 */
static bool
buildDebianDataAndConfDirectoryNames(PostgresSetup *pgSetup,
									 DebianPathnames *debPathnames)
{
	char *pgmajor = strdup(pgSetup->pg_version);

	char pgdata[MAXPGPATH];

	char clusterDir[MAXPGPATH] = { 0 };
	char versionDir[MAXPGPATH] = { 0 };

	if (pgmajor == NULL)
	{
		log_error(ALLOCATION_FAILED_ERROR);
		return false;
	}

	/* we need to work with the absolute pathname of PGDATA */
	if (!normalize_filename(pgSetup->pgdata, pgdata, MAXPGPATH))
	{
		/* errors have already been logged */
		return false;
	}

	/* clusterDir is the same as pgdata really */
	strlcpy(clusterDir, pgdata, MAXPGPATH);

	/* from PGDATA, get the directory one-level up */
	strlcpy(versionDir, clusterDir, MAXPGPATH);
	get_parent_directory(versionDir);

	/* get the names of our version and cluster directories */
	char *clusterDirName = strdup(basename(clusterDir));
	char *versionDirName = strdup(basename(versionDir));

	if (clusterDirName == NULL || versionDirName == NULL)
	{
		log_error(ALLOCATION_FAILED_ERROR);
		return false;
	}

	/* transform pgversion "11.4" to "11" to get the major version part */
	char *dot = strchr(pgmajor, '.');

	if (dot)
	{
		*dot = '\0';
	}

	/* check that debian pathname version string == Postgres version string */
	if (strcmp(versionDirName, pgmajor) != 0)
	{
		log_debug("Failed to match the version component of the "
				  "debian data_directory \"%s\" with the current "
				  "version of Postgres: \"%s\"",
				  pgdata,
				  pgmajor);
		return false;
	}

	/* prepare given debPathnames */
	strlcpy(debPathnames->versionName, versionDirName, PG_VERSION_STRING_MAX);
	strlcpy(debPathnames->clusterName, clusterDirName, MAXPGPATH);

	if (!expandDebianPatterns(debPathnames,
							  "/var/lib/postgresql/%v/%c",
							  "/etc/postgresql/%v/%c"))
	{
		/* errors have already been logged */
		return false;
	}

	/* free memory allocated with strdup */
	free(pgmajor);
	free(clusterDirName);
	free(versionDirName);

	return true;
}


/*
 * expandDebianPatterns expands the %v and %c values in given templates and
 * apply the result to debPathnames->dataDirectory and
 * debPathnames->confDirectory.
 */
static bool
expandDebianPatterns(DebianPathnames *debPathnames,
					 const char *dataDirectoryTemplate,
					 const char *confDirectoryTemplate)
{
	return expandDebianPatternsInDirectoryName(debPathnames->dataDirectory,
											   MAXPGPATH,
											   dataDirectoryTemplate,
											   debPathnames->versionName,
											   debPathnames->clusterName)

		   && expandDebianPatternsInDirectoryName(debPathnames->confDirectory,
												  MAXPGPATH,
												  confDirectoryTemplate,
												  debPathnames->versionName,
												  debPathnames->clusterName);
}


/*
 * expandDebianPatternsInDirectoryName prepares a debian target data_directory
 * or configuration directory from a pattern.
 *
 * Given the parameters:
 *   template     = "/var/lib/postgresql/%v/%c"
 *   versionName  = "11"
 *   clusterName  = "main"
 *
 * Then the following string is copied in pre-allocated pathname:
 *   "/var/lib/postgresql/11/main"
 */
static bool
expandDebianPatternsInDirectoryName(char *pathname,
									int pathnameSize,
									const char *template,
									const char *versionName,
									const char *clusterName)
{
	int pathnameIndex = 0;
	int templateIndex = 0;
	int templateSize = strlen(template);
	bool previousCharIsPercent = false;

	for (templateIndex = 0; templateIndex < templateSize; templateIndex++)
	{
		char currentChar = template[templateIndex];

		if (pathnameIndex >= pathnameSize)
		{
			log_error("BUG: expandDebianPatternsInDirectoryName destination "
					  "buffer is too short (%d bytes)", pathnameSize);
			return false;
		}

		if (previousCharIsPercent)
		{
			switch (currentChar)
			{
				case 'v':
				{
					int versionSize = strlen(versionName);

					/*
					 * Only copy if we have enough room, increment pathnameSize
					 * anyways so that the first check in the main loop catches
					 * and report the error.
					 */
					if ((pathnameIndex + versionSize) < pathnameSize)
					{
						strlcpy(pathname + pathnameIndex, versionName, pathnameSize -
								pathnameIndex);
						pathnameIndex += versionSize;
					}
					break;
				}

				case 'c':
				{
					int clusterSize = strlen(clusterName);

					/*
					 * Only copy if we have enough room, increment pathnameSize
					 * anyways so that the first check in the main loop catches
					 * and report the error.
					 */
					if ((pathnameIndex + clusterSize) < pathnameSize)
					{
						strlcpy(pathname + pathnameIndex, clusterName, pathnameSize -
								pathnameIndex);
						pathnameIndex += clusterSize;
					}
					break;
				}

				default:
				{
					pathname[pathnameIndex++] = currentChar;
					break;
				}
			}
		}
		else if (currentChar != '%')
		{
			pathname[pathnameIndex++] = currentChar;
		}

		previousCharIsPercent = currentChar == '%';
	}

	return true;
}


/*
 * initPostgresConfigFiles initializes PostgresConfigFiles structure with our
 * filenames located in given directory pathname.
 */
static void
initPostgresConfigFiles(const char *dirname,
						PostgresConfigFiles *pgConfigFiles,
						PostgresConfigurationKind confKind)
{
	pgConfigFiles->kind = confKind;
	join_path_components(pgConfigFiles->conf, dirname, "postgresql.conf");
	join_path_components(pgConfigFiles->ident, dirname, "pg_ident.conf");
	join_path_components(pgConfigFiles->hba, dirname, "pg_hba.conf");
}


/*
 * postgresConfigFilesAllExist returns true when the three files that we track
 * all exit on the file system, per file_exists() test.
 */
static bool
postgresConfigFilesAllExist(PostgresConfigFiles *pgConfigFiles)
{
	/*
	 * WARN the user about the unexpected nature of our setup here, even if we
	 * then move on to make it the way we expect it.
	 */
	if (!file_exists(pgConfigFiles->conf))
	{
		log_warn("Failed to find Postgres configuration files in PGDATA, "
				 "as expected: \"%s\" does not exist",
				 pgConfigFiles->conf);
	}

	if (!file_exists(pgConfigFiles->ident))
	{
		log_warn("Failed to find Postgres configuration files in PGDATA, "
				 "as expected: \"%s\" does not exist",
				 pgConfigFiles->ident);
	}

	if (!file_exists(pgConfigFiles->hba))
	{
		log_warn("Failed to find Postgres configuration files in PGDATA, "
				 "as expected: \"%s\" does not exist",
				 pgConfigFiles->hba);
	}

	return file_exists(pgConfigFiles->conf) &&
		   file_exists(pgConfigFiles->ident) &&
		   file_exists(pgConfigFiles->hba);
}


/*
 * move_configuration_files moves configuration files from the source place to
 * the destination place as given.
 *
 * While moving the files, we also need to edit the "postgresql.conf" content
 * to comment out the lines for the config_file, hba_file, and ident_file
 * location. We're going to use the Postgres defaults in PGDATA.
 */
static bool
move_configuration_files(PostgresConfigFiles *src, PostgresConfigFiles *dst)
{
	/* edit postgresql.conf and move it to its dst pathname */
	log_info("Preparing \"%s\" from \"%s\"", dst->conf, src->conf);

	if (!comment_out_configuration_parameters(src->conf, dst->conf))
	{
		return false;
	}

	/* HBA and ident files are copied without edits */
	log_info("Moving \"%s\" to \"%s\"", src->hba, dst->hba);

	if (!move_file(src->hba, dst->hba))
	{
		/*
		 * Clean-up the mess then, and return false whether the clean-up is a
		 * success or not.
		 */
		(void) unlink_file(dst->conf);

		return false;
	}


	/* HBA and ident files are copied without edits */
	log_info("Moving \"%s\" to \"%s\"", src->ident, dst->ident);

	if (!move_file(src->ident, dst->ident))
	{
		/*
		 * Clean-up the mess then, and return false whether the clean-up is a
		 * success or not.
		 */
		(void) unlink_file(dst->conf);
		(void) move_file(dst->hba, src->hba);

		return false;
	}

	/* finish the move of the postgresql.conf */
	if (!unlink_file(src->conf))
	{
		/*
		 * Clean-up the mess then, and return false whether the clean-up is a
		 * success or not.
		 */
		(void) move_file(dst->hba, src->hba);
		(void) move_file(dst->ident, src->ident);
		return false;
	}

	/* consider failure to symlink as a non-fatal event */
	(void) create_symbolic_link(src->conf, dst->conf);
	(void) create_symbolic_link(src->ident, dst->ident);
	(void) create_symbolic_link(src->hba, dst->hba);

	return true;
}


/*
 * comment_out_configuration_parameters reads postgresql.conf file from source
 * location and writes a new version of it at destination location with some
 * parameters commented out:
 *
 *  data_directory
 *  config_file
 *  hba_file
 *  ident_file
 *  include_dir
 */
static bool
comment_out_configuration_parameters(const char *srcConfPath,
									 const char *dstConfPath)
{
	char lineBuffer[BUFSIZE];

	/*
	 * configuration parameters can appear in any order, and we
	 * need to check for patterns for NAME = VALUE and NAME=VALUE
	 */
	char *targetVariableExpression =
		"("
		"data_directory"
		"|hba_file"
		"|ident_file"
		"|include_dir"
		"|stats_temp_directory"
		")( *)=";

	/* open a file */
	FILE *fileStream = fopen_read_only(srcConfPath);
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %m", srcConfPath);
		return false;
	}

	PQExpBuffer newConfContents = createPQExpBuffer();
	if (newConfContents == NULL)
	{
		log_error("Failed to allocate memory");
		return false;
	}

	/* read each line including terminating new line and process it */
	while (fgets(lineBuffer, BUFSIZE, fileStream) != NULL)
	{
		bool variableFound = false;
		char *matchedString =
			regexp_first_match(lineBuffer, targetVariableExpression);

		/* check if the line contains any of target variables */
		if (matchedString != NULL)
		{
			variableFound = true;

			/* regexp_first_match uses malloc, result must be deallocated */
			free(matchedString);
		}

		/*
		 * comment out the line if any of target variables is found
		 * and if it was not already commented
		 */
		if (variableFound && lineBuffer[0] != '#')
		{
			appendPQExpBufferStr(newConfContents, EDITED_BY_PG_AUTOCTL);
			appendPQExpBufferStr(newConfContents, "# ");
		}

		/* copy rest of the line */
		appendPQExpBufferStr(newConfContents, lineBuffer);
	}

	fclose(fileStream);

	/* write the resulting content at the destination path */
	if (!write_file(newConfContents->data, newConfContents->len, dstConfPath))
	{
		destroyPQExpBuffer(newConfContents);
		return false;
	}

	/* we don't need the buffer anymore */
	destroyPQExpBuffer(newConfContents);

	/*
	 * Refrain from removing the source file, we might fail to proceed and then
	 * we will want to offer a path forward to the user where the original
	 * configuration file is still around
	 */

	return true;
}


/*
 * disableAutoStart disables auto start in default configuration
 */
static bool
disableAutoStart(PostgresConfigFiles *pgConfigFiles)
{
	char startConfPath[MAXPGPATH] = { 0 };
	char copyStartConfPath[MAXPGPATH] = { 0 };
	char *newStartConfData = EDITED_BY_PG_AUTOCTL "disabled";

	path_in_same_directory(pgConfigFiles->conf, "start.conf", startConfPath);
	path_in_same_directory(pgConfigFiles->conf,
						   "start.conf.orig", copyStartConfPath);

	if (rename(startConfPath, copyStartConfPath) != 0)
	{
		log_error("Failed to rename debian auto start setup to \"%s\": %m",
				  copyStartConfPath);

		return false;
	}

	return write_file(newStartConfData, strlen(newStartConfData), startConfPath);
}
