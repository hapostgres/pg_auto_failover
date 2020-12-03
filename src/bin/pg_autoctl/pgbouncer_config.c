/*
 * src/bin/pg_autoctl/pgbouncer_config.c
 *     Pgbouncer configuration functions
 *
 * Copyright (c) XXX: FIll in As requested
 * Licensed under the PostgreSQL License.
 *
 */

#include "pgbouncer_config.h"

#include "ini_file.h"
#include "log.h"
#include "snprintf.h"

/*
 * We are intentionally not reading/writing the following values:
 *
 *	conffile: show location of current config file. Changing it will make
 *		PgBouncer use another config file for next RELOAD / SIGHUP.
 *  resolv_conf: The location of a custom resolv.conf file. This is to allow
 *      specifying custom DNS servers and perhaps other name resolution options,
 *      independent of the global operating system configuration.
 *      The parsing of the file is done by the DNS backend library, not
 *      PgBouncer, so see the library’s documentation for details on allowed
 *      syntax and directives.
 *	user: If set, specifies the Unix user to change to after startup. Works only
 *		if PgBouncer is started as root or if it’s already running as given
 *		user. Not supported on Windows.
 *
 * We are intentionally not reading the following values:
 *	server_tls_ca_file, server_tls_cert_file, server_tls_key_file
 *  unix_socket_dir: Specifies location for Unix sockets. Applies to both
 *      listening socket and server connections. If set to an empty string, Unix
 *      sockets are disabled.
 *	unix_socket_group: Group name to use for Unix socket.
 *	unix_socket_mode: File system mode for Unix socket
 *	logfile: Specifies the log file
 *	pidfile: Specifies the PID file
 *
 * We are intentionally overwritting the following values:
 *	auth_hba_file, auth_file
 *	client_tls_ca_file, client_tls_cert_file, client_tls_key_file
 *
 * We are intentionally not reading the entire databases section.
 *
 * If it is desired to not write/read a value, then simply remove the
 * make_*_option macro corresponding to said value from the
 * SET_PGBOUNCER_INI_OPTIONS_ARRAY macro. Also remove it from the PgbouncerIni
 * struct.
 *
 * If it is desired to overwrite a value, then simply change its value. Usually
 * handled in the call of pgbouncer_config_write_template, because we probably
 * want to handle at the same time the any values that point to files that will
 * be handled by us.
 *
 * If it is desired to not read a value, but handle it during the runtime,
 * then do as above and then add the correspoding make_*_option in the
 * SET_PGBOUNCER_RUNTIME_INI_OPTIONS_ARRAY macro. The values should be
 * calculated in the call of pgbouncer_config_write_runtime.
 */

struct PgbouncerIni
{
	/* template sections */

	/* users section */
	int max_user_connections;

	char *pool_mode;

	/* pgbouncer section */
	int application_name_add_host;
	int autodb_idle_timeout;
	int client_idle_timeout;
	int client_login_timeout;
	int default_pool_size;
	int disable_pqexec;
	int dns_max_ttl;
	int dns_nxdomain_ttl;
	int dns_zone_check_period;
	int idle_transaction_timeout;
	int listen_backlog;
	int listen_port;
	int log_connections;
	int log_disconnections;
	int log_pooler_errors;
	int log_stats;
	int max_client_conn;
	int max_db_connections;
	int max_packet_size;
	int min_pool_size;
	int pkt_buf;
	int query_timeout;
	int query_wait_timeout;
	int reserve_pool_size;
	int reserve_pool_timeout;
	int sbuf_loopcnt;
	int server_check_delay;
	int server_connect_timeout;
	int server_fast_close;
	int server_idle_timeout;
	int server_lifetime;
	int server_login_retry;
	int server_reset_query_always;
	int server_round_robin;
	int so_reuseport;
	int stats_period;
	int suspend_timeout;
	int tcp_keepalive;
	int tcp_keepcnt;
	int tcp_keepidle;
	int tcp_keepintvl;
	int tcp_socket_buffer;
	int tcp_user_timeout;
	int verbose;

