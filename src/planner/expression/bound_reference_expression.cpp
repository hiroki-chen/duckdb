#include "duckdb/planner/expression/bound_reference_expression.hpp"

#include "duckdb/common/to_string.hpp"
#include "duckdb/common/types/hash.hpp"
#include "duckdb/main/config.hpp"
#include "expr_args.pb.h"
#include "picachv_interfaces.h"

namespace duckdb {

BoundReferenceExpression::BoundReferenceExpression(string alias, LogicalType type, idx_t index)
    : Expression(ExpressionType::BOUND_REF, ExpressionClass::BOUND_REF, std::move(type)), index(index) {
	this->alias = std::move(alias);
}
BoundReferenceExpression::BoundReferenceExpression(LogicalType type, idx_t index)
    : BoundReferenceExpression(string(), std::move(type), index) {
}

string BoundReferenceExpression::ToString() const {
#ifdef DEBUG
	if (DBConfigOptions::debug_print_bindings) {
		return "#" + to_string(index);
	}
#endif
	if (!alias.empty()) {
		return alias;
	}
	return "#" + to_string(index);
}

bool BoundReferenceExpression::Equals(const BaseExpression &other_p) const {
	if (!Expression::Equals(other_p)) {
		return false;
	}
	auto &other = other_p.Cast<BoundReferenceExpression>();
	return other.index == index;
}

hash_t BoundReferenceExpression::Hash() const {
	return CombineHash(Expression::Hash(), duckdb::Hash<idx_t>(index));
}

unique_ptr<Expression> BoundReferenceExpression::Copy() {
	return make_uniq<BoundReferenceExpression>(alias, return_type, index);
}

void BoundReferenceExpression::CreateExprInArena(ClientContext &context) const {
	PicachvMessages::ExprArgument arg;
	PicachvMessages::ColumnExpr *expr = arg.mutable_column();

	expr->set_column_id(index);

	if (expr_from_args(context.ctx_uuid.uuid, PICACHV_UUID_LEN, (uint8_t *)arg.SerializeAsString().c_str(),
	                   arg.ByteSizeLong(), expr_uuid.uuid, PICACHV_UUID_LEN) != ErrorCode::Success) {
		throw InternalException(GetErrorMessage());
	}
}

} // namespace duckdb
