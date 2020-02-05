/*
 * src/bin/pg_autoctl/ini_file.c
 *     Functions to parse a configuration file using the .INI syntax.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <errno.h>

#include "ini_file.h"
#include "log.h"
#include "pgctl.h"
#include "parson.h"
#include "pgsetup.h"

#define INI_IMPLEMENTATION
#include "ini.h"

/*
 * Load a configuration file in the INI format.
 */
bool
read_ini_file(const char *filename, IniOption *optionList)
{
	ini_t *ini = NULL;
	char *fileContents = NULL;
	long fileSize = 0L;
	IniOption *option;

	/* read the current postgresql.conf contents */
	if (!read_file(filename, &fileContents, &fileSize))
	{
		return false;
	}

	/* parse the content of the file as per INI syntax rules */
	ini = ini_load(fileContents, NULL);
	free(fileContents);

	/*
	 * Now that the INI file is loaded into a generic structure, run through it
	 * to find given opts and set.
	 */
	for (option = optionList; option->type != INI_END_T; option++)
	{
		int sectionIndex;
		int optionIndex;
		char *val;

		sectionIndex = ini_find_section(ini, option->section, 0);

		if (sectionIndex == INI_NOT_FOUND)
		{
			if (option->required)
			{
				log_error("Failed to find section %s in \"%s\"",
						  option->section, filename);
				ini_destroy(ini);
				return false;
			}
			optionIndex = INI_NOT_FOUND;
		}
		else
		{
			optionIndex = ini_find_property(ini, sectionIndex, option->name, 0);
		}

		if (optionIndex == INI_NOT_FOUND)
		{
			if (option->required)
			{
				log_error("Failed to find option %s.%s in \"%s\"",
						  option->section, option->name, filename);
				ini_destroy(ini);
				return false;
			}
			else
			{
				switch (option->type)
				{
					case INI_INT_T:
					{
						*(option->intValue) = option->intDefault;
						break;
					}

					case INI_STRING_T:
					case INI_STRBUF_T:
					{
						ini_set_option_value(option, option->strDefault);
						break;
					}

					default:

						/* should never happen, or it's a development bug */
						log_fatal("Unknown option type %d", option->type);
						ini_destroy(ini);
						return false;
				}
			}
		}
		else
		{
			val = (char *) ini_property_value(ini, sectionIndex, optionIndex);

			log_trace("%s.%s = %s", option->section, option->name, val);

			if (val != NULL)
			{
				if (!ini_set_option_value(option, val))
				{
					/* we logged about it already */
					ini_destroy(ini);
					return false;
				}
			}
		}
	}
	ini_destroy(ini);
	return true;
}


/*
 * ini_validate_options walks through an optionList and installs default values
 * when necessary, and returns false if any required option is missing and
 * doesn't have a default provided.
 */
bool
ini_validate_options(IniOption *optionList)
{
	IniOption *option;

	for (option = optionList; option->type != INI_END_T; option++)
	{
		int n;
		char optionName[BUFSIZE];

		n = sprintf(optionName, "%s.%s", option->section, option->name);

		if (option->optName)
		{
			sprintf(optionName + n, " (--%s)", option->optName);
		}

		switch (option->type)
		{
			case INI_INT_T:
			{
				if (*(option->intValue) == -1 && option->intDefault != -1)
				{
					*(option->intValue) = option->intDefault;
				}

				if (option->required && *(option->intValue) == -1)
				{
					log_error("Option %s is required and has not been set",
							  optionName);
					return false;
				}
				break;
			}

			case INI_STRING_T:
			{
				if (*(option->strValue) == NULL && option->strDefault != NULL)
				{
					ini_set_option_value(option, option->strDefault);
				}

				if (option->required && *(option->strValue) == NULL)
				{
					log_error("Option %ss is required and has not been set",
							  optionName);
					return false;
				}
				break;
			}

			case INI_STRBUF_T:
			{
				if (IS_EMPTY_STRING_BUFFER(option->strBufValue) &&
					option->strDefault != NULL)
				{
					ini_set_option_value(option, option->strDefault);
				}

				if (option->required && IS_EMPTY_STRING_BUFFER(option->strBufValue))
				{
					log_error("Option %s is required and has not been set",
							  optionName);
					return false;
				}
				break;
			}

			default:

				/* should never happen, or it's a development bug */
				log_fatal("Unknown option type %d", option->type);
				return false;
		}
	}
	return true;
}


/*
 * ini_set_option_value saves given value to option, parsing the value string
 * as its type require.
 */
