#include "query/query_executor.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

#include "query/filter_operator.h"
#include "query/index_scan_operator.h"
#include "query/operator.h"
#include "query/parser.h"
#include "query/seq_scan_operator.h"

namespace minisgbd {
namespace {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return text;
}

bool IsKeyColumn(const std::string &column) {
  const std::string normalized_column = ToLower(column);
  return normalized_column == "key" || normalized_column == "id";
}

FilterOperator::Predicate BuildPredicate(const Condition &condition) {
  const std::string column = ToLower(condition.column);
  const int expected_value = condition.value;

  if (column == "key" || column == "id") {
    return [expected_value](const Tuple &tuple) {
      return tuple.key == expected_value;
    };
  }

  if (column == "value" || column == "valor") {
    return [expected_value](const Tuple &tuple) {
      return tuple.value == expected_value;
    };
  }

  throw std::invalid_argument("Columna desconocida en WHERE: " +
                              condition.column);
}

}  // namespace

QueryExecutor::QueryExecutor(std::string table_name,
                             const std::vector<Tuple> &tuples,
                             ExtensibleHashTable *hash_index)
    : table_name_(std::move(table_name)),
      tuples_(tuples),
      hash_index_(hash_index) {
  if (table_name_.empty()) {
    throw std::invalid_argument(
        "QueryExecutor requiere un nombre de tabla valido.");
  }
}

std::vector<Tuple> QueryExecutor::Execute(const std::string &sql) {
  return Execute(Parser::Parse(sql));
}

std::vector<Tuple> QueryExecutor::Execute(const SelectQuery &query) {
  if (ToLower(query.table) != ToLower(table_name_)) {
    throw std::invalid_argument("Tabla desconocida: " + query.table);
  }

  std::unique_ptr<Operator> source;
  std::unique_ptr<FilterOperator> filter;
  Operator *root = nullptr;

  if (!query.where.has_value()) {
    source = std::make_unique<SeqScanOperator>(tuples_);
    root = source.get();
    last_plan_type_ = QueryPlanType::kSeqScan;
  } else if (hash_index_ != nullptr && IsKeyColumn(query.where->column)) {
    source =
        std::make_unique<IndexScanOperator>(hash_index_, query.where->value);
    root = source.get();
    last_plan_type_ = QueryPlanType::kIndexScan;
  } else {
    FilterOperator::Predicate predicate = BuildPredicate(*query.where);
    source = std::make_unique<SeqScanOperator>(tuples_);
    filter =
        std::make_unique<FilterOperator>(source.get(), std::move(predicate));
    root = filter.get();
    last_plan_type_ = QueryPlanType::kFilteredSeqScan;
  }

  std::vector<Tuple> results;
  root->Open();

  try {
    Tuple tuple;
    while (root->Next(&tuple)) {
      results.push_back(tuple);
    }
  } catch (...) {
    root->Close();
    throw;
  }

  root->Close();
  return results;
}

QueryPlanType QueryExecutor::GetLastPlanType() const {
  return last_plan_type_;
}

}  // namespace minisgbd
