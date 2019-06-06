-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.

select version
  from pg_available_extension_versions
 where name = 'pgautofailover' and version = 'dummy';

alter extension pgautofailover update to dummy;

select installed_version
  from pg_available_extensions where name = 'pgautofailover';

-- should error because installed extension isn't compatible with .so
select * from pgautofailover.get_primary('unknown formation');

