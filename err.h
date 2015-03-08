#ifndef _ERR_H
#define _ERR_H

#include <cstdio>
#include <cstdlib>

void errexit(const char *message) {
	fprintf(stderr, "%s\n", message);
	exit(1);
}

#endif