	char *admin_users;
	char *auth_file;
	char *auth_query;
	char *auth_type;
	char *auth_user;
	char *client_tls_ciphers;
	char *client_tls_dheparams;
	char *client_tls_ecdhcurve;
	char *client_tls_protocols;
	char *client_tls_sslmode;
	char *ignore_startup_parameters;
	char *job_name;
	char *listen_addr;
	char *server_check_query;
	char *server_reset_query;
	char *service_name;
	char *stats_users;
	char *syslog_facility;
	char *syslog_ident;
	char *tcp_defer_accept;

	/* runtime sections */
	/* pgbouncer section */
	char *logfile;
	char *pidfile;

	char *server_tls_ciphers;
	char *server_tls_protocols;
	char *server_tls_sslmode;

	/* database section */
	char *connection;
	char *dbname;
};

#define SET_PGBOUNCER_INI_OPTIONS_ARRAY(pgbouncerIni) \
	{ \
		make_int_option("users", "max_user_connections", \
						NULL, false, \
						&(pgbouncerIni->max_user_connections)), \
		make_string_option("users", "pool_mode", \
						   NULL, false, \
						   &(pgbouncerIni->pool_mode)), \
		make_string_option("pgbouncer", "admin_users", \
						   NULL, false, \
						   &(pgbouncerIni->admin_users)), \
		make_int_option("pgbouncer", "application_name_add_host", \
						NULL, false, \
						&(pgbouncerIni->application_name_add_host)), \
		make_string_option("pgbouncer", "auth_query", \
						   NULL, false, \
						   &(pgbouncerIni->auth_query)), \
		make_string_option("pgbouncer", "auth_file", \
						   NULL, false, \
						   &(pgbouncerIni->auth_file)), \
		make_string_option("pgbouncer", "auth_type", \
						   NULL, false, \
						   &(pgbouncerIni->auth_type)), \
		make_string_option("pgbouncer", "auth_user", \
						   NULL, false, \
						   &(pgbouncerIni->auth_user)), \
		make_int_option("pgbouncer", "autodb_idle_timeout", \
						NULL, false, \
						&(pgbouncerIni->autodb_idle_timeout)), \
		make_int_option("pgbouncer", "client_idle_timeout", \
						NULL, false, \
						&(pgbouncerIni->client_idle_timeout)), \
		make_int_option("pgbouncer", "client_login_timeout", \
						NULL, false, \
						&(pgbouncerIni->client_login_timeout)), \
		make_string_option("pgbouncer", "client_tls_ciphers", \
						   NULL, false, \
						   &(pgbouncerIni->client_tls_ciphers)), \
		make_string_option("pgbouncer", "client_tls_dheparams", \
						   NULL, false, \
						   &(pgbouncerIni->client_tls_dheparams)), \
		make_string_option("pgbouncer", "client_tls_ecdhcurve", \
						   NULL, false, \
						   &(pgbouncerIni->client_tls_ecdhcurve)), \
		make_string_option("pgbouncer", "client_tls_protocols", \
						   NULL, false, \
						   &(pgbouncerIni->client_tls_protocols)), \
		make_string_option("pgbouncer", "client_tls_sslmode", \
						   NULL, false, \
						   &(pgbouncerIni->client_tls_sslmode)), \
		make_int_option("pgbouncer", "default_pool_size", \
						NULL, false, \
						&(pgbouncerIni->default_pool_size)), \
		make_int_option("pgbouncer", "disable_pqexec", \
						NULL, false, \
						&(pgbouncerIni->disable_pqexec)), \
		make_int_option("pgbouncer", "dns_max_ttl", \
						NULL, false, \
						&(pgbouncerIni->dns_max_ttl)), \
		make_int_option("pgbouncer", "dns_nxdomain_ttl", \
						NULL, false, \
						&(pgbouncerIni->dns_zone_check_period)), \
		make_int_option("pgbouncer", "idle_transaction_timeout", \
						NULL, false, \
						&(pgbouncerIni->idle_transaction_timeout)), \
		make_string_option("pgbouncer", "ignore_startup_parameters", \
						   NULL, false, \
						   &(pgbouncerIni->ignore_startup_parameters)), \
		make_string_option("pgbouncer", "job_name", \
						   NULL, false, \
						   &(pgbouncerIni->job_name)), \
		make_string_option("pgbouncer", "listen_addr", \
						   NULL, false, \
						   &(pgbouncerIni->listen_addr)), \
		make_int_option("pgbouncer", "listen_backlog", \
						NULL, false, \
						&(pgbouncerIni->listen_backlog)), \
		make_int_option("pgbouncer", "listen_port", \
						NULL, false, \
						&(pgbouncerIni->listen_port)), \
		make_int_option("pgbouncer", "log_connections", \
						NULL, false, \
						&(pgbouncerIni->log_connections)), \
		make_int_option("pgbouncer", "log_disconnections", \
						NULL, false, \
						&(pgbouncerIni->log_disconnections)), \
		make_int_option("pgbouncer", "log_pooler_errors", \
						NULL, false, \
						&(pgbouncerIni->log_pooler_errors)), \
		make_int_option("pgbouncer", "log_stats", \
						NULL, false, \
						&(pgbouncerIni->log_stats)), \
		make_int_option("pgbouncer", "max_client_conn", \
						NULL, false, \
						&(pgbouncerIni->max_client_conn)), \
		make_int_option("pgbouncer", "max_db_connections", \
						NULL, false, \
						&(pgbouncerIni->max_db_connections)), \
		make_int_option("pgbouncer", "max_packet_size", \
						NULL, false, \
						&(pgbouncerIni->max_packet_size)), \
		make_int_option("pgbouncer", "min_pool_size", \
						NULL, false, \
						&(pgbouncerIni->min_pool_size)), \
		make_int_option("pgbouncer", "pkt_buf", \
						NULL, false, \
						&(pgbouncerIni->pkt_buf)), \
		make_int_option("pgbouncer", "query_timeout", \
						NULL, false, \
						&(pgbouncerIni->query_timeout)), \
		make_int_option("pgbouncer", "query_wait_timeout", \
						NULL, false, \
						&(pgbouncerIni->query_wait_timeout)), \
		make_int_option("pgbouncer", "reserve_pool_size", \
						NULL, false, \
						&(pgbouncerIni->reserve_pool_size)), \
		make_int_option("pgbouncer", "reserve_pool_timeout", \
						NULL, false, \
						&(pgbouncerIni->reserve_pool_timeout)), \
		make_int_option("pgbouncer", "sbuf_loopcnt", \
						NULL, false, \
						&(pgbouncerIni->sbuf_loopcnt)), \
		make_int_option("pgbouncer", "server_check_delay", \
						NULL, false, \
						&(pgbouncerIni->server_check_delay)), \
		make_string_option("pgbouncer", "server_check_query", \
						   NULL, false, \
						   &(pgbouncerIni->server_check_query)), \
		make_int_option("pgbouncer", "server_connect_timeout", \
						NULL, false, \
						&(pgbouncerIni->server_connect_timeout)), \
		make_int_option("pgbouncer", "server_fast_close", \
						NULL, false, \
						&(pgbouncerIni->server_fast_close)), \
		make_int_option("pgbouncer", "server_idle_timeout", \
						NULL, false, \
						&(pgbouncerIni->server_idle_timeout)), \
		make_int_option("pgbouncer", "server_lifetime", \
						NULL, false, \
						&(pgbouncerIni->server_lifetime)), \
		make_int_option("pgbouncer", "server_login_retry", \
						NULL, false, \
						&(pgbouncerIni->server_login_retry)), \
		make_string_option("pgbouncer", "server_reset_query", \
						   NULL, false, \
						   &(pgbouncerIni->server_reset_query)), \
		make_int_option("pgbouncer", "server_reset_query_always", \
						NULL, false, \
						&(pgbouncerIni->server_reset_query_always)), \
		make_int_option("pgbouncer", "server_round_robin", \
						NULL, false, \
						&(pgbouncerIni->server_round_robin)), \
		make_string_option("pgbouncer", "server_tls_ciphers", \
						   NULL, false, \
						   &(pgbouncerIni->server_tls_ciphers)), \
		make_string_option("pgbouncer", "server_tls_protocols", \
						   NULL, false, \
						   &(pgbouncerIni->server_tls_protocols)), \
		make_string_option("pgbouncer", "server_tls_sslmode", \
						   NULL, false, \
						   &(pgbouncerIni->server_tls_sslmode)), \
		make_string_option("pgbouncer", "service_name", \
						   NULL, false, \
						   &(pgbouncerIni->service_name)), \
		make_int_option("pgbouncer", "so_reuseport", \
						NULL, false, \
						&(pgbouncerIni->so_reuseport)), \
		make_int_option("pgbouncer", "stats_period", \
						NULL, false, \
						&(pgbouncerIni->stats_period)), \
		make_string_option("pgbouncer", "stats_users", \
						   NULL, false, \
						   &(pgbouncerIni->stats_users)), \
		make_int_option("pgbouncer", "suspend_timeout", \
						NULL, false, \
						&(pgbouncerIni->suspend_timeout)), \
		make_string_option("pgbouncer", "syslog_facility", \
						   NULL, false, \
						   &(pgbouncerIni->syslog_facility)), \
		make_string_option("pgbouncer", "syslog_ident", \
						   NULL, false, \
						   &(pgbouncerIni->syslog_ident)), \
		make_string_option("pgbouncer", "tcp_defer_accept", \
						   NULL, false, \
						   &(pgbouncerIni->tcp_defer_accept)), \
		make_int_option("pgbouncer", "tcp_keepalive", \
						NULL, false, \
						&(pgbouncerIni->tcp_keepalive)), \
		make_int_option("pgbouncer", "tcp_keepcnt", \
						NULL, false, \
						&(pgbouncerIni->tcp_keepcnt)), \
		make_int_option("pgbouncer", "tcp_keepidle", \
						NULL, false, \
						&(pgbouncerIni->tcp_keepidle)), \
		make_int_option("pgbouncer", "tcp_keepintvl", \
						NULL, false, \
						&(pgbouncerIni->tcp_keepintvl)), \
		make_int_option("pgbouncer", "tcp_socket_buffer", \
						NULL, false, \
						&(pgbouncerIni->tcp_socket_buffer)), \
		make_int_option("pgbouncer", "tcp_user_timeout", \
						NULL, false, \
						&(pgbouncerIni->tcp_user_timeout)), \
		make_int_option("pgbouncer", "verbose", \
						NULL, false, \
						&(pgbouncerIni->verbose)), \
		INI_OPTION_LAST \
	}

