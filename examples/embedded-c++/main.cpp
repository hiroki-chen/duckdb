#include "duckdb.hpp"

#include <iostream>

using namespace duckdb;

void DoInsert(Connection &con) {
	con.Query("INSERT INTO integers VALUES (1, 5)");
	con.Query("INSERT INTO integers VALUES (2, 4)");
	con.Query("INSERT INTO integers VALUES (3, 5)");
	con.Query("INSERT INTO integers VALUES (4, 4)");
	con.Query("INSERT INTO integers VALUES (5, 3)");

	con.Query("INSERT INTO integers2 VALUES (1, 5)");
	con.Query("INSERT INTO integers2 VALUES (2, 5)");
	con.Query("INSERT INTO integers2 VALUES (3, 4)");
	con.Query("INSERT INTO integers2 VALUES (4, 4)");
	con.Query("INSERT INTO integers2 VALUES (5, 3)");
}

int main() {
	DuckDB db(nullptr);
	ErrorCode ec = db.InitializeMonitor();

	if (ec != ErrorCode::Success) {
		std::cout << "InitializeMonitor() returned " << ec << std::endl;
		return 1;
	}

	Connection con(db);

	ec = con.InitializeCtx();
	if (ec != ErrorCode::Success) {
		std::cout << "InitializeCtx() returned " << ec << std::endl;
		return 1;
	}

	con.Query("CREATE TABLE integers(a INTEGER, b INTEGER)");
	con.Query("CREATE TABLE integers2(a INTEGER, c INTEGER)");

	DoInsert(con);

	ec = con.RegisterPolicyDataFrame("integers", "../../../data/json/simple_policy.json");
	if (ec != ErrorCode::Success) {
		std::cout << "RegisterPolicyDataFrame() returned " << ec << std::endl;
		return 1;
	}
	ec = con.RegisterPolicyDataFrame("integers2", "../../../data/json/simple_policy.json");
	if (ec != ErrorCode::Success) {
		std::cout << "RegisterPolicyDataFrame() returned " << ec << std::endl;
		return 1;
	}

	std::cout << "================\n";
	auto result = con.Query("EXPLAIN (SELECT SUM(a), b FROM integers GROUP BY b)");
	result->Print();
	con.EnablePolicyChecking();
	try {
		result = con.Query("SELECT SUM(a), b FROM integers GROUP BY b");
	} catch (Exception e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}
	// result = con.Query("explain (SELECT * FROM integers)");
	// auto res = result->Fetch();
	// std::cout << "res == nullptr? " <<( res == nullptr) << "\n";
	result->Print();
}
