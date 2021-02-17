 #!/bin/bash

citus_indent --check
black --check .
ci/banned.h.sh
make -j5 CFLAGS=-Werror
PATH=`pg_config --bindir`:$PATH make test
