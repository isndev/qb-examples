-- QB Task Manager - Database Initialization Script
-- Creates: unprivileged user 'test' with password 'test' and database 'taskmanager'

-- Run this bootstrap as a database administrator. The application role only owns its database;
-- it does not need CREATEDB or SUPERUSER to create its tables there.
CREATE
USER test WITH PASSWORD 'test';

-- Create database
CREATE
DATABASE taskmanager OWNER test;

-- Connect to the new database
\c taskmanager

-- The tasks table will be created automatically by the application
-- using prepared statements on first startup

-- Verify setup
SELECT rolname     as username,
       rolsuper    as is_superuser,
       rolcreatedb as can_create_db
FROM pg_roles
WHERE rolname = 'test';