/*
 * Order is important because pgbouncer does not fare well with multiple
 * definition of sections. Start with [pgbouncer] section first that was
 * mentioned above
 */
#define SET_PGBOUNCER_RUNTIME_INI_OPTIONS_ARRAY(pgbouncerIni) \
	{ \
		make_string_option(NULL, "logfile", \
						   NULL, false, \
						   &(pgbouncerIni->logfile)), \
		make_string_option(NULL, "pidfile", \
						   NULL, false, \
						   &(pgbouncerIni->pidfile)), \
		make_string_option("databases", pgbouncerIni->dbname, \
						   NULL, false, \
						   &(pgbouncerIni->connection)), \
		INI_OPTION_LAST \
	}

static bool pgbouncer_config_handle_auth_file(struct PgbouncerIni *privateIni,
											  const char *pgdata);
static bool pgbouncer_runtime_logfile(struct PgbouncerIni *privateIni,
									  const char *pgdata);
static bool pgbouncer_runtime_pidfile(struct PgbouncerIni *privateIni,
									  const char *pgdata);
static bool pgbouncer_runtime_database(struct PgbouncerIni *privateIni,
									   NodeAddress primary,
									   const char *dbname);

static bool pgbouncer_runtime_auth_file(struct PgbouncerIni *privateIni,
										const char *pgdata);

