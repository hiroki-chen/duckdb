#include "duckdb/execution/operator/join/perfect_hash_join_executor.hpp"

#include "duckdb/common/types/row/row_layout.hpp"
#include "duckdb/execution/operator/join/physical_hash_join.hpp"
#include "plan_args.pb.h"

#include <iostream>

namespace duckdb {

PerfectHashJoinExecutor::PerfectHashJoinExecutor(const PhysicalHashJoin &join_p, JoinHashTable &ht_p,
                                                 PerfectHashJoinStats perfect_join_stats)
    : join(join_p), ht(ht_p), perfect_join_statistics(std::move(perfect_join_stats)) {
}

bool PerfectHashJoinExecutor::CanDoPerfectHashJoin() {
	return perfect_join_statistics.is_build_small;
}

//===--------------------------------------------------------------------===//
// Build
//===--------------------------------------------------------------------===//
bool PerfectHashJoinExecutor::BuildPerfectHashTable(LogicalType &key_type) {
	// First, allocate memory for each build column
	auto build_size = perfect_join_statistics.build_range + 1;
	for (const auto &type : join.rhs_output_types) {
		perfect_hash_table.emplace_back(type, build_size);
	}

	// and for duplicate_checking
	bitmap_build_idx = make_unsafe_uniq_array<bool>(build_size);
	memset(bitmap_build_idx.get(), 0, sizeof(bool) * build_size); // set false

	// Now fill columns with build data

	return FullScanHashTable(key_type);
}

bool PerfectHashJoinExecutor::FullScanHashTable(LogicalType &key_type) {
	auto &data_collection = ht.GetDataCollection();

	// TODO: In a parallel finalize: One should exclusively lock and each thread should do one part of the code below.
	Vector tuples_addresses(LogicalType::POINTER, ht.Count()); // allocate space for all the tuples

	key_count = 0;
	if (data_collection.ChunkCount() > 0) {
		JoinHTScanState join_ht_state(data_collection, 0, data_collection.ChunkCount(),
		                              TupleDataPinProperties::KEEP_EVERYTHING_PINNED);

		// Go through all the blocks and fill the keys addresses
		key_count = ht.FillWithHTOffsets(join_ht_state, tuples_addresses);
	}

	std::cout << "key_count is " << key_count << std::endl;

	// Scan the build keys in the hash table
	Vector build_vector(key_type, key_count);
	RowOperations::FullScanColumn(ht.layout, tuples_addresses, build_vector, key_count, 0);

	// Now fill the selection vector using the build keys and create a sequential vector
	// TODO: add check for fast pass when probe is part of build domain
	sel_build = SelectionVector(key_count + 1);
	sel_tuples = SelectionVector(key_count + 1);
	bool success = FillSelectionVectorSwitchBuild(build_vector, sel_build, sel_tuples, key_count);

	// early out
	if (!success) {
		return false;
	}
	if (unique_keys == perfect_join_statistics.build_range + 1 && !ht.has_null) {
		perfect_join_statistics.is_build_dense = true;
	}
	key_count = unique_keys; // do not consider keys out of the range
	std::cout << "unique_keys is " << unique_keys << std::endl;

	// Full scan the remaining build columns and fill the perfect hash table
	//
	// Filling is done by moving the data from the position sel_tuples[i] to the sel_build[i] for each column.
	const auto build_size = perfect_join_statistics.build_range + 1;
	for (idx_t i = 0; i < join.rhs_output_types.size(); i++) {
		auto &vector = perfect_hash_table[i];
		const auto output_col_idx = ht.output_columns[i];
		D_ASSERT(vector.GetType() == ht.layout.GetTypes()[output_col_idx]);
		if (build_size > STANDARD_VECTOR_SIZE) {
			auto &col_mask = FlatVector::Validity(vector);
			col_mask.Initialize(build_size);
		}
		data_collection.Gather(tuples_addresses, sel_tuples, key_count, output_col_idx, vector, sel_build, nullptr);
	}

	return true;
}

bool PerfectHashJoinExecutor::FillSelectionVectorSwitchBuild(Vector &source, SelectionVector &sel_vec,
                                                             SelectionVector &seq_sel_vec, idx_t count) {
	switch (source.GetType().InternalType()) {
	case PhysicalType::INT8:
		return TemplatedFillSelectionVectorBuild<int8_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::INT16:
		return TemplatedFillSelectionVectorBuild<int16_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::INT32:
		return TemplatedFillSelectionVectorBuild<int32_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::INT64:
		return TemplatedFillSelectionVectorBuild<int64_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::UINT8:
		return TemplatedFillSelectionVectorBuild<uint8_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::UINT16:
		return TemplatedFillSelectionVectorBuild<uint16_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::UINT32:
		return TemplatedFillSelectionVectorBuild<uint32_t>(source, sel_vec, seq_sel_vec, count);
	case PhysicalType::UINT64:
		return TemplatedFillSelectionVectorBuild<uint64_t>(source, sel_vec, seq_sel_vec, count);
	default:
		throw NotImplementedException("Type not supported for perfect hash join");
	}
}

template <typename T>
bool PerfectHashJoinExecutor::TemplatedFillSelectionVectorBuild(Vector &source, SelectionVector &sel_vec,
                                                                SelectionVector &seq_sel_vec, idx_t count) {
	if (perfect_join_statistics.build_min.IsNull() || perfect_join_statistics.build_max.IsNull()) {
		return false;
	}
	auto min_value = perfect_join_statistics.build_min.GetValueUnsafe<T>();
	auto max_value = perfect_join_statistics.build_max.GetValueUnsafe<T>();
	UnifiedVectorFormat vector_data;
	source.ToUnifiedFormat(count, vector_data);
	auto data = reinterpret_cast<T *>(vector_data.data);
	// generate the selection vector
	for (idx_t i = 0, sel_idx = 0; i < count; ++i) {
		auto data_idx = vector_data.sel->get_index(i);
		auto input_value = data[data_idx];
		// add index to selection vector if value in the range
		if (min_value <= input_value && input_value <= max_value) {
			auto idx = (idx_t)(input_value - min_value); // subtract min value to get the idx position
			sel_vec.set_index(sel_idx, idx);
			if (bitmap_build_idx[idx]) {
				return false;
			} else {
				bitmap_build_idx[idx] = true;
				unique_keys++;
			}
			seq_sel_vec.set_index(sel_idx++, i);
		}
	}
	return true;
}

//===--------------------------------------------------------------------===//
// Probe
//===--------------------------------------------------------------------===//
class PerfectHashJoinState : public OperatorState {
public:
	PerfectHashJoinState(ClientContext &context, const PhysicalHashJoin &join) : probe_executor(context) {
		join_keys.Initialize(Allocator::Get(context), join.condition_types);
		for (auto &cond : join.conditions) {
			probe_executor.AddExpression(*cond.left);
		}
		build_sel_vec.Initialize(STANDARD_VECTOR_SIZE);
		probe_sel_vec.Initialize(STANDARD_VECTOR_SIZE);
		seq_sel_vec.Initialize(STANDARD_VECTOR_SIZE);
	}

