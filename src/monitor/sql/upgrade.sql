-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

CREATE EXTENSION pgautofailover VERSION '1.0';
\dx pgautofailover

ALTER EXTENSION pgautofailover UPDATE TO '1.1';
\dx pgautofailover

ALTER EXTENSION pgautofailover UPDATE TO '1.2';
\dx pgautofailover

ALTER EXTENSION pgautofailover UPDATE TO '1.3';
\dx pgautofailover

DROP EXTENSION pgautofailover;
