#!/bin/sh
# Install the pre-staged v2.2 pgautofailover extension files into the system
# extension and library paths reported by pg_config.  Run as root inside the
# pgaf:current container.
set -e

STAGING=/usr/local/bin/pgaf/2.2
EXTDIR=$(pg_config --sharedir)/extension
LIBDIR=$(pg_config --pkglibdir)

install -m 644 "${STAGING}/pgautofailover.control"         "${EXTDIR}/"
install -m 644 "${STAGING}/pgautofailover--2.1--2.2.sql"   "${EXTDIR}/"
install -m 644 "${STAGING}/pgautofailover--2.2.sql"        "${EXTDIR}/"
install -m 755 "${STAGING}/pgautofailover.so"              "${LIBDIR}/"
