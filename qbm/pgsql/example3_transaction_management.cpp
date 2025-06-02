// examples/qbm/pgsql/example3_transaction_management.cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/io/async.h> // For qb::io::async::callback
#include <pgsql/pgsql.h>

#include <iostream>
#include <vector>
#include <string> // For std::string in displayAccount balance
#include <optional> // Use standard optional
#include <sstream>   // Required for std::ostringstream
#include <iomanip>   // Required for std::fixed, std::setprecision

// IMPORTANT: Replace with your actual PostgreSQL connection string
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

// Table for this example - using DOUBLE PRECISION for balance
const char* ACCOUNTS_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS accounts (" // Removed comment fragment
    "id SERIAL PRIMARY KEY, "
    "name TEXT NOT NULL UNIQUE, "
    "balance DOUBLE PRECISION NOT NULL DEFAULT 0.0" // Changed to DOUBLE PRECISION
    ");";

// Prepared statement names
const char* PREPARE_INSERT_ACCOUNT = "insert_account_stmt_v3_5"; // Renamed again
const char* PREPARE_UPDATE_BALANCE = "update_balance_stmt_v3_5";
const char* PREPARE_SELECT_ACCOUNT_BY_NAME = "select_account_by_name_stmt_v3_5";
const char* PREPARE_DELETE_ACCOUNT = "delete_account_stmt_v3_5";

// Helper function to format double as string with 2 decimal places
std::string format_decimal(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

class TransactionDemoActor : public qb::Actor {
public:
    TransactionDemoActor() {}

    ~TransactionDemoActor() override {
        cleanupDatabase();
    }

    bool onInit() override {
        qb::io::cout() << "TransactionDemoActor [" << id() << "] onInit." << std::endl;
        registerEvent<qb::KillEvent>(*this);

        _db_connection = std::make_unique<qb::pg::tcp::database>(PG_CONNECTION_STRING);
        if (_db_connection->connect()) {
            qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;
            initializeSchemaAndStatements();
            return true;
        }

        auto& err = _db_connection->error();
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << err.what() << std::endl;
        qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
        qb::io::cerr() << "Please ensure PostgreSQL is running and connection string is correct." << std::endl;
        return false;
    }

    void on(const qb::KillEvent& event) {
        qb::io::cout() << "TransactionDemoActor received KillEvent." << std::endl;
        kill();
    }

private:
    void initializeSchemaAndStatements() {
        qb::io::cout() << "Initializing accounts table (using DOUBLE PRECISION) and preparing statements..." << std::endl;
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                tr.execute(ACCOUNTS_TABLE_SQL)
                .prepare(PREPARE_INSERT_ACCOUNT,
                         "INSERT INTO accounts (name, balance) VALUES ($1, $2) RETURNING id, name, balance;",
                         {qb::pg::oid::text, qb::pg::oid::float8}) // Use float8 OID
                .prepare(PREPARE_UPDATE_BALANCE,
                         "UPDATE accounts SET balance = balance + $1 WHERE name = $2;",
                         {qb::pg::oid::float8, qb::pg::oid::text}) // Use float8 OID
                .prepare(PREPARE_SELECT_ACCOUNT_BY_NAME,
                         "SELECT id, name, balance FROM accounts WHERE name = $1;",
                         {qb::pg::oid::text})
                .prepare(PREPARE_DELETE_ACCOUNT,
                         "DELETE FROM accounts WHERE name = $1;"
                         )
                .success([this](auto &) {
                    qb::io::cout() << "Schema and statements initialized." << std::endl;
                    runTransactionScenarios();
                })
                .error([this](qb::pg::error::db_error const & err) {
                    qb::io::cerr() << "Failed to initialize schema/statements: " << err.what() << std::endl;
                    qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                    this->kill();
                });
            },
            [this](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "Failed to begin transaction for schema init: " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                this->kill();
            }
        );
    }

    void runTransactionScenarios() {
        qb::io::cout() << "\n--- Running Transaction Scenarios ---" << std::endl;
        
        // Scenario 1: Successful transfer
        transferFunds("Alice", "Bob", 50.00);

        // Scenario 2: Failed transfer (simulated by duplicate insert)
        transferFundsWithError("Charlie", "David");
        
        // Scenario 3: Verify state after transactions
        qb::io::async::callback([this]() {
            qb::io::cout() << "\n--- Verifying Account Balances Post-Transactions ---" << std::endl;
            displayAccount("Alice");
            displayAccount("Bob");
            displayAccount("Charlie"); 
            displayAccount("David");   
            displayAccount("Eve");
            displayAccount("ErrorTriggerAccount");

            qb::io::async::callback([this](){
                 qb::io::cout() << "Example operations and checks complete. Signaling shutdown." << std::endl;
                 this->push<qb::KillEvent>(this->id());
            }, 0.5);

        }, 2.0);
    }

    void displayAccount(const std::string& name) {
        _db_connection->execute(PREPARE_SELECT_ACCOUNT_BY_NAME, {name},
            [name](qb::pg::results&& res) {
                if (!res.empty()) {
                    qb::io::cout() << "Account '" << name << "': ID=" << res[0]["id"].as<int>()
                                   << ", Balance=" << res[0]["balance"].as<double>() << std::endl; // Retrieve as double
                } else {
                    qb::io::cout() << "Account '" << name << "' not found." << std::endl;
                }
            },
            [name](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "Error selecting account '" << name << "': " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            }
        );
    }

    void ensureAccountExists(qb::pg::transaction& tr, const std::string& name, double initial_balance) {
        qb::io::cout() << "Ensuring account: " << name << " with balance: " << initial_balance << std::endl;

        tr.execute(PREPARE_INSERT_ACCOUNT, {name, initial_balance}, // Pass double directly
            [name, initial_balance](qb::pg::transaction&, qb::pg::results&& res) {
                if(!res.empty()) {
                    qb::io::cout() << "Ensured account (created): '" << name << "' with balance " << res[0]["balance"].as<double>() << std::endl; // Retrieve as double
                } else {
                     qb::io::cout() << "Ensured account (likely created, no RETURNING data): '" << name << "' with initial balance " << initial_balance << std::endl;
                }
            },
            [name](qb::pg::error::db_error const & err) {
                if (std::string(err.code) == "23505") { 
                    qb::io::cout() << "Ensured account (already existed): '" << name << "'" << std::endl;
                } else {
                    qb::io::cerr() << "Error trying to ensure account '" << name << "': " << err.what() << std::endl;
                    qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                    throw err; 
                }
            }
        );
    }


    void transferFunds(const std::string& from_account, const std::string& to_account, double amount) {
        qb::io::cout() << "\nAttempting to transfer " << amount
                       << " from '" << from_account << "' to '" << to_account << "'." << std::endl;

        _db_connection->begin(
            [this, from_account, to_account, amount](qb::pg::transaction& tr) {
                qb::io::cout() << "Transaction started for transferring funds (" << from_account << " -> " << to_account << ")." << std::endl;

                ensureAccountExists(tr, from_account, 100.00); 
                ensureAccountExists(tr, to_account, 20.00);   
                
                tr.then([this, from_account, amount](qb::pg::transaction& tr2) {
                    qb::io::cout() << "Debiting " << amount << " from '" << from_account << "'" << std::endl;
                    tr2.execute(PREPARE_UPDATE_BALANCE, {-amount, from_account}); // Pass double directly
                })
                .then([this, to_account, amount](qb::pg::transaction& tr2) {
                    qb::io::cout() << "Crediting " << amount << " to '" << to_account << "'" << std::endl;
                    tr2.execute(PREPARE_UPDATE_BALANCE, {amount, to_account}); // Pass double directly
                })
                .success([from_account, to_account, amount](auto &) {
                    qb::io::cout() << "SUCCESS: Transaction for transferring " << amount
                                   << " from '" << from_account << "' to '" << to_account << "' committed." << std::endl;
                })
                .error([from_account, to_account, amount](qb::pg::error::db_error const & err) {
                    qb::io::cerr() << "ERROR: Transaction for transferring " << amount
                                   << " from '" << from_account << "' to '" << to_account << "' rolled back." << std::endl;
                    qb::io::cerr() << "Reason: " << err.what() << std::endl;
                    qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                    qb::io::cerr() << "Detail hint: " << err.what() << std::endl; 
                });
            },
            [from_account, to_account](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "FATAL: Could not begin transaction for transfer " 
                               << from_account << " -> " << to_account << ": " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            }
        );
    }
    
    void transferFundsWithError(const std::string& /*from_account_name*/, const std::string& /*to_account_name*/) {
        qb::io::cout() << "\nAttempting transaction that is expected to fail (duplicate insert of 'Eve')..." << std::endl;
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                // Ensure 'Eve' exists (first time should succeed or report already exists)
                ensureAccountExists(tr, "Eve", 100.0); 
                
                // Chain the attempt to insert 'Eve' again
                tr.then([this](qb::pg::transaction& tr2){
                    qb::io::cout() << "Attempting to insert duplicate account 'Eve' to trigger error..." << std::endl;
                    // Pass double directly for balance parameter
                    tr2.execute(PREPARE_INSERT_ACCOUNT, {std::string("Eve"), 50.0}); // Direct duplicate insert attempt
                })
                .success([](auto &) {
                    qb::io::cerr() << "UNEXPECTED SUCCESS: Transaction with intentional error (duplicate 'Eve') committed." << std::endl;
                })
                .error([](qb::pg::error::db_error const & err) {
                    qb::io::cout() << "EXPECTED FAILURE: Transaction (duplicate 'Eve') rolled back as intended." << std::endl;
                    qb::io::cout() << "Reason: " << err.what() << std::endl;
                    qb::io::cout() << "SQLSTATE: " << err.code << std::endl;
                });
            },
            [](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "FATAL: Could not begin transaction for 'Eve' error scenario: " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            }
        );
    }


    void cleanupDatabase() {
        if (!_db_connection) {
             qb::io::cout() << "Skipping cleanup, DB connection object not created." << std::endl;
            return;
        }
         qb::io::cout() << "Cleaning up accounts table..." << std::endl;
        auto status = _db_connection->begin(
            [](qb::pg::transaction& tr) {
                tr.execute("DROP TABLE IF EXISTS accounts;");
            }
        ).await();
        if (status) {
            qb::io::cout() << "Accounts table dropped." << std::endl;
        } else {
            qb::io::cerr() << "Failed to drop accounts table: " << status.error().what() << std::endl;
            qb::io::cerr() << "SQLSTATE: " << status.error().code << std::endl;
        }
    }

    std::unique_ptr<qb::pg::tcp::database> _db_connection;
};

int main(int argc, char* argv[]) {
    qb::Main engine;

    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") { // Default check
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. "
                       << "Please update it in example2_prepared_statements.cpp with your actual database details." << std::endl;
    }

    engine.addActor<TransactionDemoActor>(0);
    engine.start();
    engine.join();

    if (engine.hasError()) {
        qb::io::cerr() << "Engine stopped due to an error." << std::endl;
        return 1;
    }
    
    qb::io::cout() << "Application finished." << std::endl;
    return 0;
} 