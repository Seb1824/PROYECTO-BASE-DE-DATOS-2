#include "query/query_executor.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "query/filter_operator.h"
#include "query/index_scan_operator.h"
#include "query/operator.h"
#include "query/parser.h"
#include "query/projection_operator.h"
#include "query/seq_scan_operator.h"
#include "storage/table_page.h"

namespace minisgbd {
namespace {

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return text;
}

std::string NormalizeColumn(const std::string &column) {
  const std::string normalized = ToLower(column);
  if (normalized == "key" || normalized == "id") {
    return "key";
  }
  if (normalized == "value" || normalized == "valor") {
    return "value";
  }
  throw std::invalid_argument("Columna desconocida: " + column);
}

bool IsKeyColumn(const std::string &column) {
  return NormalizeColumn(column) == "key";
}

bool CompareValues(int actual, ComparisonOperator comparison, int expected) {
  switch (comparison) {
    case ComparisonOperator::kEqual:
      return actual == expected;
    case ComparisonOperator::kNotEqual:
      return actual != expected;
    case ComparisonOperator::kLess:
      return actual < expected;
    case ComparisonOperator::kLessOrEqual:
      return actual <= expected;
    case ComparisonOperator::kGreater:
      return actual > expected;
    case ComparisonOperator::kGreaterOrEqual:
      return actual >= expected;
  }
  return false;
}

FilterOperator::Predicate BuildPredicate(const Condition &condition) {
  const std::string column = NormalizeColumn(condition.column);
  const ComparisonOperator comparison = condition.comparison;
  const int expected_value = condition.value;

  if (column == "key") {
    return [comparison, expected_value](const Tuple &tuple) {
      return CompareValues(tuple.key, comparison, expected_value);
    };
  }
  return [comparison, expected_value](const Tuple &tuple) {
    return CompareValues(tuple.value, comparison, expected_value);
  };
}

struct ProjectionSelection {
  bool include_key{false};
  bool include_value{false};
  std::vector<std::string> columns;
};

ProjectionSelection BuildProjection(const SelectQuery &query) {
  if (query.select_all) {
    return ProjectionSelection{true, true, {"key", "value"}};
  }
  if (query.columns.empty()) {
    throw std::invalid_argument(
        "SELECT requiere al menos una columna.");
  }

  ProjectionSelection selection;
  std::unordered_set<std::string> seen;
  for (const std::string &column : query.columns) {
    const std::string normalized = NormalizeColumn(column);
    if (!seen.insert(normalized).second) {
      throw std::invalid_argument(
          "SELECT contiene una columna duplicada: " + column);
    }
    selection.columns.push_back(normalized);
    if (normalized == "key") {
      selection.include_key = true;
    } else {
      selection.include_value = true;
    }
  }
  return selection;
}

void ValidateTable(const std::string &requested,
                   const std::string &available) {
  if (ToLower(requested) != ToLower(available)) {
    throw std::invalid_argument("Tabla desconocida: " + requested);
  }
}

std::vector<LocatedRecord> FindMatchingRecords(
    TableHeap *table_heap, ExtensibleHashTable *hash_index,
    const std::optional<Condition> &where) {
  if (table_heap == nullptr) {
    throw std::invalid_argument(
        "La operacion requiere un TableHeap fisico.");
  }
  if (!where.has_value()) {
    return table_heap->ReadAll();
  }

  const FilterOperator::Predicate predicate = BuildPredicate(*where);
  if (hash_index != nullptr && IsKeyColumn(where->column) &&
      where->comparison == ComparisonOperator::kEqual) {
    int encoded_rid = 0;
    if (!hash_index->GetValue(where->value, &encoded_rid)) {
      return {};
    }
    const RID rid = RID::Decode(encoded_rid);
    int key = 0;
    int value = 0;
    if (!table_heap->GetRecord(rid, &key, &value) ||
        key != where->value) {
      throw std::runtime_error(
          "El indice contiene un RID inconsistente.");
    }
    const Tuple tuple{key, value};
    if (predicate(tuple)) {
      return {LocatedRecord{rid, key, value}};
    }
    return {};
  }

  std::vector<LocatedRecord> matches;
  for (const LocatedRecord &record : table_heap->ReadAll()) {
    if (predicate(Tuple{record.key, record.value})) {
      matches.push_back(record);
    }
  }
  return matches;
}

}  // namespace

QueryExecutor::QueryExecutor(std::string table_name,
                             const std::vector<Tuple> &tuples,
                             ExtensibleHashTable *hash_index)
    : table_name_(std::move(table_name)),
      tuples_(&tuples),
      hash_index_(hash_index) {
  if (table_name_.empty()) {
    throw std::invalid_argument(
        "QueryExecutor requiere un nombre de tabla valido.");
  }
}

