// examples/qbm/pgsql/example4_data_types.cpp
#include <qb/actor.h>
#include <qb/main.h>
#include <qb/io.h>
#include <qb/system/timestamp.h> // For qb::Timestamp
#include <qb/uuid.h>             // For qb::uuid
#include <qb/json.h>             // For qb::json (nlohmann::json)
#include <qb/io/async.h> // For qb::io::async::callback for delayed kill
#include <pgsql/pgsql.h>

#include <iostream>
#include <vector>
#include <string>
#include <limits> // For numeric_limits
#include <iomanip> // For std::fixed, std::setprecision
#include <cctype> // For std::isprint

// IMPORTANT: Replace with your actual PostgreSQL connection string
// Changed format to match working examples
const char* PG_CONNECTION_STRING = "tcp://test:test@localhost:5432[test]";

const char* DATA_TYPES_TABLE_SQL =
    "CREATE TABLE IF NOT EXISTS data_types_test (" // reliant on the framework's error handling mechanisms.
    "id SERIAL PRIMARY KEY, "
    "integer_col INT, "
    "smallint_col SMALLINT, "
    "bigint_col BIGINT, "
    "text_col TEXT, "
    "varchar_col VARCHAR(50), "
    "char_col CHAR(10), "
    "boolean_col BOOLEAN, "
    "numeric_col DECIMAL(12, 3), " // Precision 12, Scale 3
    "real_col REAL, "
    "double_col DOUBLE PRECISION, "
    "date_col DATE, "
    "time_col TIME WITHOUT TIME ZONE, " // Explicitly without time zone
    "timestamp_col TIMESTAMP WITHOUT TIME ZONE, "
    "timestamptz_col TIMESTAMP WITH TIME ZONE, "
    "uuid_col UUID, "
    "bytea_col BYTEA, "
    "json_col JSON, "
    "jsonb_col JSONB, "
    "integer_array_col INT[], "
    "text_array_col TEXT[], "
    "nullable_text_col TEXT"
    ");";

const char* PREPARE_INSERT_DATA_TYPES = "insert_data_types_stmt_v4";
const char* PREPARE_SELECT_DATA_TYPES_BY_ID = "select_data_types_stmt_v4";

class DataTypesActor : public qb::Actor {
public:
    DataTypesActor() {}

    ~DataTypesActor() override {
        if (_db_connection && _db_connection->is_connected()) {
            cleanupDatabase();
        }
    }

    bool onInit() override {
        qb::io::cout() << "DataTypesActor [" << id() << "] onInit." << std::endl;
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
            qb::io::cerr() << "Error during DataTypesActor init: " << e.what() << std::endl;
            return false;
        }
    }

    void on(const qb::KillEvent& event) {
        qb::io::cout() << "DataTypesActor received KillEvent." << std::endl;
        kill();
    }

