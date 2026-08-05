.. _pg_autoctl_set_basebackup_policy:

pg_autoctl set basebackup-policy
==================================

pg_autoctl set basebackup-policy - Update a named base-backup
production/retention policy

Synopsis
--------

This command updates a base-backup production/retention policy that
already exists on the monitor::

  usage: pg_autoctl set basebackup-policy  --monitor --name --config

  --monitor   pg_auto_failover Monitor Postgres URL
  --name      policy name
  --config    path to a JSON document with the fields to change

Description
-----------

Only the fields present in the ``--config`` document change; any field
left out keeps its current value. See :ref:`pg_autoctl_create_basebackup_policy`
for the full set of fields and what each one controls -- the document
shape is identical, just with only the fields you want to change.

Every archiver whose (formation, group) resolves to this policy (directly,
or through its formation's own default) picks up the change on its next
tick -- there is no need to restart anything.

Options
-------

The following options are available to ``pg_autoctl set basebackup-policy``:

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--name

  Name of the policy to update.

--config

  Path to a JSON document with the fields to change.
