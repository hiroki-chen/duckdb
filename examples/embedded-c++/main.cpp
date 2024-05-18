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
	con.Query("INSERT INTO integers VALUES (3, 3)");

	ec = con.RegisterPolicyDataFrame("integers", "../../../data/json/simple_policy.json");
	if (ec != ErrorCode::Success) {
		std::cout << "RegisterPolicyDataFrame() returned " << ec << std::endl;
		return 1;
	}

	auto result = con.Query("explain(SELECT * FROM (integers t1 JOIN integers t2 ON t1.a = t2.b))");
	result->Print();
	std::cout << "================\n";
	result = con.Query("SELECT * FROM (integers t1 JOIN integers t2 ON t1.a = t2.b)");
	// result = con.Query("explain (SELECT * FROM integers)");
	// result->Print();
}
