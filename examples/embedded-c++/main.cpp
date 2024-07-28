#include "duckdb.hpp"

#include <iostream>

using namespace duckdb;

// void DoInsert(Connection &con) {
// 	con.Query("INSERT INTO integers VALUES (1, 5)");
// 	con.Query("INSERT INTO integers VALUES (2, 4)");
// 	con.Query("INSERT INTO integers VALUES (3, 5)");
// 	con.Query("INSERT INTO integers VALUES (4, 4)");
// 	con.Query("INSERT INTO integers VALUES (5, 3)");

// 	con.Query("INSERT INTO integers2 VALUES (1, 5)");
// 	con.Query("INSERT INTO integers2 VALUES (2, 4)");
// 	con.Query("INSERT INTO integers2 VALUES (3, 5)");
// 	con.Query("INSERT INTO integers2 VALUES (4, 4)");
// 	con.Query("INSERT INTO integers2 VALUES (5, 3)");
// }

int main() {
	DuckDB db(nullptr);
	ErrorCode ec = ErrorCode::Success;

	Connection con(db);

	ec = con.InitializeCtx();
	if (ec != ErrorCode::Success) {
		std::cout << "InitializeCtx() returned " << ec << std::endl;
		return 1;
	}

	try {
		auto res = con.Query("SELECT * FROM '../../../../picachv/data/tables/lineitem.parquet'");
	} catch (Exception e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}
}
