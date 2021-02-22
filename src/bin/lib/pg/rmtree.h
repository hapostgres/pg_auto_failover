#ifndef RMTREE_H
#define RMTREE_H

#include "postgres_fe.h"

bool rm_tree(const char *path, bool rmtopdir);

#endif	/* RMTREE_H */