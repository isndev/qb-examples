// examples/qbm/pgsql/example5_error_handling.cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <pgsql/pgsql.h>
#include <qb/io/async.h> // For qb::io::async::callback for delayed kill

#include <iostream>
#include <vector>
#include <string>

// IMPORTANT: Replace with your actual PostgreSQL connection string
// Changed format to match working examples
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

const char* ERROR_TEST_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS error_test_items (" //reliant on the framework's error handling mechanisms.
    "id SERIAL PRIMARY KEY, "
    "name TEXT NOT NULL UNIQUE, "
    "quantity INT CHECK (quantity >= 0), "
    "description TEXT"
    ");";

const char* PREPARE_INSERT_ERROR_ITEM = "insert_error_item_stmt_v5";
const char* PREPARE_SELECT_ERROR_ITEM_BY_NAME = "select_error_item_by_name_stmt_v5";

class ErrorHandlingActor : public qb::Actor {
public:
    ErrorHandlingActor() {}

    ~ErrorHandlingActor() override {
        if (_db_connection && _db_connection->is_connected()) {
            cleanupDatabase();
        }
    }

    bool onInit() override {
        qb::io::cout() << "ErrorHandlingActor [" << id() << "] onInit." << std::endl;
        registerEvent<qb::KillEvent>(*this);

        try {
            _db_connection = std::make_unique<qb::pg::tcp::database>(PG_CONNECTION_STRING);
            if (_db_connection->connect()) {
                qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;
            initializeSchemaAndStatements();
                return true;
            }
            auto& err = _db_connection->error();
            qb::io::cerr() << "Failed to connect to PostgreSQL: " << err.what() << std::endl;
            qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            return false;
        } catch (const std::exception& e) {
            qb::io::cerr() << "Error during ErrorHandlingActor init: " << e.what() << std::endl;
            return false;
        }
    }

    void on(const qb::KillEvent& event) {
        qb::io::cout() << "ErrorHandlingActor received KillEvent." << std::endl;
        kill();
    }

private:
    void printDbError(const std::string& context, const qb::pg::error::db_error& err) {
        qb::io::cerr() << "--- ERROR in " << context << " ---" << std::endl;
        qb::io::cerr() << "Message: " << err.what() << std::endl;
        qb::io::cerr() << "Severity: " << err.severity_string() << std::endl;
        qb::io::cerr() << "SQLSTATE (Code): " << err.code << " (Enum: " << static_cast<int>(err.sqlstate) << ")" << std::endl;
        if (err.detail()) qb::io::cerr() << "Detail: " << err.detail().value() << std::endl;
        if (err.hint()) qb::io::cerr() << "Hint: " << err.hint().value() << std::endl;
        if (err.position()) qb::io::cerr() << "Position: " << err.position().value() << std::endl;
        qb::io::cerr() << "-------------------------" << std::endl;
    }


    void initializeSchemaAndStatements() {
        qb::io::cout() << "Initializing error_test_items table and preparing statements..." << std::endl;
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                tr.execute(ERROR_TEST_TABLE_SQL)
                .prepare(PREPARE_INSERT_ERROR_ITEM,
                         "INSERT INTO error_test_items (name, quantity, description) VALUES ($1, $2, $3);",
                         {qb::pg::oid::text, qb::pg::oid::int4, qb::pg::oid::text})
                .prepare(PREPARE_SELECT_ERROR_ITEM_BY_NAME,
                         "SELECT name, quantity, description FROM error_test_items WHERE name = $1;",
                         {qb::pg::oid::text})
                .success([this](auto &) {
                    qb::io::cout() << "Error test schema and statements initialized." << std::endl;
                    runErrorScenarios();
                })
                .error([this](qb::pg::error::db_error const & err) {
                    printDbError("Schema/Statement Init", err);
                    this->kill();
                });
            },
            [this](qb::pg::error::db_error const & err) {
                printDbError("Begin Transaction for Schema Init", err);
                this->kill();
            }
        );
    }

    void runErrorScenarios() {
        qb::io::cout() << "\n--- Running Error Handling Scenarios ---" << std::endl;

        // Scenario 1: Syntax Error
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                qb::io::cout() << "\nScenario 1: Executing query with syntax error..." << std::endl;
                tr.execute("SELEC * FRM non_existent_table;", // Deliberate syntax error
                    [](qb::pg::transaction&, qb::pg::results&&) {
                        qb::io::cerr() << "UNEXPECTED SUCCESS: Syntax error query somehow succeeded." << std::endl;
                    },
                    [this](qb::pg::transaction&, qb::pg::error::db_error const & err) {
                        printDbError("Syntax Error Query", err);
                        // Expected: SQLSTATE 42601 (syntax_error)
                    }
                );
            },
            [this](qb::pg::error::db_error const & err) { 
                printDbError("Begin for Syntax Error", err); 
            }
        );


        // Scenario 2: Unique Constraint Violation
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                qb::io::cout() << "\nScenario 2: Unique constraint violation..." << std::endl;
                // First insert an item
                tr.execute(PREPARE_INSERT_ERROR_ITEM, {"UniqueItem", 10, "First instance"})
                .then([this](qb::pg::transaction& tr2){
                    // Attempt to insert the same item again
                    qb::io::cout() << "Attempting to insert duplicate 'UniqueItem'..." << std::endl;
                    tr2.execute(PREPARE_INSERT_ERROR_ITEM, {"UniqueItem", 20, "Second instance - should fail"});
                })
                .success([](auto &){
                    qb::io::cerr() << "UNEXPECTED SUCCESS: Duplicate insert somehow succeeded." << std::endl;
                })
                .error([this](qb::pg::error::db_error const & err){
                    printDbError("Unique Constraint Violation", err);
                    // Expected: SQLSTATE 23505 (unique_violation)
                });
            },
            [this](qb::pg::error::db_error const & err){ 
                printDbError("Begin for Unique Violation", err); 
            }
        );


        // Scenario 3: Check Constraint Violation
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                qb::io::cout() << "\nScenario 3: Check constraint violation (negative quantity)..." << std::endl;
                tr.execute(PREPARE_INSERT_ERROR_ITEM, {"CheckItem", -5, "Negative quantity - should fail"},
                    [](qb::pg::transaction&, qb::pg::results&&){
                        qb::io::cerr() << "UNEXPECTED SUCCESS: Insert with negative quantity succeeded." << std::endl;
                    },
                    [this](qb::pg::transaction&, qb::pg::error::db_error const & err){
                        printDbError("Check Constraint Violation", err);
                        // Expected: SQLSTATE 23514 (check_violation)
                    }
                );
            },
            [this](qb::pg::error::db_error const & err){ 
                printDbError("Begin for Check Violation", err); 
            }
        );
        
        // Scenario 4: Accessing NULL with non-optional .as<T>()
        // And Type Mismatch with .as<WrongType>()
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                qb::io::cout() << "\nScenario 4: Client-side result processing errors..." << std::endl;
                tr.execute(PREPARE_INSERT_ERROR_ITEM, {"ClientErrorItem", 5, std::optional<std::string>()}) // description is NULL
                .then([this](qb::pg::transaction& tr2){
                     tr2.execute(PREPARE_SELECT_ERROR_ITEM_BY_NAME, {"ClientErrorItem"},
                        [this](qb::pg::transaction&, qb::pg::results&& res){
                            if (res.empty()) {
                                qb::io::cerr() << "ClientErrorItem not found." << std::endl; return;
                            }
                            const auto& row = res[0];
                            try {
                                qb::io::cout() << "Attempting to get NULL description as non-optional string..." << std::endl;
                                [[maybe_unused]] std::string desc = row["description"].as<std::string>(); // Should throw
                                qb::io::cerr() << "UNEXPECTED: Retrieved NULL as non-optional string." << std::endl;
                            } catch (const qb::pg::error::value_is_null& e) {
                                qb::io::cout() << "CAUGHT EXPECTED: qb::pg::error::value_is_null: " << e.what() << std::endl;
                            } catch (const std::exception& e) {
                                qb::io::cerr() << "UNEXPECTED EXCEPTION (value_is_null): " << e.what() << std::endl;
                            }

                            try {
                                qb::io::cout() << "Attempting to get quantity (INT) as std::string..." << std::endl;
                                [[maybe_unused]] std::string qty_str = row["quantity"].as<std::string>(); // Should throw
                                qb::io::cerr() << "UNEXPECTED: Retrieved INT quantity as string." << std::endl;
                            } catch (const qb::pg::error::field_type_mismatch& e) {
                                qb::io::cout() << "CAUGHT EXPECTED: qb::pg::error::field_type_mismatch: " << e.what() << std::endl;
                            } catch (const std::exception& e) {
                                qb::io::cerr() << "UNEXPECTED EXCEPTION (field_type_mismatch): " << e.what() << std::endl;
                            }
                        },
                        [this](qb::pg::transaction&, qb::pg::error::db_error const & err){ 
                            printDbError("Select for Client Errors", err); 
                        }
                    );
                })
                .error([this](qb::pg::error::db_error const & err){ 
                    printDbError("Transaction for Client Errors", err); 
                });
            },
            [this](qb::pg::error::db_error const & err){ 
                printDbError("Begin for Client Errors", err); 
            }
        );

        // Use async callback to allow scenarios to complete before actor might be killed
        qb::io::async::callback([this](){
            qb::io::cout() << "\nError scenarios initiated. Check output for results." << std::endl;
            this->push<qb::KillEvent>(this->id()); // Kill actor after tests
        }, 3.0); // Delay to allow async operations
    }


    void cleanupDatabase() {
        qb::io::cout() << "Cleaning up error_test_items table..." << std::endl;
        auto status = _db_connection->begin(
            [](qb::pg::transaction& tr) {
                tr.execute("DROP TABLE IF EXISTS error_test_items;");
            }
        ).await();
        if (status) {
            qb::io::cout() << "error_test_items table dropped." << std::endl;
        } else {
            // Use the printDbError utility for status.error()
            printDbError("Cleanup Database", status.error());
        }
    }

    std::unique_ptr<qb::pg::tcp::database> _db_connection;
};

int main(int argc, char* argv[]) {
    qb::Main engine;
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. Please update it." << std::endl;
    }
    engine.addActor<ErrorHandlingActor>(0);
    engine.start();
    engine.join();
    if (engine.hasError()) {
        qb::io::cerr() << "Engine stopped due to an error." << std::endl;
        return 1;
    }
    qb::io::cout() << "Application finished." << std::endl;
    return 0;
} 