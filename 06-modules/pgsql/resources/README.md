# `examples/06-modules/pgsql/resources`

Assets staged next to the tier-06 pgsql binaries by `qb_stage_example_resources()`, so an example
can load them with a plain relative path (`"resources/sql/top-scores.sql"`) and carry no path
plumbing of its own.

| Path | Read by | Why it is a file and not a string literal |
|---|---|---|
| `sql/top-scores.sql` | `09-callbacks-and-await.cpp` | It is the subject of `Transaction::prepare_file()`, whose whole reason to exist is that a statement kept as SQL can be reviewed, diffed and linted as SQL. |

`qb_stage_example_resources` stages one directory per example directory, so everything here is
copied next to **every** binary in `06-modules/pgsql/` — the staging is per directory, not per
target.