bool
ini_set_option_value(IniOption *option, const char *value)
{
	if (option == NULL)
	{
		return false;
	}

	switch (option->type)
	{
		case INI_STRING_T:
		{
			if (value == NULL)
			{
				*(option->strValue) = NULL;
			}
			else
			{
				*(option->strValue) = strdup(value);
			}
			break;
		}

		case INI_STRBUF_T:
		{
			/*
			 * When given a String Buffer str[SIZE], then we are given strbuf
			 * as the address where to host the data directly.
			 */
			if (value == NULL)
			{
				/* null are handled as bytes of '\0' in string buffers */
				bzero((void *) option->strBufValue, option->strBufferSize);
			}
			else
			{
				strlcpy((char *) option->strBufValue, value, option->strBufferSize);
			}
			break;
		}

		case INI_INT_T:
		{
			if (value)
			{
				int nb = strtol(value, NULL, 10);

				if (nb == 0 && errno == EINVAL)
				{
					log_error("Failed to parse %s.%s's value \"%s\" as a number",
							  option->section, option->name, value);
					return false;
				}
				*(option->intValue) = nb;
			}
			break;
		}

		default:
		{
			/* developer error, should never happen */
			log_fatal("Unknown option type %d", option->type);
			return false;
		}
	}
	return true;
}


/*
 * Format a single option as a string value.
 */
bool
ini_option_to_string(IniOption *option, char *dest, size_t size)
{
	switch (option->type)
	{
		case INI_STRING_T:
		{
			strlcpy(dest, *(option->strValue), size);
			return true;
		}

		case INI_STRBUF_T:
		{
			strlcpy(dest, (char *) option->strBufValue, size);
			return true;
		}

		case INI_INT_T:
		{
			snprintf(dest, size, "%d", *(option->intValue));
			return true;
		}

		default:
		{
			log_fatal("Unknown option type %d", option->type);
			return false;
		}
	}
	return false;
}


#define streq(x, y) ((x != NULL) && (y != NULL) && ( \
						 strcmp(x, y) == 0))

/*
 * write_ini_to_stream writes in-memory INI structure to given STREAM in the
 * INI format specifications.
 */
bool
write_ini_to_stream(FILE *stream, IniOption *optionList)
{
	char *currentSection = NULL;
	IniOption *option;

	for (option = optionList; option->type != INI_END_T; option++)
	{
		/* we might need to open a new section */
		if (!streq(currentSection, option->section))
		{
			if (currentSection != NULL)
			{
				fprintf(stream, "\n");
			}
			currentSection = (char *) option->section;
			fprintf(stream, "[%s]\n", currentSection);
		}

		switch (option->type)
		{
			case INI_INT_T:
			{
				fprintf(stream, "%s = %d\n", option->name, *(option->intValue));
				break;
			}

			case INI_STRING_T:
			{
				char *value = *(option->strValue);

				if (value)
				{
					fprintf(stream, "%s = %s\n", option->name, value);
				}
				else if (option->required)
				{
					log_error("Option %s.%s is required but is not set",
							  option->section, option->name);
					return false;
				}
				break;
			}

			case INI_STRBUF_T:
			{
				/* here we have a string buffer, which is its own address */
				char *value = (char *) option->strBufValue;

				if (value[0] != '\0')
				{
					fprintf(stream, "%s = %s\n", option->name, value);
				}
				else if (option->required)
				{
					log_error("Option %s.%s is required but is not set",
							  option->section, option->name);
					return false;
				}
				break;
			}

			default:
			{
				/* developper error, should never happen */
				log_fatal("Unknown option type %d", option->type);
				break;
			}
		}
	}
	fflush(stream);
	return true;
}


/*
 * ini_to_json populates the given JSON value with the contents of the INI
 * file. Sections become JSON objects, options the keys to the section objects.
 */
bool
ini_to_json(JSON_Object *jsRoot, IniOption *optionList)
{
	char *currentSection = NULL;
	JSON_Value *currentSectionJs = NULL;
	JSON_Object *currentSectionJsObj = NULL;
	IniOption *option = NULL;

	for (option = optionList; option->type != INI_END_T; option++)
	{
		/* we might need to open a new section */
		if (!streq(currentSection, option->section))
		{
			if (currentSection != NULL)
			{
				json_object_set_value(jsRoot, currentSection, currentSectionJs);
			}

			currentSectionJs = json_value_init_object();
			currentSectionJsObj = json_value_get_object(currentSectionJs);

			currentSection = (char *) option->section;
		}

		switch (option->type)
		{
			case INI_INT_T:
			{
				json_object_set_number(currentSectionJsObj,
									   option->name,
									   (double) *(option->intValue));
				break;
			}

			case INI_STRING_T:
			{
				char *value = *(option->strValue);

				if (value)
				{
					json_object_set_string(currentSectionJsObj,
										   option->name,
										   value);
				}
				else if (option->required)
				{
					log_error("Option %s.%s is required but is not set",
							  option->section, option->name);
					return false;
				}
				break;
			}

			case INI_STRBUF_T:
			{
				/* here we have a string buffer, which is its own address */
				char *value = (char *) option->strBufValue;

				if (value[0] != '\0')
				{
					json_object_set_string(currentSectionJsObj,
										   option->name,
										   value);
				}
				else if (option->required)
				{
					log_error("Option %s.%s is required but is not set",
							  option->section, option->name);
					return false;
				}
				break;
			}

			default:
			{
				/* developper error, should never happen */
				log_fatal("Unknown option type %d", option->type);
				break;
			}
		}
	}

	if (currentSection != NULL)
	{
		json_object_set_value(jsRoot, currentSection, currentSectionJs);
	}

	return true;
}


