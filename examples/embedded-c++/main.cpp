#include "duckdb.hpp"

#include <iostream>

using namespace duckdb;

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
	for (size_t i = 0; i < 5; i++) {
		std::string query = "INSERT INTO integers VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ")";
		con.Query(query);
	}

	ec = con.RegisterPolicyDataFrame("integers", "../../../data/json/simple_policy.json");
	if (ec != ErrorCode::Success) {
		std::cout << "RegisterPolicyDataFrame() returned " << ec << std::endl;
		return 1;
	}

	// auto result = con.Query("explain(SELECT * FROM (integers t1 JOIN integers t2 ON t1.a = t2.b))");
	// result->Print();
	std::cout << "================\n";
	auto result = con.Query("EXPLAIN (SELECT a + 1 FROM integers)");
	result->Print();
	// TODO: FIX the bug. The projection seems not working.
	result = con.Query("SELECT a + 1 FROM integers");
	// result = con.Query("explain (SELECT * FROM integers)");
	// auto res = result->Fetch();
	// std::cout << "res == nullptr? " <<( res == nullptr) << "\n";
	result->Print();
}
