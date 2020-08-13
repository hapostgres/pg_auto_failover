/*-------------------------------------------------------------------------
 *
 * src/monitor/version_compat.h
 *	  Compatibility macros for writing code agnostic to PostgreSQL versions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#if (PG_VERSION_NUM < 110000)

/*
 * The list_qsort API was introduced in Postgres 11:
 *
 * https://git.postgresql.org/gitweb/?p=postgresql.git;a=commit;h=ab72716778128fb63d54ac256adf7fe6820a1185
 */

#include "nodes/pg_list.h"
#include "version_compat.h"

/*
 * Sort a list using qsort. A sorted list is built but the cells of the
 * original list are re-used.  The comparator function receives arguments of
 * type ListCell **
 */
List *
list_qsort(const List *list, list_qsort_comparator cmp)
{
	ListCell *cell;
	int i;
	int len = list_length(list);
	ListCell **list_arr;
	List *new_list;

	if (len == 0)
	{
		return NIL;
	}

	i = 0;
	list_arr = palloc(sizeof(ListCell *) * len);
	foreach(cell, list)
	list_arr[i++] = cell;

	qsort(list_arr, len, sizeof(ListCell *), cmp);

	new_list = (List *) palloc(sizeof(List));
	new_list->type = list->type;
	new_list->length = len;
	new_list->head = list_arr[0];
	new_list->tail = list_arr[len - 1];

	for (i = 0; i < len - 1; i++)
	{
		list_arr[i]->next = list_arr[i + 1];
	}

	list_arr[len - 1]->next = NULL;
	pfree(list_arr);
	return new_list;
}


#endif
