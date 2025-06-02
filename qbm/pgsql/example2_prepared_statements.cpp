// examples/qbm/pgsql/example2_prepared_statements.cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h> // Changed from qb/io/stdout.h
#include <pgsql/pgsql.h>
#include <qb/io/async.h> // For qb::io::async::callback

#include <iostream> // For std::cerr fallback
#include <vector>
#include <string>

// IMPORTANT: Replace with your actual PostgreSQL connection string
// Example format: "tcp://user:password@host:port[database_name]"
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

// Prepared statement names
const char* PREPARE_CREATE_USERS_TABLE = "create_users_table_stmt_v2_5";
const char* PREPARE_INSERT_USER = "insert_user_stmt_v2_5";
const char* PREPARE_SELECT_USER_BY_ID = "select_user_by_id_stmt_v2_5";

struct User {
    int id;
    std::string name;
    std::optional<std::string> email;
};

class PreparedStatementActor : public qb::Actor {
public:
    PreparedStatementActor() {
        // qb::io::cout() << "PreparedStatementActor constructor." << std::endl; // Optional logging
    }

    ~PreparedStatementActor() override {
        cleanupDatabase();
    }

    bool onInit() override {
        qb::io::cout() << "PreparedStatementActor [" << id() << "] onInit on core " << getIndex() << std::endl;
        registerEvent<qb::KillEvent>(*this);

        _db_connection = std::make_unique<qb::pg::tcp::database>(PG_CONNECTION_STRING);
        if (_db_connection->connect()) { // Synchronous connect call
            qb::io::cout() << "Successfully connected to PostgreSQL." << std::endl;
            initializeDatabase(); // Proceed with DB setup
            return true;
        }

        // Connection failed
        auto& err = _db_connection->error(); // Get error details
        qb::io::cerr() << "Failed to connect to PostgreSQL: " << err.what() << std::endl;
        qb::io::cerr() << "SQLSTATE: " << err.code << std::endl; // Assuming err.code is SQLSTATE
        qb::io::cerr() << "Please ensure PostgreSQL is running and connection string is correct." << std::endl;
        // No kill() here, onInit returning false will stop the actor's progression.
        return false;
    }

