# Testing monitor upgrades

This directory contains some docker-compose based tooling to manually test
monitor upgrades. The tooling is built around the idea that we want to test
what happens at upgrade from the code in the local branch.

It might be possible to also test what happens when we upgrade from a
previously released version with some edits in this tooling, though that's
not the main use-case here.

A typical manual session looks like the following. First, let us prepare a
tmux environment with two panes. The top pane will show the logs from all
the nodes running within the docker-compose orchestration:

```bash
$ tmux
$ tmux split-window -v
```

Now, in the first pane, build our images and start all our services:

```bash
# in the first pane
$ make build
$ make up
```

Now, in the second pane, watch until the cluster has reached a stable state
(primary/secondary/secondary) before we go on to upgrade the monitor.

```bash
# in the second pane
$ make watch
```

To upgrade the monitor we apply a local patch that provides version 1.7
(with no schema changes, just version number hacking), build an updated
docker image using the patch, and restart the monitor with this new version:

```bash
# in the second pane
$ make version
$ make upgrade-monitor
```

To check that the upgrade went well, we can do:

```bash
# in the second pane
$ make version
$ make state
```

We should see the Postgres nodes being verbose about the monitor having been
upgraded, but not the nodes. It is possible to now upgrade the nodes to the
current version too, though that's not the goal of this work at the moment.

```bash
# in the second pane
$ make upgrade-nodes

# in the first pane, C-c the current logs session, and re-attach
$ make tail

# in the second page
$ make version
$ make state
```

To test a failover:

```bash
$ make failover
$ make state
```

Time to clean-up our local repository:

```bash
# in the second pane
$ make down
$ make clean
```
