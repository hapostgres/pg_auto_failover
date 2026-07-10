.. _pg_autoctl_manual_service_restart:

pg_autoctl manual service restart
==================================

pg_autoctl manual service restart - Restart pg_autoctl sub-processes

Synopsis
--------

::

   pg_autoctl manual service restart
    postgres     Restart the pg_autoctl postgres controller service
    listener     Restart the pg_autoctl monitor listener service
    node-active  Restart the pg_autoctl keeper node-active service


Description
-----------

It is possible to restart the ``pg_autoctl`` keeper or listener service
without affecting the other running services, and without stopping Postgres.
Typically, to restart the ``pg_autoctl`` keeper without impacting Postgres::

  $ pg_autoctl manual service restart node-active --pgdata node1
  14:52:06 31223 INFO  Sending the TERM signal to service "node-active" with pid 26626
  14:52:06 31223 INFO  Service "node-active" has been restarted with pid 31230
  31230

The Postgres service is not impacted by the restart of the ``pg_autoctl``
keeper process.
