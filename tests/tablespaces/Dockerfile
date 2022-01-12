ARG PGVERSION=10

FROM pg_auto_failover_test:pg${PGVERSION} as test

USER root
RUN mkdir -p /extra_volumes/extended_a && chown -R docker /extra_volumes/extended_a
RUN mkdir -p /extra_volumes/extended_b && chown -R docker /extra_volumes/extended_b
RUN mkdir -p /extra_volumes/extended_c && chown -R docker /extra_volumes/extended_c
RUN mkdir -p /var/lib/postgres && chown -R docker /var/lib/postgres

USER docker
