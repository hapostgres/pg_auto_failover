.. _install:

Installing pg_auto_failover
===========================

We provide native system packages for pg_auto_failover on most popular Linux distributions. To download the packages you will need to obtain a Citus Enterprise Edition key -- `contact us <https://www.citusdata.com/about/contact_us>`_ to get one.

Use the steps below to install pg_auto_failover on PostgreSQL 11.

Ubuntu or Debian
----------------

.. code-block:: bash

  # add the required packages to your system
  curl https://install.citusdata.com/community/deb.sh | sudo bash

  # install pg_auto_failover
  sudo apt-get install postgresql-11-auto-failover10

  # confirm installation
  /usr/bin/pg_autoctl --version

Fedora, CentOS, or Red Hat
--------------------------

.. code-block:: bash

  # add the required packages to your system
  curl https://install.citusdata.com/community/rpm.sh | sudo bash

  # install pg_auto_failover
  sudo yum install -y pg-auto-failover10_11

  # confirm installation
  /usr/pgsql-11/bin/pg_autoctl --version
