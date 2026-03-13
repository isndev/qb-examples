-- QB Task Manager - Database Initialization Script
-- Creates: user 'test' with password 'test' and database 'taskmanager'

-- Create user
CREATE USER test WITH PASSWORD 'test' CREATEDB;

-- Grant superuser privileges (optional, for development)
ALTER USER test WITH SUPERUSER;

-- Create database
CREATE DATABASE taskmanager OWNER test;

-- Connect to the new database
\c taskmanager;

-- Grant all privileges on the database
GRANT ALL PRIVILEGES ON DATABASE taskmanager TO test;

-- The tasks table will be created automatically by the application
-- using prepared statements on first startup

-- Verify setup
SELECT
    rolname as username,
    rolsuper as is_superuser,
    rolcreatedb as can_create_db
FROM pg_roles
WHERE rolname = 'test';