/*
 * pgbouncer_config_init initializes a PgbouncerConfig with the default
 * values.
 */
bool
pgbouncer_config_init(PgbouncerConfig *config, const char *pgdata)
{
	if (!pgdata)
	{
		log_error("Failed to initialize pgbouncer configuration, "
				  "pgdata not set");
		return false;
	}

	/* setup config->pathnames.pid */
	/* setup config->pathnames.pgbouncer */
	/* setup config->pathnames.pgbouncerRunTime */
	if (!SetPidFilePath(&(config->pathnames), pgdata) ||
		!SetPgbouncerFilePath(&(config->pathnames), pgdata) ||
		!SetPgbouncerRunTimeFilePath(&(config->pathnames), pgdata))
	{
		/* It has already logged why */
		return false;
	}

	/* Find the absolute path of pgbouncer */
	if (!search_path_first("pgbouncer", config->pgbouncerProg, LOG_ERROR))
	{
		log_fatal("Failed to find pgbouncer program in PATH");
		return false;
	}

	return true;
}


/*
 * pgbouncer_config_destroy frees any malloc'ed memory in PgbouncerConfig
 */
bool
pgbouncer_config_destroy(PgbouncerConfig *config)
{
	struct PgbouncerIni *pgbouncerIni;

	if (!config || !config->data)
	{
		return true;
	}

	pgbouncerIni = config->data;

	if (pgbouncerIni->admin_users)
	{
		free(pgbouncerIni->admin_users);
	}
	if (pgbouncerIni->auth_file)
	{
		free(pgbouncerIni->auth_file);
	}
	if (pgbouncerIni->auth_query)
	{
		free(pgbouncerIni->auth_query);
	}
	if (pgbouncerIni->auth_type)
	{
		free(pgbouncerIni->auth_type);
	}
	if (pgbouncerIni->auth_user)
	{
		free(pgbouncerIni->auth_user);
	}
	if (pgbouncerIni->client_tls_ciphers)
	{
		free(pgbouncerIni->client_tls_ciphers);
	}
	if (pgbouncerIni->client_tls_dheparams)
	{
		free(pgbouncerIni->client_tls_dheparams);
	}
	if (pgbouncerIni->client_tls_ecdhcurve)
	{
		free(pgbouncerIni->client_tls_ecdhcurve);
	}
	if (pgbouncerIni->client_tls_protocols)
	{
		free(pgbouncerIni->client_tls_protocols);
	}
	if (pgbouncerIni->client_tls_sslmode)
	{
		free(pgbouncerIni->client_tls_sslmode);
	}
	if (pgbouncerIni->ignore_startup_parameters)
	{
		free(pgbouncerIni->ignore_startup_parameters);
	}
	if (pgbouncerIni->job_name)
	{
		free(pgbouncerIni->job_name);
	}
	if (pgbouncerIni->listen_addr)
	{
		free(pgbouncerIni->listen_addr);
	}
	if (pgbouncerIni->pool_mode)
	{
		free(pgbouncerIni->pool_mode);
	}
	if (pgbouncerIni->server_check_query)
	{
		free(pgbouncerIni->server_check_query);
	}
	if (pgbouncerIni->server_reset_query)
	{
		free(pgbouncerIni->server_reset_query);
	}
	if (pgbouncerIni->server_tls_ciphers)
	{
		free(pgbouncerIni->server_tls_ciphers);
	}
	if (pgbouncerIni->server_tls_protocols)
	{
		free(pgbouncerIni->server_tls_protocols);
	}
	if (pgbouncerIni->server_tls_sslmode)
	{
		free(pgbouncerIni->server_tls_sslmode);
	}
	if (pgbouncerIni->service_name)
	{
		free(pgbouncerIni->service_name);
	}
	if (pgbouncerIni->stats_users)
	{
		free(pgbouncerIni->stats_users);
	}
	if (pgbouncerIni->syslog_facility)
	{
		free(pgbouncerIni->syslog_facility);
	}
	if (pgbouncerIni->syslog_ident)
	{
		free(pgbouncerIni->syslog_ident);
	}
	if (pgbouncerIni->tcp_defer_accept)
	{
		free(pgbouncerIni->tcp_defer_accept);
	}

	if (pgbouncerIni->logfile)
	{
		free(pgbouncerIni->logfile);
	}
	if (pgbouncerIni->pidfile)
	{
		free(pgbouncerIni->pidfile);
	}

	if (pgbouncerIni->connection)
	{
		free(pgbouncerIni->connection);
	}
	if (pgbouncerIni->dbname)
	{
		free(pgbouncerIni->dbname);
	}

	free(config->data);
	config->data = NULL;

	return true;
}