	DataChunk join_keys;
	ExpressionExecutor probe_executor;
	SelectionVector build_sel_vec;
	SelectionVector probe_sel_vec;
	SelectionVector seq_sel_vec;
};

unique_ptr<OperatorState> PerfectHashJoinExecutor::GetOperatorState(ExecutionContext &context) {
	auto state = make_uniq<PerfectHashJoinState>(context.client, join);
	return std::move(state);
}

// BUG: The size of build vec is not properly synced.
//
// For this function we need obtain the row information that describes for each joined tuple,
// the indices of the tuples that were joined. Called by CachingPhysicalOperator::Execute
//
// Here, input is the "rhs table" where the "lhs table" is actually the payload held by the hash table.
OperatorResultType PerfectHashJoinExecutor::ProbePerfectHashTable(ExecutionContext &context, DataChunk &input,
                                                                  DataChunk &result, OperatorState &state_p,
                                                                  const uint8_t *build_uuid) {
	auto &state = state_p.Cast<PerfectHashJoinState>();
	// keeps track of how many probe keys have a match
	idx_t probe_sel_count = 0;

	// fetch the join keys from the chunk
	state.join_keys.Reset();
	state.probe_executor.Execute(input, state.join_keys);

	// select the keys that are in the min-max range
	auto &keys_vec = state.join_keys.data[0];
	auto keys_count = state.join_keys.size();
	// todo: add check for fast pass when probe is part of build domain
	FillSelectionVectorSwitchProbe(keys_vec, state.build_sel_vec, state.probe_sel_vec, keys_count, probe_sel_count);

	// @hiroki-chen: For this type of hash join all columns are preserved so there is no need
	// to check "which" columns are ouptut.

	// If build is dense and probe is in build's domain, just reference probe
	if (perfect_join_statistics.is_build_dense && keys_count == probe_sel_count) {
		result.Reference(input);
	} else {
		// Otherwise, filter it out the values that do not match
		// Note that this function filters out `input` according to `probe_sel_vec`
		// and then store this into `result`.
		result.Slice(context.client, input, state.probe_sel_vec, probe_sel_count, 0);
	}

	// on the build side, we need to fetch the data and build dictionary vectors with the sel_vec
	for (idx_t i = 0; i < join.rhs_output_types.size(); i++) {
		auto &result_vector = result.data[input.ColumnCount() + i];
		D_ASSERT(result_vector.GetType() == ht.layout.GetTypes()[ht.output_columns[i]]);
		auto &build_vec = perfect_hash_table[i];
		result_vector.Reference(build_vec);
		result_vector.Slice(state.build_sel_vec, probe_sel_count);
	}

	if (context.client.PolicyCheckingEnabled()) {
		unordered_map<idx_t, idx_t> recover;
		for (idx_t i = 0; i < probe_sel_count; i++) {
			recover[sel_build[i]] = sel_tuples[i];
		}

		PicachvMessages::PlanArgument arg;
		PicachvMessages::JoinInformation *join_info = arg.mutable_transform_info()->mutable_join();
		google::protobuf::RepeatedPtrField<PicachvMessages::RowJoinInformation> *row_join_info =
		    join_info->mutable_row_join_info();

		for (idx_t i = 0; i < probe_sel_count; i++) {
			idx_t left = state.probe_sel_vec.get_index(i);
			idx_t right = state.build_sel_vec.get_index(i);
			right = recover[right];

			PicachvMessages::RowJoinInformation row_info;
			row_info.set_left_row(left);
			row_info.set_right_row(right);

			row_join_info->Add(std::move(row_info));
		}

		join_info->mutable_lhs_df_uuid()->assign(reinterpret_cast<const char *>(input.GetActiveUUID()),
		                                         PICACHV_UUID_LEN);
		join_info->mutable_rhs_df_uuid()->assign(reinterpret_cast<const char *>(build_uuid), PICACHV_UUID_LEN);

		for (size_t i = 0; i < input.ColumnCount(); i++) {
			join_info->mutable_left_columns()->Add(i);
		}
		for (size_t i = 0; i < ht.output_columns.size(); i++) {
			join_info->mutable_right_columns()->Add(ht.output_columns[i]);
		}

		// This step just ensures that we have set the proper protobuf field
		// to indicate that we are doing a join but no other logic of computation.
		(void)arg.mutable_transform();
		duckdb_uuid_t uuid;
		if (execute_epilogue(context.client.ctx_uuid.uuid, PICACHV_UUID_LEN, (uint8_t *)arg.SerializeAsString().c_str(),
		                     arg.ByteSizeLong(), input.GetActiveUUID(), PICACHV_UUID_LEN, uuid.uuid,
		                     PICACHV_UUID_LEN) != ErrorCode::Success) {
			throw InternalException(GetErrorMessage());
		}
		result.SetActiveUUID(uuid.uuid);

		std::cout << "result have " << result.GetTypes().size() << " columns\n";
	}

	return OperatorResultType::NEED_MORE_INPUT;
}

void PerfectHashJoinExecutor::FillSelectionVectorSwitchProbe(Vector &source, SelectionVector &build_sel_vec,
                                                             SelectionVector &probe_sel_vec, idx_t count,
                                                             idx_t &probe_sel_count) {
	switch (source.GetType().InternalType()) {
	case PhysicalType::INT8:
		TemplatedFillSelectionVectorProbe<int8_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::INT16:
		TemplatedFillSelectionVectorProbe<int16_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::INT32:
		TemplatedFillSelectionVectorProbe<int32_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::INT64:
		TemplatedFillSelectionVectorProbe<int64_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::UINT8:
		TemplatedFillSelectionVectorProbe<uint8_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::UINT16:
		TemplatedFillSelectionVectorProbe<uint16_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::UINT32:
		TemplatedFillSelectionVectorProbe<uint32_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	case PhysicalType::UINT64:
		TemplatedFillSelectionVectorProbe<uint64_t>(source, build_sel_vec, probe_sel_vec, count, probe_sel_count);
		break;
	default:
		throw NotImplementedException("Type not supported");
	}
}

template <typename T>
void PerfectHashJoinExecutor::TemplatedFillSelectionVectorProbe(Vector &source, SelectionVector &build_sel_vec,
                                                                SelectionVector &probe_sel_vec, idx_t count,
                                                                idx_t &probe_sel_count) {
	auto min_value = perfect_join_statistics.build_min.GetValueUnsafe<T>();
	auto max_value = perfect_join_statistics.build_max.GetValueUnsafe<T>();

	UnifiedVectorFormat vector_data;
	source.ToUnifiedFormat(count, vector_data);
	auto data = reinterpret_cast<T *>(vector_data.data);
	auto validity_mask = &vector_data.validity;
	// build selection vector for non-dense build
	if (validity_mask->AllValid()) {
		for (idx_t i = 0, sel_idx = 0; i < count; ++i) {
			// retrieve value from vector
			auto data_idx = vector_data.sel->get_index(i);
			auto input_value = data[data_idx];
			// add index to selection vector if value in the range
			if (min_value <= input_value && input_value <= max_value) {
				auto idx = (idx_t)(input_value - min_value); // subtract min value to get the idx position
				                                             // check for matches in the build
				if (bitmap_build_idx[idx]) {
					build_sel_vec.set_index(sel_idx, idx);
					probe_sel_vec.set_index(sel_idx++, i);
					probe_sel_count++;
				}
			}
		}
	} else {
		for (idx_t i = 0, sel_idx = 0; i < count; ++i) {
			// retrieve value from vector
			auto data_idx = vector_data.sel->get_index(i);
			if (!validity_mask->RowIsValid(data_idx)) {
				continue;
			}
			auto input_value = data[data_idx];
			// add index to selection vector if value in the range
			if (min_value <= input_value && input_value <= max_value) {
				auto idx = (idx_t)(input_value - min_value); // subtract min value to get the idx position
				                                             // check for matches in the build
				if (bitmap_build_idx[idx]) {
					build_sel_vec.set_index(sel_idx, idx);
					probe_sel_vec.set_index(sel_idx++, i);
					probe_sel_count++;
				}
			}
		}
	}
}

} // namespace duckdb
