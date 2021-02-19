#ifndef PG_RMTREE_H
#define PG_RMTREE_H

#include "postgres_fe.h"

bool rmtree(const char *path, bool rmtopdir);

#endif	/* PG_RMTREE_H */