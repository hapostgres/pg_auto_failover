.. _pg_autoctl_do_service_restart:

pg_autoctl do service restart
=============================

pg_autoctl do service restart - Run pg_autoctl sub-processes (services)

Synopsis
--------

pg_autoctl do service restart provides the following commands::

   pg_autoctl do service restart
    postgres     Restart the pg_autoctl postgres controller service
    listener     Restart the pg_autoctl monitor listener service
    node-active  Restart the pg_autoctl keeper node-active service


Description
-----------

It is possible to restart the ``pg_autoctl`` or the Postgres service without
affecting the other running service. Typically, to restart the
``pg_autoctl`` parts without impacting Postgres::

  $ pg_autoctl do service restart node-active --pgdata node1
  14:52:06 31223 INFO  Sending the TERM signal to service "node-active" with pid 26626
  14:52:06 31223 INFO  Service "node-active" has been restarted with pid 31230
  31230

The Postgres service has not been impacted by the restart of the
``pg_autoctl`` process.
