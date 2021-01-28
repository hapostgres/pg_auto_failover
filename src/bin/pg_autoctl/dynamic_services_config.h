/*
 * src/bin/pg_autoctl/dynamic_services_config.h
 *     Dynamic services configuration data structure and function definitions
 *
 * XXX: Add copywrite as required
 */

#ifndef DYNAMIC_SERVICES_CONFIG_H
#define DYNAMIC_SERVICES_CONFIG_H

#include <stdbool.h>

/* No need to include supervisor.h here */
typedef struct ServiceArray ServiceArray;

/*
 * returns true when everything was successfull, false otherwise, the
 * dynamicServicesConfig might be bogus then, it is the caller's responsibility
 * to ignore it
 */
bool dynamic_services_read_config(ServiceArray *enabledServices);
bool dynamic_services_write_config(const ServiceArray *const enabledServices,
								   const ServiceArray *const disabledServices);

#endif
