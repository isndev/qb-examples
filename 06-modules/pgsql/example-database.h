#pragma once

#include <cstdlib>
#include <string>

#include <qb/uuid.h>

// Point the lessons at a disposable database without editing and rebuilding the sources.
inline const char *
example_pg_connection_string() {
    const char *uri = std::getenv("QB_EXAMPLE_PG_URI");
    return uri && *uri ? uri : "tcp://test:test@localhost:5432[test]";
}

// Only the transactions lesson needs a persistent table: READ ONLY transactions may write
// temporary tables. A fresh name prevents a concurrent run (or a previous crashed run) from
// owning this run's data. CREATE must fail on a collision; nothing is dropped before it succeeds.
inline std::string
example_pg_table_name(const char *prefix) {
    std::string name = prefix;
    name += '_';
    for (char c : uuids::to_string(qb::generate_random_uuid())) {
        if (c != '-')
            name += c;
    }
    return name;
}
