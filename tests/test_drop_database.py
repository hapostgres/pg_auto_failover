
"""
Test DROP DATABASE operations with pg_auto_failover health check workers.

This test verifies that databases can be dropped successfully even when
health check workers are running, addressing issue #1063.

This test was originally created to reproduce a hang during DROP DATABASE
caused by health check workers not processing interrupts. The fix involves
adding CHECK_FOR_INTERRUPTS() calls in the worker loops.
"""

import tests.pgautofailover_utils as pgautofailover
from nose.tools import eq_
import time

cluster = None
monitor = None
node1 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    """Create and start the monitor."""
    global monitor
    monitor = cluster.create_monitor("/tmp/drop_db/monitor")
    monitor.run()


def test_001_init_primary():
    """Initialize a primary Postgres node."""
    global node1
    node1 = cluster.create_datanode("/tmp/drop_db/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")


def test_002_drop_database_basic():
    """
    Test basic DROP DATABASE operation.
    
    This tests that a database can be dropped successfully while
    pg_auto_failover health check workers are running.
    """
    print()
    print("Creating test database 'testdb1'...")
    node1.run_sql_query("CREATE DATABASE testdb1")
    
    # Give the health check worker time to start for the new database
    time.sleep(2)
    
    # Verify the database exists
    result = node1.run_sql_query(
        "SELECT datname FROM pg_database WHERE datname = 'testdb1'"
    )
    assert result[0][0] == "testdb1", "Database testdb1 should exist"
    
    print("Dropping database 'testdb1'...")
    start_time = time.time()
    
    # This should complete quickly without hanging
    node1.run_sql_query("DROP DATABASE testdb1")
    
    elapsed_time = time.time() - start_time
    print(f"DROP DATABASE completed in {elapsed_time:.2f} seconds")
    
    # Verify it completed in a reasonable time (should be < 10 seconds)
    assert elapsed_time < 10, \
        f"DROP DATABASE took {elapsed_time:.2f}s, expected < 10s (possible hang)"
    
    # Verify the database is gone
    result = node1.run_sql_query(
        "SELECT count(*) FROM pg_database WHERE datname = 'testdb1'"
    )
    eq_(result[0][0], 0, "Database testdb1 should be dropped")
    
    print(f"✓ DROP DATABASE completed successfully in {elapsed_time:.2f}s")


def test_003_drop_database_with_force():
    """
    Test DROP DATABASE with FORCE option.
    
    The FORCE option was added in PostgreSQL 13 to forcibly disconnect
    all sessions before dropping the database.
    """
    print()
    print("Creating test database 'testdb2'...")
    node1.run_sql_query("CREATE DATABASE testdb2")
    
    # Give the health check worker time to start
    time.sleep(2)
    
    print("Dropping database 'testdb2' with FORCE...")
    start_time = time.time()
    
    # PostgreSQL 13+ supports DROP DATABASE ... FORCE
    try:
        node1.run_sql_query("DROP DATABASE testdb2 WITH (FORCE)")
    except Exception as e:
        # If FORCE is not supported (PG < 13), use regular DROP
        if "syntax error" in str(e).lower() or "force" in str(e).lower():
            print("FORCE option not supported, using regular DROP DATABASE")
            node1.run_sql_query("DROP DATABASE testdb2")
        else:
            raise
    
    elapsed_time = time.time() - start_time
    print(f"DROP DATABASE WITH FORCE completed in {elapsed_time:.2f} seconds")
    
    # Verify it completed quickly
    assert elapsed_time < 10, \
        f"DROP DATABASE WITH FORCE took {elapsed_time:.2f}s, expected < 10s"
    
    # Verify the database is gone
    result = node1.run_sql_query(
        "SELECT count(*) FROM pg_database WHERE datname = 'testdb2'"
    )
    eq_(result[0][0], 0, "Database testdb2 should be dropped")
    
    print(f"✓ DROP DATABASE WITH FORCE completed successfully in {elapsed_time:.2f}s")


def test_004_drop_multiple_databases():
    """
    Test dropping multiple databases in sequence.
    
    This verifies that the health check worker cleanup is working
    correctly and doesn't interfere with subsequent operations.
    """
    print()
    print("Creating multiple test databases...")
    
    databases = ['testdb3', 'testdb4', 'testdb5']
    
    for db in databases:
        node1.run_sql_query(f"CREATE DATABASE {db}")
    
    # Give health check workers time to start
    time.sleep(3)
    
    print("Dropping databases sequentially...")
    
    for db in databases:
        start_time = time.time()
        node1.run_sql_query(f"DROP DATABASE {db}")
        elapsed_time = time.time() - start_time
        
        print(f"  Dropped {db} in {elapsed_time:.2f}s")
        
        assert elapsed_time < 10, \
            f"DROP DATABASE {db} took {elapsed_time:.2f}s, expected < 10s"
        
        # Verify the database is gone
        result = node1.run_sql_query(
            f"SELECT count(*) FROM pg_database WHERE datname = '{db}'"
        )
        eq_(result[0][0], 0, f"Database {db} should be dropped")
    
    print("✓ All databases dropped successfully")


def test_005_verify_no_orphaned_workers():
    """
    Verify that no health check workers are left running for dropped databases.
    
    This checks pg_stat_activity to ensure workers are properly cleaned up.
    """
    print()
    print("Checking for orphaned health check workers...")
    
    # Query for any pg_auto_failover health check workers
    result = node1.run_sql_query("""
        SELECT count(*), array_agg(datname) 
        FROM pg_stat_activity 
        WHERE application_name = 'pg_auto_failover health check worker'
          AND datname NOT IN (
              SELECT datname FROM pg_database WHERE datallowconn
          )
    """)
    
    orphaned_count = result[0][0]
    
    if orphaned_count > 0:
        print(f"WARNING: Found {orphaned_count} orphaned health check workers")
        print(f"Databases: {result[0][1]}")
    
    eq_(orphaned_count, 0, 
        "Should not have health check workers for non-existent databases")
    
    print("✓ No orphaned workers found")
