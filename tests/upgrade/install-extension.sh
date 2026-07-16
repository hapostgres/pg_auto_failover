#!/bin/sh
# Install the pre-staged pgautofailover extension files into the system
# extension and library paths reported by pg_config.  Run as root inside the
# pgaf:current container.
set -e

STAGING=/usr/local/bin/pgaf/next
EXTDIR=$(pg_config --sharedir)/extension
LIBDIR=$(pg_config --pkglibdir)

install -m 644 "${STAGING}/"pgautofailover*.control "${EXTDIR}/"
install -m 644 "${STAGING}/"pgautofailover*.sql     "${EXTDIR}/"
install -m 755 "${STAGING}/pgautofailover.so"       "${LIBDIR}/"