/*
 * pgbouncer_config_read_template reads the contents of the stored ini file. The
 * contents have to match our hardcoded config or it errors.
 *
 * The template is found in the Pathname section and typically has been written
 * by a call to pgbouncer_config_write_template()
 *
 * The contents of that configuration are held in a privately owned member in
 * PgbouncerConfig.
 */
bool
pgbouncer_config_read_template(PgbouncerConfig *config)
{
	struct PgbouncerIni pgbouncerIni = { 0 };
	IniOption pgbouncerConfigIni[] =
		SET_PGBOUNCER_INI_OPTIONS_ARRAY((&pgbouncerIni));

	if (config->data)
	{
		log_error("Bad internal config state, "
				  "failed to read template");
		return false;
	}

	if (!read_ini_file(config->pathnames.pgbouncer, pgbouncerConfigIni))
	{
		log_error("Bad pgbouncer ini config %s", config->pathnames.pgbouncer);
		return false;
	}

	config->data = malloc(sizeof(pgbouncerIni));
	if (!config->data)
	{
		log_fatal("Bad internal state, malloc failed");
		return false;
	}

	/*
	 * IGNORE-BANNED fits here because we want to take ownershipt of the
	 * stack allocated memory variable in the heap. The memory addresses are
	 * guarranteed to not overlap and to be of the same size.
	 */
	memcpy(config->data, &pgbouncerIni, sizeof(pgbouncerIni)); /* IGNORE-BANNED */

	return true;
}