private:
    void initializeSchemaAndStatements() {
        qb::io::cout() << "Initializing data_types_test table and preparing statements..." << std::endl;
        _db_connection->begin(
            [this](qb::pg::transaction& tr) {
                tr.execute(DATA_TYPES_TABLE_SQL)
                .prepare(PREPARE_INSERT_DATA_TYPES,
                         "INSERT INTO data_types_test ("
                         "integer_col, smallint_col, bigint_col, text_col, varchar_col, char_col, "
                         "boolean_col, numeric_col, real_col, double_col, date_col, time_col, "
                         "timestamp_col, timestamptz_col, uuid_col, bytea_col, json_col, jsonb_col, "
                         "integer_array_col, text_array_col, nullable_text_col"
                         ") VALUES ("
                         "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21"
                         ") RETURNING id;",
                         { // Specify OIDs for all parameters
                             qb::pg::oid::int4, qb::pg::oid::int2, qb::pg::oid::int8, qb::pg::oid::text, qb::pg::oid::varchar, qb::pg::oid::bpchar,
                             qb::pg::oid::boolean, qb::pg::oid::numeric, qb::pg::oid::float4, qb::pg::oid::float8, qb::pg::oid::date, qb::pg::oid::time,
                             qb::pg::oid::timestamp, qb::pg::oid::timestamptz, qb::pg::oid::uuid, qb::pg::oid::bytea, qb::pg::oid::json, qb::pg::oid::jsonb,
                             qb::pg::oid::int4_array, qb::pg::oid::text_array, qb::pg::oid::text
                         })
                .prepare(PREPARE_SELECT_DATA_TYPES_BY_ID,
                         "SELECT * FROM data_types_test WHERE id = $1;",
                         {qb::pg::oid::int4})
                .success([this](auto &) {
                    qb::io::cout() << "Data types schema and statements initialized." << std::endl;
                    insertAndSelectData();
                })
                .error([this](qb::pg::error::db_error const & err) {
                    qb::io::cerr() << "Failed to initialize data types schema/statements: " << err.what() << std::endl;
                    qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                    this->kill();
                });
            },
            [this](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "Failed to begin transaction for data types schema init: " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                this->kill();
            }
        );
    }

    void insertAndSelectData() {
        qb::io::cout() << "\n--- Inserting and Selecting Various Data Types ---" << std::endl;

        // Sample data
        int p_integer_col = 12345;
        short p_smallint_col = 123;
        long long p_bigint_col = 1234567890123LL;
        std::string p_text_col = "This is a text field.";
        std::string p_varchar_col = "Varchar up to 50";
        std::string p_char_col = "char(10)  "; 
        bool p_boolean_col = true;
        std::string p_numeric_col = "12345.678"; 
        float p_real_col = 123.45f;
        double p_double_col = 9876.54321;

        qb::Timestamp now_ts = qb::Timestamp::now(); 
        qb::Timestamp p_date_col = now_ts;
        qb::Timestamp p_time_col = now_ts;
        qb::Timestamp p_timestamp_col = now_ts;
        qb::Timestamp p_timestamptz_col = now_ts;

        qb::uuid p_uuid_col = qb::uuid::generate_random_uuid();
        std::vector<char> p_bytea_col = {'b', 'y', 't', 'e', '\0', 'a', 'r', 'r', 'a', 'y'};
        qb::json p_json_col = {{"key1", "value1"}, {"key2", 100}};
        qb::json p_jsonb_col = {{"item", "jsonb_item"}, {"active", true}, {"nested", {1,2,3}}};
        std::vector<int> p_integer_array_col = {10, 20, 30, 40};
        std::vector<std::string> p_text_array_col = {"apple", "banana", "cherry"};
        std::optional<std::string> p_nullable_text_col = "This is not null.";

        _db_connection->begin(
            [=](qb::pg::transaction& tr) { // Capture all p_ variables by copy
                tr.execute(PREPARE_INSERT_DATA_TYPES,
                           {p_integer_col, p_smallint_col, p_bigint_col, p_text_col, p_varchar_col, p_char_col,
                            p_boolean_col, p_numeric_col, p_real_col, p_double_col, p_date_col, p_time_col,
                            p_timestamp_col, p_timestamptz_col, p_uuid_col, p_bytea_col, p_json_col, p_jsonb_col,
                            p_integer_array_col, p_text_array_col, p_nullable_text_col},
                    [this](qb::pg::transaction& /*tr_inner*/, qb::pg::results&& res) {
                        if (!res.empty() && res[0].size() > 0) {
                            int new_id = res[0][0].as<int>();
                            qb::io::cout() << "Successfully inserted data types row with ID: " << new_id << std::endl;
                            selectAndDisplayData(new_id);
                        } else {
                            qb::io::cerr() << "Insert data types row did not return an ID." << std::endl;
                        }
                    },
                    [this](qb::pg::error::db_error const & err) {
                        qb::io::cerr() << "Failed to insert data types row: " << err.what() << std::endl;
                        if(err.detail()) qb::io::cerr() << "Detail: " << err.detail().value() << std::endl;
                    }
                );
            },
            [this](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "Failed to begin transaction for inserting data types: " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
            }
        );
    }

    void selectAndDisplayData(int id) {
        _db_connection->execute(PREPARE_SELECT_DATA_TYPES_BY_ID, {id},
            [this, id](qb::pg::results&& res) {
                if (res.empty()) {
                    qb::io::cerr() << "Data types row with ID " << id << " not found after insert." << std::endl;
                    // qb::io::async::callback([this](){ this->push<qb::KillEvent>(this->id()); }, 0.1); // Optional: kill actor
                    return;
                }
                qb::io::cout() << "\n--- Retrieved Data for ID: " << id << " ---" << std::endl;
                const auto& row = res[0];

                qb::io::cout() << "ID: " << row["id"].as<int>() << std::endl;
                qb::io::cout() << "Integer: " << row["integer_col"].as<int>() << std::endl;
                qb::io::cout() << "SmallInt: " << row["smallint_col"].as<short>() << std::endl;
                qb::io::cout() << "BigInt: " << row["bigint_col"].as<long long>() << std::endl;
                qb::io::cout() << "Text: " << row["text_col"].as<std::string>() << std::endl;
                qb::io::cout() << "Varchar: " << row["varchar_col"].as<std::string>() << std::endl;
                qb::io::cout() << "Char: [" << row["char_col"].as<std::string>() << "]" << std::endl; 
                qb::io::cout() << "Boolean: " << (row["boolean_col"].as<bool>() ? "true" : "false") << std::endl;
                qb::io::cout() << "Numeric: " << row["numeric_col"].as<std::string>() << std::endl; 
                qb::io::cout() << "Real: " << std::fixed << std::setprecision(5) << row["real_col"].as<float>() << std::endl;
                qb::io::cout() << "Double: " << std::fixed << std::setprecision(10) << row["double_col"].as<double>() << std::endl;

                qb::io::cout() << "Date: " << row["date_col"].as<qb::Timestamp>().to_iso8601_date_string() << std::endl;
                qb::io::cout() << "Time: " << row["time_col"].as<qb::Timestamp>().to_iso8601_time_string() << std::endl;
                qb::io::cout() << "Timestamp: " << row["timestamp_col"].as<qb::Timestamp>().to_iso8601_string() << std::endl;
                qb::io::cout() << "TimestampTZ: " << row["timestamptz_col"].as<qb::Timestamp>().to_iso8601_string_utc() << " (UTC)" << std::endl;
                
                qb::io::cout() << "UUID: " << row["uuid_col"].as<qb::uuid>().to_string() << std::endl;
                
                auto bytea_data = row["bytea_col"].as<std::vector<char>>();
                qb::io::cout() << "ByteA: ";
                for(char c : bytea_data) { if(std::isprint(static_cast<unsigned char>(c))) qb::io::cout() << c; else qb::io::cout() << "."; }
                qb::io::cout() << " (Size: " << bytea_data.size() << ")" << std::endl;

                qb::io::cout() << "JSON: " << row["json_col"].as<qb::json>().dump(2) << std::endl;
                qb::io::cout() << "JSONB: " << row["jsonb_col"].as<qb::json>().dump(2) << std::endl;

                auto int_array = row["integer_array_col"].as<std::vector<int>>();
                qb::io::cout() << "Integer Array: [";
                for(size_t i=0; i < int_array.size(); ++i) { qb::io::cout() << int_array[i] << (i == int_array.size()-1 ? "" : ", ");}
                qb::io::cout() << "]" << std::endl;

                auto text_array = row["text_array_col"].as<std::vector<std::string>>();
                qb::io::cout() << "Text Array: [";
                for(size_t i=0; i < text_array.size(); ++i) { qb::io::cout() << "\"" << text_array[i] << "\"" << (i == text_array.size()-1 ? "" : ", ");}
                qb::io::cout() << "]" << std::endl;

                auto nullable_val = row["nullable_text_col"].as<std::optional<std::string>>();
                qb::io::cout() << "Nullable Text: " << (nullable_val ? nullable_val.value() : "NULL") << std::endl;
                
                // Signal shutdown after we're done
                qb::io::async::callback([this](){ this->push<qb::KillEvent>(this->id()); }, 0.5);
            },
            [this, id](qb::pg::error::db_error const & err) {
                qb::io::cerr() << "Error selecting data types row ID " << id << ": " << err.what() << std::endl;
                qb::io::cerr() << "SQLSTATE: " << err.code << std::endl;
                // Attempt to shut down after error
                qb::io::async::callback([this](){ this->push<qb::KillEvent>(this->id()); }, 0.1);
            }
        );
    }

    void cleanupDatabase() {
        qb::io::cout() << "Cleaning up data_types_test table..." << std::endl;
        auto status = _db_connection->begin(
            [](qb::pg::transaction& tr) {
                tr.execute("DROP TABLE IF EXISTS data_types_test;");
            }
        ).await();
        if (status) {
            qb::io::cout() << "data_types_test table dropped." << std::endl;
        } else {
            qb::io::cerr() << "Failed to drop data_types_test table: " << status.error().what() << std::endl;
            qb::io::cerr() << "SQLSTATE: " << status.error().code << std::endl;
        }
    }

    std::unique_ptr<qb::pg::tcp::database> _db_connection;
};

int main(int argc, char* argv[]) {
    qb::Main engine;
    if (std::string(PG_CONNECTION_STRING) == "tcp://user:password@host:port[dbname]") {
        qb::io::cerr() << "WARNING: Using default PG_CONNECTION_STRING. Please update it." << std::endl;
    }
    engine.addActor<DataTypesActor>(0);
    engine.start();
    engine.join();
    if (engine.hasError()) {
        qb::io::cerr() << "Engine stopped due to an error." << std::endl;
        return 1;
    }
    qb::io::cout() << "Application finished." << std::endl;
    return 0;
} 