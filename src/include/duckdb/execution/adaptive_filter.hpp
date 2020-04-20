#pragma once

#include "duckdb/planner/expression/list.hpp"
#include <random>

namespace duckdb {
    class AdaptiveFilter{
    public:
        AdaptiveFilter(Expression &expr);
    void AdaptRuntimeStatistics(double duration);
    vector<idx_t> permutation;
    private:
	//! used for adaptive expression reordering
	idx_t iteration_count;
	idx_t swap_idx;
	idx_t right_random_border;
	idx_t observe_interval;
	idx_t execute_interval;
	double runtime_sum;
	double prev_mean;
	bool observe;
	bool warmup;
	vector<idx_t> swap_likeliness;
	std::default_random_engine generator;
    };
}