/*
 * pgbouncer_config_read_user_ini_file reads the contents of the user supplied
 * ini file. The contents have to match our hardcoded config or it errors. Those
 * values are then held in a privately owned member in PgbouncerConfig.
 *
 * There should be no loaded configuration in the struct prior to calling this
 * function.
 *
 * Returns true when the configuration is successfully read.
 */
bool
pgbouncer_config_read_user_supplied_ini(PgbouncerConfig *config)
{
	struct PgbouncerIni pgbouncerIni = { 0 };
	IniOption pgbouncerConfigIni[] =
		SET_PGBOUNCER_INI_OPTIONS_ARRAY((&pgbouncerIni));

	if (config->data)
	{
		log_error("Bad internal config state, "
				  "failed to read user supplied ini");
		return false;
	}

	if (!read_ini_file(config->userSuppliedConfig, pgbouncerConfigIni))
	{
		log_error("Bad user supplied config %s", config->userSuppliedConfig);
		return false;
	}

	config->data = malloc(sizeof(pgbouncerIni));
	if (!config->data)
	{
		log_fatal("Bad internal state, malloc failed");
		return false;
	}

	/*
	 * IGNORE-BANNED fits here because we want to take ownershipt of the
	 * stack allocated memory variable in the heap. The memory addresses are
	 * guarranteed to not overlap and to be of the same size.
	 */
	memcpy(config->data, &pgbouncerIni, sizeof(pgbouncerIni)); /* IGNORE-BANNED */

	return true;
}


