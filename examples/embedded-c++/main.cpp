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
	con.Query("CREATE TABLE integers2(a INTEGER, c INTEGER)");
	for (size_t i = 0; i < 5; i++) {
		std::string query = "INSERT INTO integers VALUES (" + std::to_string(i) + ", " + std::to_string(5 - i) + ")";
		con.Query(query);
		query = "INSERT INTO integers2 VALUES (" + std::to_string(i) + ", " + std::to_string(5 - i) + ")";
		con.Query(query);
	}

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
	auto result = con.Query("EXPLAIN (SELECT * from integers JOIN integers2 ON integers.a = integers2.a)");
	result->Print();
	con.EnablePolicyChecking();
	try {
		result = con.Query("SELECT * from integers JOIN integers2 ON integers.a = integers2.a");
	} catch (Exception e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}
	// result = con.Query("explain (SELECT * FROM integers)");
	// auto res = result->Fetch();
	// std::cout << "res == nullptr? " <<( res == nullptr) << "\n";
	result->Print();
}
