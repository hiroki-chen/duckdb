#include "duckdb/execution/operator/filter/physical_filter.hpp"

#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/planner/expression/bound_conjunction_expression.hpp"
#include "picachv_interfaces.h"
#include "plan_args.pb.h"
#include "transform.pb.h"

namespace duckdb {

PhysicalFilter::PhysicalFilter(vector<LogicalType> types, vector<unique_ptr<Expression>> select_list,
                               idx_t estimated_cardinality)
    : CachingPhysicalOperator(PhysicalOperatorType::FILTER, std::move(types), estimated_cardinality) {
	D_ASSERT(select_list.size() > 0);
	if (select_list.size() > 1) {
		// create a big AND out of the expressions
		auto conjunction = make_uniq<BoundConjunctionExpression>(ExpressionType::CONJUNCTION_AND);
		for (auto &expr : select_list) {
			conjunction->children.push_back(std::move(expr));
		}
		expression = std::move(conjunction);
	} else {
		expression = std::move(select_list[0]);
	}
}

class FilterState : public CachingOperatorState {
public:
	explicit FilterState(ExecutionContext &context, Expression &expr)
	    : executor(context.client, expr), sel(STANDARD_VECTOR_SIZE) {
	}

	ExpressionExecutor executor;
	SelectionVector sel;

public:
	void Finalize(const PhysicalOperator &op, ExecutionContext &context) override {
		context.thread.profiler.Flush(op, executor, "filter", 0);
	}
};

unique_ptr<OperatorState> PhysicalFilter::GetOperatorState(ExecutionContext &context) const {
	return make_uniq<FilterState>(context, *expression);
}

OperatorResultType PhysicalFilter::ExecuteInternal(ExecutionContext &context, DataChunk &input, DataChunk &chunk,
                                                   GlobalOperatorState &gstate, OperatorState &state_p) const {
	auto &state = state_p.Cast<FilterState>();
	idx_t result_count = state.executor.SelectExpression(input, state.sel);
	if (result_count == input.size()) {
		// nothing was filtered: skip adding any selection vectors
		chunk.Reference(input);
	} else {
		chunk.Slice(input, state.sel, result_count);
		// Here we need to tell the security monitor what is filtered.
		PicachvMessages::PlanArgument arg;
		// Just tell protobuf that we need to filter the data;
		// but this message does not contain any argument.
		(void)arg.mutable_transform();

		// Convert the selection vector into a boolean one.
		auto filter = arg.mutable_transform_info()->mutable_filter()->mutable_filter();
		filter->Resize(input.size(), false);

		for (idx_t i = 0; i < result_count; i++) {
			filter->Set(state.sel.get_index(i), true);
		}

		if (context.client.PolicyCheckingEnabled()) {
			duckdb_uuid_t uuid;
			if (execute_epilogue(context.client.ctx_uuid.uuid, PICACHV_UUID_LEN,
			                     (uint8_t *)arg.SerializeAsString().data(), arg.ByteSizeLong(), input.GetActiveUUID(),
			                     PICACHV_UUID_LEN, uuid.uuid, PICACHV_UUID_LEN) != ErrorCode::Success) {
				throw InternalException(GetErrorMessage());
			}

			chunk.SetActiveUUID(uuid.uuid);
		}
	}
	return OperatorResultType::NEED_MORE_INPUT;
}

string PhysicalFilter::ParamsToString() const {
	auto result = expression->GetName();
	result += "\n[INFOSEPARATOR]\n";
	result += StringUtil::Format("EC: %llu", estimated_cardinality);
	return result;
}

} // namespace duckdb
