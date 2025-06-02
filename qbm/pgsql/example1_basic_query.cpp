// examples/qbm/pgsql/example1_basic_query.cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <pgsql/pgsql.h> // Corrected include path

#include <iostream> // For std::cerr

// IMPORTANT: Replace with your actual PostgreSQL connection string
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

class BasicQueryActor : public qb::Actor {
public:
    BasicQueryActor() {
        // Constructor
    }

    bool onInit() override {
        qb::io::cout() << "BasicQueryActor [" << id() << "] onInit on core " << getIndex() << std::endl;

        // Register event to handle graceful shutdown
        registerEvent<qb::KillEvent>(*this);

        _db_connection = std::make_unique<qb::pg::tcp::database>(PG_CONNECTION_STRING);;
        if (_db_connection->connect()) {
            qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;
            executeQuery();

            return true;
        }

        auto &e = _db_connection->error();
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << e.what() << std::endl;
        qb::io::cerr() << "Please ensure PostgreSQL is running and the connection "
                          "string is correct."
                       << std::endl;
        return false;
    }

    void on(const qb::KillEvent& event) {
        qb::io::cout() << "BasicQueryActor [" << id() << "] received KillEvent. Shutting down." << std::endl;
        kill();
    }

private:
    void executeQuery() {
        qb::io::cout() << "Executing query: SELECT version();" << std::endl;

        _db_connection->begin(
            [this](qb::pg::transaction& tr) { // Success callback for begin()
                tr.execute("SELECT version();",
                    [this](qb::pg::transaction& /*inner_tr*/, qb::pg::results&& res) { // Success callback for execute()
                        if (!res.empty()) {
                            const auto& row = res[0];
                            const auto& field = row[0];
                            qb::io::cout() << "PostgreSQL Version: " << field.as<std::string>() << std::endl;
                        } else {
                            qb::io::cout() << "Query executed, but no results returned." << std::endl;
                        }
                        this->kill();
                    },
                    [this](qb::pg::error::db_error const & err) { // Error callback for execute()
                        qb::io::cerr() << "Query execution failed: " << err.what() << std::endl;
                        qb::io::cerr() << "SQLSTATE: " << err.code << " Severity: " << err.what() << std::endl;
                    }
                );
            },
            [this](qb::pg::error::db_error const &err) { // Error callback for begin()
                 qb::io::cerr() << "Failed to begin transaction: " << err.what() << std::endl;
                 this->kill();
            }
        );
    }

    std::unique_ptr<qb::pg::tcp::database> _db_connection;
};

int main(int argc, char* argv[]) {
    qb::Main engine;

    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. "
                       << "Please update it in example1_basic_query.cpp with your actual database details." << std::endl;
    }

    engine.addActor<BasicQueryActor>(0);

    engine.start();
    engine.join();

    if (engine.hasError()) {
        qb::io::cerr() << "Engine stopped due to an error." << std::endl;
        return 1;
    }

    qb::io::cout() << "Application finished successfully." << std::endl;
    return 0;
} 