/*
 * pgbouncer_config_write_runtime writes an already loaded configuration to the
 * runtime file.
 *
 * The function comprises of three parts:
 *	Preparation of the runtime values of the config file
 *	File management of the runtime config file, already calculated in Pathnames
 *	Writing those values to the file
 *
 * It is also responsible for handling the runtime section of the PgbouncerIni
 * struct.
 *
 */
bool
pgbouncer_config_write_runtime(PgbouncerConfig *config)
{
	FILE *fileStream = NULL;
	struct PgbouncerIni privateIni = { 0 };
	const char *filePath = config->pathnames.pgbouncerRunTime;
	bool success;

	if (!config->data)
	{
		log_error("Attempt to write file without loaded configuration");
		return false;
	}

	/*
	 * IGNORE-BANNED fits here because we are going to overwrite some values,
	 * for example values pointing to files, so we operate on a stacked
	 * allocated copy. Any overwritten values in the copy do not affect the
	 * original values.
	 */
	memcpy(&privateIni, config->data, sizeof(privateIni)); /* IGNORE-BANNED */

	/*
	 * Handle the runtime values
	 */
	if (!pgbouncer_runtime_logfile(&privateIni, config->pgSetup.pgdata) ||
		!pgbouncer_runtime_pidfile(&privateIni, config->pgSetup.pgdata) ||
		!pgbouncer_runtime_auth_file(&privateIni, config->pgSetup.pgdata) ||
		!pgbouncer_runtime_database(&privateIni, config->primary,
									config->pgSetup.dbname))
	{
		/* It has already logged why */
		return false;
	}

	log_trace("pgbouncer_config_write_runtime \"%s\"", filePath);
	log_info("Will write to: %s", filePath);

	fileStream = fopen_with_umask(filePath, "w", FOPEN_FLAGS_W, 0644);
	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	success = write_ini_to_stream(fileStream,
								  (IniOption []) SET_PGBOUNCER_INI_OPTIONS_ARRAY(
									  (&privateIni)));

	success &= write_ini_to_stream(fileStream,
								   (IniOption []) SET_PGBOUNCER_RUNTIME_INI_OPTIONS_ARRAY(
									   (&privateIni)));

	free(privateIni.logfile);
	free(privateIni.pidfile);
	free(privateIni.auth_file);
	free(privateIni.dbname);
	free(privateIni.connection);

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return success;
}


/*
 * pgbouncer_config_write_template writes loaded configuration to the file
 * pointed by Pathnames
 *
 * This file will be used as our template. If there are any known values in the
 * configuration that point to files, it is our responsibility to manage. In
 * those cases the contents of those files are copied into files that are
 * managed by us.
 */
bool
pgbouncer_config_write_template(PgbouncerConfig *config)
{
	const char *filePath = config->pathnames.pgbouncer;
	struct PgbouncerIni *loadedIni = config->data;
	bool success = false;
	FILE *fileStream = NULL;

	if (!loadedIni)
	{
		log_error("Attempt to write file without loaded configuration");
		return false;
	}

	if (!pgbouncer_config_handle_auth_file(loadedIni, config->pgSetup.pgdata))
	{
		/* It has already logged why */
		return false;
	}

	log_trace("pgbouncer_config_write_template \"%s\"", filePath);

	log_info("Will write to: %s", filePath);
	fileStream = fopen_with_umask(filePath, "w", FOPEN_FLAGS_W, 0644);
	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	success = write_ini_to_stream(fileStream,
								  (IniOption []) SET_PGBOUNCER_INI_OPTIONS_ARRAY(
									  loadedIni));

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return success;
}