    void on(const qb::KillEvent& event) {
        qb::io::cout() << "PreparedStatementActor [" << id() << "] received KillEvent. Shutting down." << std::endl;
        kill(); 
    }

private:
    void initializeDatabase() {
        qb::io::cout() << "Initializing database schema and preparing statements..." << std::endl;

        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                qb::io::cout() << "Transaction for DB init started. Creating table and preparing statements." << std::endl;
                tr.execute("CREATE TABLE IF NOT EXISTS users (" // reliant on the framework's error handling mechanisms.
                           "id SERIAL PRIMARY KEY, "
                           "name TEXT NOT NULL, "
                           "email TEXT UNIQUE"
                           ");")
                .prepare(PREPARE_INSERT_USER,
                           "INSERT INTO users (name, email) VALUES ($1, $2) RETURNING id;",
                           {qb::pg::oid::text, qb::pg::oid::text}) // Using lowercase oids as seen in some contexts
                .prepare(PREPARE_SELECT_USER_BY_ID,
                           "SELECT id, name, email FROM users WHERE id = $1;",
                           {qb::pg::oid::int4})  // Using lowercase oids
                .success([this](auto &) {
                    qb::io::cout() << "Database initialized and statements prepared successfully." << std::endl;
                    runApplicationLogic();
                })
                .error([this](qb::pg::error::db_error const & err) { // Corrected error callback signature
                    qb::io::cerr() << "Failed to initialize database or prepare statements: " << err.what() << std::endl;
                    qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                    this->kill(); 
                });
            },
            [this](qb::pg::error::db_error const & err) { // Corrected error callback signature
                qb::io::cerr() << "Failed to begin transaction for DB init: " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                this->kill();
            }
        );
    }

    void runApplicationLogic() {
        qb::io::cout() << "\nRunning application logic..." << std::endl;
        
        insertUser("Alice Wonderland", "alice@example.com");
        // Chain a kill event after a delay to allow operations to finish for the example
        // This is a simplistic way for an example to auto-terminate.
        // In real apps, shutdown logic would be more robust.
        qb::io::async::callback([this](){
            insertUser("Bob The Builder", std::nullopt);
            qb::io::async::callback([this](){
                insertUser("Charlie Chaplin", "charlie@example.com");
                 qb::io::async::callback([this](){
                    selectUser(999); // Try selecting a non-existent user
                    qb::io::async::callback([this](){
                        qb::io::cout() << "Example operations complete. Signaling shutdown." << std::endl;
                        this->push<qb::KillEvent>(this->id());
                    }, 1.0); // Delay after last operation
                }, 0.5);
            }, 0.5);
        }, 0.5);
    }

    void insertUser(const std::string& name, std::optional<std::string> email) {
        qb::io::cout() << "Attempting to insert user: " << name << ", Email: " << (email ? email.value() : "NULL") << std::endl;

        _db_connection->begin(
            [this, name, email](qb::pg::transaction& tr) {
                tr.execute(PREPARE_INSERT_USER, {name, email},
                    [this, name](qb::pg::transaction& /*tr_inner*/, qb::pg::results&& res) {
                        if (!res.empty() && res[0].size() > 0) {
                            int new_user_id = res[0][0].as<int>();
                            qb::io::cout() << "Successfully inserted user '" << name << "' with ID: " << new_user_id << std::endl;
                            selectUser(new_user_id); 
                        } else {
                             qb::io::cerr() << "Insert user '" << name << "' did not return an ID." << std::endl;
                        }
                    },
                    [this, name](qb::pg::error::db_error const & err) { // Corrected error callback signature
                        qb::io::cerr() << "Failed to insert user '" << name << "': " << err.what() << std::endl;
                        qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                         if (std::string(err.code) == "23505") { // unique_violation
                            qb::io::cerr() << "Detail: This might be due to a duplicate email." << std::endl;
                        }
                    }
                );
            },
            [this, name](qb::pg::error::db_error const & err) { // Corrected error callback signature
                qb::io::cerr() << "Failed to begin transaction for inserting user '" << name << "': " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            }
        );
    }

    void selectUser(int user_id) {
        qb::io::cout() << "Attempting to select user with ID: " << user_id << std::endl;

        _db_connection->execute(PREPARE_SELECT_USER_BY_ID, {user_id},
            [this, user_id](qb::pg::results&& res) { 
                if (!res.empty()) {
                    const auto& row = res[0];
                    User user;
                    user.id = row["id"].as<int>();
                    user.name = row["name"].as<std::string>();
                    user.email = row["email"].as<std::optional<std::string>>();

                    qb::io::cout() << "Selected User ---- ID: " << user.id
                                  << ", Name: " << user.name
                                  << ", Email: " << (user.email ? user.email.value() : "N/A")
                                  << " ----" << std::endl;
                } else {
                    qb::io::cout() << "User with ID " << user_id << " not found." << std::endl;
                }
            },
            [this, user_id](qb::pg::error::db_error const & err) { // Corrected error callback signature
                qb::io::cerr() << "Failed to select user with ID " << user_id << ": " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            }
        );
    }

    void cleanupDatabase() {
        qb::io::cout() << "Cleaning up database (dropping users table)..." << std::endl;
        
        auto status = _db_connection->begin(
            [](qb::pg::transaction& tr) {
                tr.execute("DROP TABLE IF EXISTS users;");
            }
        ).await(); // .await() is blocking, use with care, acceptable in destructor for cleanup.

        if (status) {
            qb::io::cout() << "Users table dropped successfully (or did not exist)." << std::endl;
        } else {
            qb::io::cerr() << "Failed to drop users table: " << status.error().what() << std::endl;
            qb::io::cerr() << "SQLSTATE: " << status.error().code << std::endl;
        }
    }

    std::unique_ptr<qb::pg::tcp::database> _db_connection;
};

int main(int argc, char* argv[]) {
    qb::Main engine; // Changed from qb::Main engine(argv[0])

    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") { // Default check
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. "
                       << "Please update it in example2_prepared_statements.cpp with your actual database details." << std::endl;
    }

    engine.addActor<PreparedStatementActor>(0);
    engine.start();
    engine.join();

    if (engine.hasError()) {
        qb::io::cerr() << "Engine stopped due to an error." << std::endl;
        return 1;
    }

    qb::io::cout() << "Application finished." << std::endl;
    return 0;
}