QueryExecutor::QueryExecutor(std::string table_name, TableHeap *table_heap,
                             ExtensibleHashTable *hash_index)
    : table_name_(std::move(table_name)),
      table_heap_(table_heap),
      hash_index_(hash_index) {
  if (table_name_.empty()) {
    throw std::invalid_argument(
        "QueryExecutor requiere un nombre de tabla valido.");
  }
  if (table_heap_ == nullptr) {
    throw std::invalid_argument(
        "QueryExecutor requiere un TableHeap valido.");
  }
}

std::vector<Tuple> QueryExecutor::Execute(const std::string &sql) {
  QueryStatement statement = Parser::ParseStatement(sql);
  if (std::holds_alternative<SelectQuery>(statement)) {
    return Execute(std::get<SelectQuery>(statement));
  }
  if (std::holds_alternative<InsertQuery>(statement)) {
    return Execute(std::get<InsertQuery>(statement));
  }
  if (std::holds_alternative<UpdateQuery>(statement)) {
    return Execute(std::get<UpdateQuery>(statement));
  }
  return Execute(std::get<DeleteQuery>(statement));
}

std::vector<Tuple> QueryExecutor::Execute(const SelectQuery &query) {
  ValidateTable(query.table, table_name_);
  const ProjectionSelection selection = BuildProjection(query);
  last_output_columns_ = selection.columns;

  std::unique_ptr<Operator> source;
  std::unique_ptr<FilterOperator> filter;
  std::unique_ptr<ProjectionOperator> projection;
  Operator *root = nullptr;
  auto make_seq_scan = [this]() -> std::unique_ptr<Operator> {
    if (table_heap_ != nullptr) {
      return std::make_unique<SeqScanOperator>(table_heap_);
    }
    return std::make_unique<SeqScanOperator>(*tuples_);
  };

  if (!query.where.has_value()) {
    source = make_seq_scan();
    root = source.get();
    last_plan_type_ = QueryPlanType::kSeqScan;
  } else if (table_heap_ != nullptr && hash_index_ != nullptr &&
             IsKeyColumn(query.where->column) &&
             query.where->comparison == ComparisonOperator::kEqual) {
    source = std::make_unique<IndexScanOperator>(
        hash_index_, table_heap_, query.where->value);
    root = source.get();
    last_plan_type_ = QueryPlanType::kIndexScan;
  } else {
    FilterOperator::Predicate predicate = BuildPredicate(*query.where);
    source = make_seq_scan();
    filter =
        std::make_unique<FilterOperator>(source.get(), std::move(predicate));
    root = filter.get();
    last_plan_type_ = QueryPlanType::kFilteredSeqScan;
  }

  if (!query.select_all) {
    projection = std::make_unique<ProjectionOperator>(
        root, selection.include_key, selection.include_value);
    root = projection.get();
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

std::vector<Tuple> QueryExecutor::Execute(const InsertQuery &query) {
  ValidateTable(query.table, table_name_);
  if (table_heap_ == nullptr || hash_index_ == nullptr) {
    throw std::invalid_argument(
        "INSERT requiere una tabla fisica y un indice persistente.");
  }

  int existing_rid = 0;
  if (hash_index_->GetValue(query.key, &existing_rid)) {
    throw std::invalid_argument("La clave " + std::to_string(query.key) +
                                " ya existe.");
  }

  const std::optional<RID> rid =
      table_heap_->InsertTuple(query.key, query.value);
  if (!rid.has_value()) {
    throw std::runtime_error(
        "No se pudo insertar el registro en la tabla fisica.");
  }

  try {
    if (!hash_index_->Insert(query.key, rid->Encode())) {
      if (!table_heap_->RollbackInsert(*rid)) {
        throw std::runtime_error(
            "Fallo el indice y no se pudo revertir TableHeap.");
      }
      throw std::runtime_error(
          "No se pudo actualizar el indice; INSERT fue revertido.");
    }
  } catch (...) {
    int indexed_rid = 0;
    if (!hash_index_->GetValue(query.key, &indexed_rid)) {
      table_heap_->RollbackInsert(*rid);
    }
    throw;
  }

  last_plan_type_ = QueryPlanType::kInsert;
  last_output_columns_ = {"key", "value"};
  return {Tuple{query.key, query.value}};
}

std::vector<Tuple> QueryExecutor::Execute(const UpdateQuery &query) {
  ValidateTable(query.table, table_name_);
  if (table_heap_ == nullptr || hash_index_ == nullptr) {
    throw std::invalid_argument(
        "UPDATE requiere una tabla fisica y un indice persistente.");
  }

  const std::string target_column = NormalizeColumn(query.column);
  std::vector<LocatedRecord> matches =
      FindMatchingRecords(table_heap_, hash_index_, query.where);
  std::vector<Tuple> results;
  results.reserve(matches.size());

  if (target_column == "key") {
    if (query.value == TABLE_TOMBSTONE_KEY) {
      throw std::invalid_argument(
          "La clave minima de int esta reservada.");
    }
    if (matches.size() > 1) {
      throw std::invalid_argument(
          "UPDATE de key solo puede afectar una fila para preservar "
          "la unicidad.");
    }
    if (!matches.empty()) {
      const LocatedRecord &record = matches.front();
      if (record.key != query.value) {
        int existing_rid = 0;
        if (hash_index_->GetValue(query.value, &existing_rid)) {
          throw std::invalid_argument(
              "La nueva clave ya existe.");
        }
        if (!hash_index_->Remove(record.key)) {
          throw std::runtime_error(
              "No se encontro la clave anterior en el indice.");
        }
        if (!table_heap_->UpdateRecord(
                record.rid, query.value, record.value)) {
          hash_index_->Insert(record.key, record.rid.Encode());
          throw std::runtime_error(
              "No se pudo actualizar la fila fisica.");
        }
        try {
          if (!hash_index_->Insert(query.value, record.rid.Encode())) {
            throw std::runtime_error(
                "No se pudo insertar la nueva clave en el indice.");
          }
        } catch (...) {
          table_heap_->UpdateRecord(
              record.rid, record.key, record.value);
          hash_index_->Insert(record.key, record.rid.Encode());
          throw;
        }
      }
      results.push_back(Tuple{query.value, record.value});
    }
  } else {
    std::size_t updated_count = 0;
    try {
      for (const LocatedRecord &record : matches) {
        if (!table_heap_->UpdateRecord(
                record.rid, record.key, query.value)) {
          throw std::runtime_error(
              "No se pudo actualizar una fila fisica.");
        }
        ++updated_count;
        results.push_back(Tuple{record.key, query.value});
      }
    } catch (...) {
      for (std::size_t index = 0; index < updated_count; ++index) {
        const LocatedRecord &record = matches[index];
        table_heap_->UpdateRecord(
            record.rid, record.key, record.value);
      }
      throw;
    }
  }

  last_plan_type_ = QueryPlanType::kUpdate;
  last_output_columns_ = {"key", "value"};
  return results;
}

std::vector<Tuple> QueryExecutor::Execute(const DeleteQuery &query) {
  ValidateTable(query.table, table_name_);
  if (table_heap_ == nullptr || hash_index_ == nullptr) {
    throw std::invalid_argument(
        "DELETE requiere una tabla fisica y un indice persistente.");
  }

  const std::vector<LocatedRecord> matches =
      FindMatchingRecords(table_heap_, hash_index_, query.where);
  std::size_t removed_indexes = 0;
  try {
    for (const LocatedRecord &record : matches) {
      if (!hash_index_->Remove(record.key)) {
        throw std::runtime_error(
            "No se encontro una clave de TableHeap en el indice.");
      }
      ++removed_indexes;
    }
  } catch (...) {
    for (std::size_t index = 0; index < removed_indexes; ++index) {
      hash_index_->Insert(
          matches[index].key, matches[index].rid.Encode());
    }
    throw;
  }

  std::size_t deleted_records = 0;
  try {
    for (const LocatedRecord &record : matches) {
      if (!table_heap_->DeleteRecord(record.rid)) {
        throw std::runtime_error(
            "No se pudo borrar una fila fisica.");
      }
      ++deleted_records;
    }
  } catch (...) {
    for (std::size_t index = 0; index < deleted_records; ++index) {
      const LocatedRecord &record = matches[index];
      table_heap_->RestoreRecord(
          record.rid, record.key, record.value);
    }
    for (const LocatedRecord &record : matches) {
      hash_index_->Insert(record.key, record.rid.Encode());
    }
    throw;
  }

  std::vector<Tuple> results;
  results.reserve(matches.size());
  for (const LocatedRecord &record : matches) {
    results.push_back(Tuple{record.key, record.value});
  }
  last_plan_type_ = QueryPlanType::kDelete;
  last_output_columns_ = {"key", "value"};
  return results;
}

QueryPlanType QueryExecutor::GetLastPlanType() const {
  return last_plan_type_;
}

const std::vector<std::string> &QueryExecutor::GetLastOutputColumns() const {
  return last_output_columns_;
}

}  // namespace minisgbd