/*
 * lookup_ini_option implements an option lookup given a section name and an
 * option name.
 */
IniOption *
lookup_ini_option(IniOption *optionList, const char *section, const char *name)
{
	IniOption *option;

	/* now lookup section/option names in opts */
	for (option = optionList; option->type != INI_END_T; option++)
	{
		if (streq(option->section, section) && streq(option->name, name))
		{
			return option;
		}
	}
	return NULL;
}


/*
 * Lookup an option value given a "path" of section.option.
 */
IniOption *
lookup_ini_path_value(IniOption *optionList, const char *path)
{
	char *section_name, *option_name, *ptr;
	IniOption *option;

	/*
	 * Split path into section/option.
	 */
	ptr = strchr(path, '.');

	if (ptr == NULL)
	{
		log_error("Failed to find a dot separator in option path \"%s\"", path);
		return NULL;
	}

	section_name = strdup(path);                   /* don't scribble on path */
	option_name = section_name + (ptr - path) + 1; /* apply same offset */
	*(option_name - 1) = '\0';                     /* split string at the dot */

	option = lookup_ini_option(optionList, section_name, option_name);

	if (option == NULL)
	{
		log_error("Failed to find configuration option for path \"%s\"", path);
	}

	free(section_name);

	return option;
}


/*
 * ini_merge merges the options that have been set in overrideOptionList into
 * the options in dstOptionList, ignoring default values.
 */
bool
ini_merge(IniOption *dstOptionList, IniOption *overrideOptionList)
{
	IniOption *option;

	for (option = overrideOptionList; option->type != INI_END_T; option++)
	{
		IniOption *dstOption =
			lookup_ini_option(dstOptionList, option->section, option->name);

		if (dstOption == NULL)
		{
			/* developper error, why do we have incompatible INI options? */
			log_error("BUG: ini_merge: lookup failed in dstOptionList(%s, %s)",
					  option->section, option->name);
			return false;
		}

		switch (option->type)
		{
			case INI_INT_T:
			{
				if (*(option->intValue) != -1 && *(option->intValue) != 0)
				{
					*(dstOption->intValue) = *(option->intValue);
				}
				break;
			}

			case INI_STRING_T:
			{
				if (*(option->strValue) != NULL)
				{
					*(dstOption->strValue) = strdup(*(option->strValue));
				}
				break;
			}

			case INI_STRBUF_T:
			{
				if (!IS_EMPTY_STRING_BUFFER(option->strBufValue))
				{
					strlcpy((char *) dstOption->strBufValue,
							(char *) option->strBufValue,
							dstOption->strBufferSize);
				}
				break;
			}

			default:

				/* should never happen, or it's a development bug */
				log_fatal("Unknown option type %d", option->type);
				return false;
		}
	}
	return true;
}


/*
 * ini_get_setting reads given INI filename and maps its content using an
 * optionList that instructs which options to read and what default values to
 * use. Then ini_get_setting looks up the given path (section.option) and sets
 * the given value string.
 */
bool
ini_get_setting(const char *filename, IniOption *optionList,
				const char *path, char *value, size_t size)
{
	IniOption *option = NULL;

	log_debug("Reading configuration from %s", filename);

	if (!read_ini_file(filename, optionList))
	{
		log_error("Failed to parse configuration file \"%s\"", filename);
		return false;
	}

	option = lookup_ini_path_value(optionList, path);

	if (option)
	{
		return ini_option_to_string(option, value, size);
	}

	return false;
}


/*
 * ini_set_option sets the INI value to the given value.
 */
bool
ini_set_option(IniOption *optionList, const char *path, char *value)
{
	IniOption *option = lookup_ini_path_value(optionList, path);

	if (option && ini_set_option_value(option, value))
	{
		return true;
	}

	return false;
}


/*
 * ini_set_setting sets the INI filename option identified by path to the given
 * value. optionList is used to know how to read the values in the file and
 * also contains the default values.
 */
bool
ini_set_setting(const char *filename, IniOption *optionList,
				const char *path, char *value)
{
	log_debug("Reading configuration from %s", filename);

	if (!read_ini_file(filename, optionList))
	{
		log_error("Failed to parse configuration file \"%s\"", filename);
		return false;
	}

	return ini_set_option(optionList, path, value);
}
