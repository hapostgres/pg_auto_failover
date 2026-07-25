.. pg_auto_failover documentation master file, created by
   sphinx-quickstart on Sat May  5 14:33:23 2018.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Welcome to pg_auto_failover's documentation!
============================================

The pg_auto_failover project is an Open Source Software project. The
development happens at `https://github.com/hapostgres/pg_auto_failover`__ and
is public: everyone is welcome to participate by opening issues or pull
requests, giving feedback, etc.

Remember that the first steps are to actually play with the pg_autoctl
command, then read the entire available documentation (after all, I took the
time to write it), and then to address the community in a kind and polite
way — the same way you would expect people to use when addressing you.

__ https://github.com/hapostgres/pg_auto_failover

.. note::

   The development of pg_auto_failover was originally driven by Citus Data,
   and then a team at Microsoft. The project is now orphaned in terms of
   financial support: maintenance and development happen on volunteers'
   free time.

   For enhancements, improvements, and new features, consider contributing
   to the project. Pull Requests are reviewed as time allows.

   For professional support or to sponsor ongoing maintenance, see
   `oss.theartofpostgresql.com <https://oss.theartofpostgresql.com>`_.

.. note::

   Assistance is provided as usual with Open Source projects, on a voluntary
   basis. If you need help to cook a patch, enhance the documentation, or
   even to use the software, you're welcome to ask questions and expect some
   level of free guidance.

.. toctree::
   :hidden:
   :caption: Getting Started

   intro
   how-to
   tutorial
   install

.. toctree::
   :hidden:
   :caption: Architecture

   architecture
   architecture-multi-standby
   failover-state-machine
   fault-tolerance
   security

.. toctree::
   :hidden:
   :caption: Citus

   citus
   citus-quickstart

.. toctree::
   :hidden:
   :caption: Manual Pages

   ref/manual
   ref/configuration

.. toctree::
   :hidden:
   :caption: Operations

   operations
   testing
   reporting-bugs
   faq

..
   Indices and tables
   ==================

   * :ref:`genindex`
   * :ref:`modindex`
   * :ref:`search`
