-- examples/06-modules/pgsql/resources/sql/top-scores.sql
--
-- Loaded at runtime by 06-modules/pgsql/09-callbacks-and-await.cpp through
-- Transaction::prepare_file(), which reads this file and PREPAREs its contents under a
-- name. Keeping the statement here rather than in a C++ string literal is the point of
-- that call: the SQL is reviewable as SQL, a DBA can read it without a C++ toolchain,
-- and it does not have to survive being escaped into a literal.
--
-- $1 is a minimum score (float8), $2 a row limit (int4). Both are BOUND, never pasted.
SELECT name, score
FROM qb_example_callbacks
WHERE score >= $1
ORDER BY score DESC
LIMIT $2;
