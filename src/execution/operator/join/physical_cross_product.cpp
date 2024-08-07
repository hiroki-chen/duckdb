#include "duckdb/execution/operator/join/physical_cross_product.hpp"

#include "duckdb/common/types/column/column_data_collection.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/execution/operator/join/physical_join.hpp"
#include "picachv_interfaces.h"
#include "plan_args.pb.h"
#include "transform.pb.h"

#include <iostream>

namespace duckdb {

PhysicalCrossProduct::PhysicalCrossProduct(vector<LogicalType> types, unique_ptr<PhysicalOperator> left,
                                           unique_ptr<PhysicalOperator> right, idx_t estimated_cardinality)
    : CachingPhysicalOperator(PhysicalOperatorType::CROSS_PRODUCT, std::move(types), estimated_cardinality) {
	children.push_back(std::move(left));
	children.push_back(std::move(right));
}

//===--------------------------------------------------------------------===//
// Sink
//===--------------------------------------------------------------------===//
class CrossProductGlobalState : public GlobalSinkState {
public:
	explicit CrossProductGlobalState(ClientContext &context, const PhysicalCrossProduct &op)
	    : rhs_materialized(context, op.children[1]->GetTypes()) {
		rhs_materialized.InitializeAppend(append_state);
	}

	ColumnDataCollection rhs_materialized;
	ColumnDataAppendState append_state;
	mutex rhs_lock;
};

unique_ptr<GlobalSinkState> PhysicalCrossProduct::GetGlobalSinkState(ClientContext &context) const {
	return make_uniq<CrossProductGlobalState>(context, *this);
}

SinkResultType PhysicalCrossProduct::Sink(ExecutionContext &context, DataChunk &chunk, OperatorSinkInput &input) const {
	auto &sink = input.global_state.Cast<CrossProductGlobalState>();
	lock_guard<mutex> client_guard(sink.rhs_lock);
	sink.rhs_materialized.Append(sink.append_state, chunk);
	return SinkResultType::NEED_MORE_INPUT;
}

//===--------------------------------------------------------------------===//
// Operator
//===--------------------------------------------------------------------===//
CrossProductExecutor::CrossProductExecutor(ColumnDataCollection &rhs)
    : rhs(rhs), position_in_chunk(0), initialized(false), finished(false) {
	rhs.InitializeScanChunk(scan_chunk);
}

void CrossProductExecutor::Reset(DataChunk &input, DataChunk &output) {
	initialized = true;
	finished = false;
	scan_input_chunk = false;
	rhs.InitializeScan(scan_state);
	position_in_chunk = 0;
	scan_chunk.Reset();
}

bool CrossProductExecutor::NextValue(DataChunk &input, DataChunk &output) {
	if (!initialized) {
		// not initialized yet: initialize the scan
		Reset(input, output);
	}
	position_in_chunk++;
	idx_t chunk_size = scan_input_chunk ? input.size() : scan_chunk.size();
	if (position_in_chunk < chunk_size) {
		return true;
	}
	// fetch the next chunk
	rhs.Scan(scan_state, scan_chunk);
	position_in_chunk = 0;
	if (scan_chunk.size() == 0) {
		return false;
	}
	// the way the cross product works is that we keep one chunk constantly referenced
	// while iterating over the other chunk one value at a time
	// the second one is the chunk we are "scanning"

	// for the engine, it is better if we emit larger chunks
	// hence the chunk that we keep constantly referenced should be the larger of the two
	scan_input_chunk = input.size() < scan_chunk.size();
	return true;
}

OperatorResultType CrossProductExecutor::Execute(ClientContext &client, DataChunk &input, DataChunk &output) {
	if (rhs.Count() == 0) {
		// no RHS: empty result
		return OperatorResultType::FINISHED;
	}
	if (!NextValue(input, output)) {
		// ran out of entries on the RHS
		// reset the RHS and move to the next chunk on the LHS
		initialized = false;
		return OperatorResultType::NEED_MORE_INPUT;
	}

	// set up the constant chunk
	auto &constant_chunk = scan_input_chunk ? scan_chunk : input;
	auto col_count = constant_chunk.ColumnCount();
	auto col_offset = scan_input_chunk ? input.ColumnCount() : 0;
	output.SetCardinality(constant_chunk.size());
	for (idx_t i = 0; i < col_count; i++) {
		output.data[col_offset + i].Reference(constant_chunk.data[i]);
	}

	// for the chunk that we are scanning, scan a single value from that chunk
	auto &scan = scan_input_chunk ? input : scan_chunk;
	col_count = scan.ColumnCount();
	col_offset = scan_input_chunk ? 0 : input.ColumnCount();
	for (idx_t i = 0; i < col_count; i++) {
		ConstantVector::Reference(output.data[col_offset + i], scan.data[i], position_in_chunk, scan.size());
	}

	if (client.PolicyCheckingEnabled()) {
		PicachvMessages::PlanArgument arg;
		(void)arg.mutable_transform();
		PicachvMessages::JoinInformation *ti = arg.mutable_transform_info()->mutable_join();
		const DataChunk &lhs_chunk = scan_input_chunk ? input : scan_chunk;
		const DataChunk &rhs_chunk = scan_input_chunk ? scan_chunk : input;

		std::cout << "lhs_chunk.uuid = " << StringUtil::ByteArrayToString(lhs_chunk.GetActiveUUID(), PICACHV_UUID_LEN) << std::endl;
		std::cout << "rhs_chunk.uuid = " << StringUtil::ByteArrayToString(rhs_chunk.GetActiveUUID(), PICACHV_UUID_LEN) << std::endl;

		idx_t lhs_count = lhs_chunk.size();
		idx_t rhs_count = rhs_chunk.size();

		ti->mutable_lhs_df_uuid()->assign(reinterpret_cast<const char *>(lhs_chunk.GetActiveUUID()), PICACHV_UUID_LEN);
		ti->mutable_rhs_df_uuid()->assign(reinterpret_cast<const char *>(rhs_chunk.GetActiveUUID()), PICACHV_UUID_LEN);

		for (idx_t i = 0; i < lhs_chunk.GetTypes().size(); i++) {
			ti->mutable_left_columns()->Add(i);
		}
		for (idx_t i = 0; i < rhs_chunk.GetTypes().size(); i++) {
			ti->mutable_right_columns()->Add(i);
		}

		for (idx_t j = 0; j < rhs_count; j++) {
			for (idx_t i = 0; i < lhs_count; i++) {
				auto *row_info = ti->mutable_row_join_info()->Add();
				row_info->set_left_row(i);
				row_info->set_right_row(j);
			}
		}

		duckdb_uuid_t uuid;
		if (execute_epilogue(client.ctx_uuid.uuid, PICACHV_UUID_LEN, (uint8_t *)arg.SerializeAsString().c_str(),
		                     arg.ByteSizeLong(), nullptr, 0, uuid.uuid, PICACHV_UUID_LEN) != ErrorCode::Success) {
			throw InternalException("CrossProductExecutor::Execute: " + GetErrorMessage());
		}

		output.SetActiveUUID(uuid.uuid);
	}

	return OperatorResultType::HAVE_MORE_OUTPUT;
}

class CrossProductOperatorState : public CachingOperatorState {
public:
	explicit CrossProductOperatorState(ColumnDataCollection &rhs) : executor(rhs) {
	}

	CrossProductExecutor executor;
};

unique_ptr<OperatorState> PhysicalCrossProduct::GetOperatorState(ExecutionContext &context) const {
	auto &sink = sink_state->Cast<CrossProductGlobalState>();
	return make_uniq<CrossProductOperatorState>(sink.rhs_materialized);
}

OperatorResultType PhysicalCrossProduct::ExecuteInternal(ExecutionContext &context, DataChunk &input, DataChunk &chunk,
                                                         GlobalOperatorState &gstate, OperatorState &state_p) const {
	auto &state = state_p.Cast<CrossProductOperatorState>();
	return state.executor.Execute(context.client, input, chunk);
}

//===--------------------------------------------------------------------===//
// Pipeline Construction
//===--------------------------------------------------------------------===//
void PhysicalCrossProduct::BuildPipelines(Pipeline &current, MetaPipeline &meta_pipeline) {
	PhysicalJoin::BuildJoinPipelines(current, meta_pipeline, *this);
}

vector<const_reference<PhysicalOperator>> PhysicalCrossProduct::GetSources() const {
	return children[0]->GetSources();
}

} // namespace duckdb