/*
 * pgbouncer_config_handle_auth_file overwrites the user provided value for
 * auth_file with a new destination file path. It copies the contents of the
 * user provided file to the new destination, overwritting any existing file.
 *
 * Returns true if all the above succeeded.
 */
static bool
pgbouncer_config_handle_auth_file(struct PgbouncerIni *loadedIni,
								  const char *pgdata)
{
	char authFilePath[MAXPGPATH];

	if (!loadedIni->auth_file)
	{
		return true;
	}

	if (!build_xdg_path(authFilePath,
						XDG_CONFIG,
						pgdata, "pgbouncer_auth_file.txt"))
	{
		log_error("Failed to write auth_file");
		return false;
	}

	if (file_exists(authFilePath) && !unlink_file(authFilePath))
	{
		log_error("Failed to remove previous auth_file");
		return false;
	}

	if (!duplicate_file(loadedIni->auth_file, authFilePath))
	{
		log_error("Failed to write auth_file");
		return false;
	}

	free(loadedIni->auth_file);
	loadedIni->auth_file = strdup(authFilePath);

	return true;
}


/* XXX: remember to unlink it on exit */
static bool
pgbouncer_runtime_auth_file(struct PgbouncerIni *privateIni,
							const char *pgdata)
{
	char authFilePath[MAXPGPATH];

	if (!privateIni->auth_file)
	{
		return true;
	}

	if (!build_xdg_path(authFilePath,
						XDG_RUNTIME,
						pgdata,
						"pgbouncer_auth_file.txt"))
	{
		log_error("Failed to write auth_file");
		return false;
	}

	if (!unlink_file(authFilePath))
	{
		log_error("Failed to remove previous auth_file");
		return false;
	}

	if (!duplicate_file(privateIni->auth_file, authFilePath))
	{
		log_error("Failed to write auth_file");
		return false;
	}

	/* Do not free the initial value */
	privateIni->auth_file = strdup(authFilePath);

	return true;
}


static bool
pgbouncer_runtime_logfile(struct PgbouncerIni *privateIni,
						  const char *pgdata)
{
	char logFilePath[MAXPGPATH];

	if (!build_xdg_path(logFilePath,
						XDG_RUNTIME,
						pgdata,
						"pgbouncer.log"))
	{
		log_error("Failed to build pgbouncer runtime logfile");
		return false;
	}

	privateIni->logfile = strdup(logFilePath);
	return true;
}


static bool
pgbouncer_runtime_pidfile(struct PgbouncerIni *privateIni,
						  const char *pgdata)
{
	char pidFilePath[MAXPGPATH];

	if (!build_xdg_path(pidFilePath,
						XDG_RUNTIME,
						pgdata,
						"pgbouncer.pid"))
	{
		log_error("Failed to build pgbouncer runtime pid");
		return false;
	}

	privateIni->pidfile = strdup(pidFilePath);
	return true;
}


static bool
pgbouncer_runtime_database(struct PgbouncerIni *privateIni,
						   NodeAddress primary,
						   const char *dbname)
{
	char connection[MAXCONNINFO];

	/* mydb = port=5002 host=there.com dbname=mydb */
	pg_snprintf(connection, MAXCONNINFO, "port=%d host=%s dbname=%s",
				primary.port,
				primary.host,
				dbname);

	if (privateIni->dbname)
	{
		free(privateIni->dbname);
	}

	if (privateIni->connection)
	{
		free(privateIni->connection);
	}

	privateIni->dbname = strdup(dbname);
	privateIni->connection = strdup(connection);

	return true;
}
