#include "duckdb_python/python_instance_checker.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/pair.hpp"
#include "duckdb/common/constants.hpp"

namespace duckdb {

PythonTypeWrapper& PythonModuleWrapper::Get(const string& type)
{
	auto entry = type_map.find(type);
	PythonTypeWrapper* ptr;
	if (entry == type_map.end()) {
		auto result = RegisterType(type);
		if (!result.second) {
			std::runtime_error("Internal exception: type(" + type + ") was not found in module(" + name + ")");
		}
		ptr = result.first->second.get();
	}
	else {
		ptr = entry->second.get();
	}
	return *ptr;
}

pair<unordered_map<string, unique_ptr<PythonTypeWrapper>>::iterator, bool> PythonModuleWrapper::RegisterType(const string& type_name)
{
	bool contains_type = py::hasattr(module_, type_name);
	if (!contains_type) {
		return make_pair(type_map.end(), false);
	}

	auto type = module_.attr(type_name);
	auto result = type_map.insert(make_pair(type_name, type));
	return result;
}

bool PythonTypeWrapper::InstanceOf(py::handle object)
{
	return py::isinstance(object, type);
}

pair<unordered_map<string, unique_ptr<PythonModuleWrapper>>::iterator, bool> PythonInstanceChecker::RegisterModule(const string& module_name)
{
	auto module_ = py::module::import(module_name);
	//error handling?
	auto result = module_map.insert(make_pair(module_name, module_));
	return result;
}

PythonModuleWrapper& PythonInstanceChecker::GetModule(const string& module_name) {
	auto entry = module_map.find(module_name);
	PythonModuleWrapper* ptr;
	if (entry == module_map.end()) {
		auto result = RegisterModule(type);
		if (!result.second) {
			std::runtime_error("Internal exception: module(" + module_name + ") was not found");
		}
		ptr = result.first->second.get();
	}
	else {
		ptr = entry->second.get();
	}
	return *ptr;
}

bool PythonInstanceChecker::IsInstanceOf(py::object object, const string& type_name, const string& module)
{
	auto& module_ = GetModule(module);
	auto& type = module_.Get(type_name);
	return type.InstanceOf(object);
}

} //namespace duckdb
