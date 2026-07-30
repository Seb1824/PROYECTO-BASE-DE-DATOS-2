#include "query/query_executor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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

namespace minisgbd {
namespace {

const std::vector<std::string> &AllColumns() {
  static const std::vector<std::string> columns{
      "id", "nombre", "ciudad", "profesion"};
  return columns;
}

std::string ToLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return text;
}

std::string NormalizeColumn(const std::string &column) {
  const std::string normalized = ToLower(column);
  if (normalized == "id" || normalized == "nombre" ||
      normalized == "ciudad" || normalized == "profesion") {
    return normalized;
  }
  throw std::invalid_argument("Columna desconocida: " + column);
}

bool IsIdColumn(const std::string &column) {
  return NormalizeColumn(column) == "id";
}

template <typename Value>
bool CompareValues(const Value &actual, ComparisonOperator comparison,
                   const Value &expected) {
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

std::string ComparisonText(ComparisonOperator comparison) {
  switch (comparison) {
    case ComparisonOperator::kEqual:
      return "=";
    case ComparisonOperator::kNotEqual:
      return "!=";
    case ComparisonOperator::kLess:
      return "<";
    case ComparisonOperator::kLessOrEqual:
      return "<=";
    case ComparisonOperator::kGreater:
      return ">";
    case ComparisonOperator::kGreaterOrEqual:
      return ">=";
  }
  return "?";
}

std::string ValueText(const ScalarValue &value) {
  if (std::holds_alternative<int>(value)) {
    return std::to_string(std::get<int>(value));
  }

  std::string escaped;
  for (const char character : std::get<std::string>(value)) {
    escaped.push_back(character);
    if (character == '\'') {
      escaped.push_back('\'');
    }
  }
  return "'" + escaped + "'";
}

std::string ConditionText(const Condition &condition) {
  return NormalizeColumn(condition.column) + " " +
         ComparisonText(condition.comparison) + " " +
         ValueText(condition.value);
}

std::string JoinColumns(const std::vector<std::string> &columns) {
  std::string joined;
  for (std::size_t index = 0; index < columns.size(); ++index) {
    if (index != 0) {
      joined += ", ";
    }
    joined += columns[index];
  }
  return joined;
}

int RequireInteger(const ScalarValue &value,
                   const std::string &column) {
  if (!std::holds_alternative<int>(value)) {
    throw std::invalid_argument(
        "La columna " + column + " requiere un valor entero.");
  }
  return std::get<int>(value);
}

const std::string &RequireText(const ScalarValue &value,
                               const std::string &column) {
  if (!std::holds_alternative<std::string>(value)) {
    throw std::invalid_argument(
        "La columna " + column +
        " requiere una cadena entre comillas simples.");
  }
  return std::get<std::string>(value);
}

FilterOperator::Predicate BuildPredicate(const Condition &condition) {
  const std::string column = NormalizeColumn(condition.column);
  const ComparisonOperator comparison = condition.comparison;

  if (column == "id") {
    const int expected = RequireInteger(condition.value, column);
    return [comparison, expected](const Tuple &tuple) {
      return CompareValues(tuple.id, comparison, expected);
    };
  }

  const std::string expected = RequireText(condition.value, column);
  if (column == "nombre") {
    return [comparison, expected](const Tuple &tuple) {
      return CompareValues(tuple.nombre, comparison, expected);
    };
  }
  if (column == "ciudad") {
    return [comparison, expected](const Tuple &tuple) {
      return CompareValues(tuple.ciudad, comparison, expected);
    };
  }
  return [comparison, expected](const Tuple &tuple) {
    return CompareValues(tuple.profesion, comparison, expected);
  };
}

std::vector<std::string> BuildProjection(const SelectQuery &query) {
  if (query.select_all) {
    return AllColumns();
  }
  if (query.columns.empty()) {
    throw std::invalid_argument(
        "SELECT requiere al menos una columna.");
  }

  std::vector<std::string> columns;
  std::unordered_set<std::string> seen;
  for (const std::string &column : query.columns) {
    const std::string normalized = NormalizeColumn(column);
    if (!seen.insert(normalized).second) {
      throw std::invalid_argument(
          "SELECT contiene una columna duplicada: " + column);
    }
    columns.push_back(normalized);
  }
  return columns;
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
  if (hash_index != nullptr && IsIdColumn(where->column) &&
      where->comparison == ComparisonOperator::kEqual) {
    const int id = RequireInteger(where->value, "id");
    int encoded_rid = 0;
    if (!hash_index->GetValue(id, &encoded_rid)) {
      return {};
    }

    const RID rid = RID::Decode(encoded_rid);
    PersonRecord person;
    if (!table_heap->GetRecord(rid, &person) || person.id != id) {
      throw std::runtime_error(
          "El indice contiene un RID inconsistente.");
    }
    if (predicate(person)) {
      return {LocatedRecord{rid, person}};
    }
    return {};
  }

  std::vector<LocatedRecord> matches;
  for (const LocatedRecord &record : table_heap->ReadAll()) {
    if (predicate(record.person)) {
      matches.push_back(record);
    }
  }
  return matches;
}

PersonRecord BuildInsertedPerson(const InsertQuery &query) {
  PersonRecord person{
      query.id, query.nombre, query.ciudad, query.profesion};
  ValidatePersonRecord(person);
  return person;
}

void AssignTextColumn(PersonRecord *person, const std::string &column,
                      const std::string &value) {
  if (column == "nombre") {
    person->nombre = value;
  } else if (column == "ciudad") {
    person->ciudad = value;
  } else {
    person->profesion = value;
  }
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
  const std::vector<std::string> selection = BuildProjection(query);
  last_output_columns_ = selection;

  std::unique_ptr<Operator> source;
  std::unique_ptr<FilterOperator> filter;
  std::unique_ptr<ProjectionOperator> projection;
  Operator *root = nullptr;
  std::string source_name;
  std::string source_detail;
  std::string filter_detail;

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
    source_name = "SeqScan";
    source_detail = "tabla=" + table_name_ +
                    (table_heap_ != nullptr ? "; origen=TableHeap"
                                            : "; origen=memoria");
  } else if (table_heap_ != nullptr && hash_index_ != nullptr &&
             IsIdColumn(query.where->column) &&
             query.where->comparison == ComparisonOperator::kEqual) {
    const int id = RequireInteger(query.where->value, "id");
    source = std::make_unique<IndexScanOperator>(
        hash_index_, table_heap_, id);
    root = source.get();
    last_plan_type_ = QueryPlanType::kIndexScan;
    source_name = "IndexScan";
    source_detail =
        "indice=hash extensible; id = " + std::to_string(id);
  } else {
    FilterOperator::Predicate predicate = BuildPredicate(*query.where);
    source = make_seq_scan();
    filter =
        std::make_unique<FilterOperator>(source.get(), std::move(predicate));
    root = filter.get();
    last_plan_type_ = QueryPlanType::kFilteredSeqScan;
    source_name = "SeqScan";
    source_detail = "tabla=" + table_name_ +
                    (table_heap_ != nullptr ? "; origen=TableHeap"
                                            : "; origen=memoria");
    filter_detail = ConditionText(*query.where);
  }

  if (!query.select_all) {
    projection =
        std::make_unique<ProjectionOperator>(root, selection);
    root = projection.get();
  }

  if (tracer_ != nullptr) {
    int parent_id = -1;
    if (projection != nullptr) {
      const int projection_id = tracer_->RegisterOperator(
          "Projection", "columnas=" + JoinColumns(selection), parent_id);
      projection->AttachTrace(tracer_, projection_id);
      parent_id = projection_id;
    }
    if (filter != nullptr) {
      const int filter_id =
          tracer_->RegisterOperator("Filter", filter_detail, parent_id);
      filter->AttachTrace(tracer_, filter_id);
      parent_id = filter_id;
    }
    const int source_id =
        tracer_->RegisterOperator(source_name, source_detail, parent_id);
    source->AttachTrace(tracer_, source_id);
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
  const PersonRecord person = BuildInsertedPerson(query);
  const auto trace_start = ExecutionTracer::Clock::now();
  const int trace_operator_id =
      tracer_ != nullptr
          ? tracer_->RegisterOperator(
                "Insert", "tabla=" + table_name_ + "; id=" +
                              std::to_string(person.id))
          : -1;

  if (table_heap_ == nullptr || hash_index_ == nullptr) {
    throw std::invalid_argument(
        "INSERT requiere una tabla fisica y un indice persistente.");
  }

  int existing_rid = 0;
  if (hash_index_->GetValue(person.id, &existing_rid)) {
    throw std::invalid_argument(
        "El id " + std::to_string(person.id) + " ya existe.");
  }

  const std::optional<RID> rid = table_heap_->InsertTuple(person);
  if (!rid.has_value()) {
    throw std::runtime_error(
        "No se pudo insertar el registro en la tabla fisica.");
  }

  try {
    if (!hash_index_->Insert(person.id, rid->Encode())) {
      if (!table_heap_->RollbackInsert(*rid)) {
        throw std::runtime_error(
            "Fallo el indice y no se pudo revertir TableHeap.");
      }
      throw std::runtime_error(
          "No se pudo actualizar el indice; INSERT fue revertido.");
    }
  } catch (...) {
    int indexed_rid = 0;
    if (!hash_index_->GetValue(person.id, &indexed_rid)) {
      table_heap_->RollbackInsert(*rid);
    }
    throw;
  }

  last_plan_type_ = QueryPlanType::kInsert;
  last_output_columns_ = AllColumns();
  if (tracer_ != nullptr) {
    tracer_->RecordOperatorEvent(
        trace_operator_id, "Execute", trace_start,
        ExecutionTracer::Clock::now(), 1);
  }
  return {person};
}

std::vector<Tuple> QueryExecutor::Execute(const UpdateQuery &query) {
  ValidateTable(query.table, table_name_);
  const std::string target_column = NormalizeColumn(query.column);
  const auto trace_start = ExecutionTracer::Clock::now();
  const int trace_operator_id =
      tracer_ != nullptr
          ? tracer_->RegisterOperator(
                "Update", "tabla=" + table_name_ + "; columna=" +
                              target_column)
          : -1;

  if (table_heap_ == nullptr || hash_index_ == nullptr) {
    throw std::invalid_argument(
        "UPDATE requiere una tabla fisica y un indice persistente.");
  }

  std::vector<LocatedRecord> matches =
      FindMatchingRecords(table_heap_, hash_index_, query.where);
  std::vector<Tuple> results;
  results.reserve(matches.size());

  if (target_column == "id") {
    const int new_id = RequireInteger(query.value, target_column);
    if (matches.size() > 1) {
      throw std::invalid_argument(
          "UPDATE de id solo puede afectar una fila para preservar "
          "la unicidad.");
    }

    if (!matches.empty()) {
      const LocatedRecord &record = matches.front();
      PersonRecord updated = record.person;
      updated.id = new_id;

      if (record.person.id != new_id) {
        int existing_rid = 0;
        if (hash_index_->GetValue(new_id, &existing_rid)) {
          throw std::invalid_argument("El nuevo id ya existe.");
        }
        if (!hash_index_->Remove(record.person.id)) {
          throw std::runtime_error(
              "No se encontro el id anterior en el indice.");
        }
        if (!table_heap_->UpdateRecord(record.rid, updated)) {
          hash_index_->Insert(record.person.id, record.rid.Encode());
          throw std::runtime_error(
              "No se pudo actualizar la fila fisica.");
        }
        try {
          if (!hash_index_->Insert(new_id, record.rid.Encode())) {
            throw std::runtime_error(
                "No se pudo insertar el nuevo id en el indice.");
          }
        } catch (...) {
          table_heap_->UpdateRecord(record.rid, record.person);
          hash_index_->Insert(record.person.id, record.rid.Encode());
          throw;
        }
      }
      results.push_back(std::move(updated));
    }
  } else {
    const std::string new_value =
        RequireText(query.value, target_column);
    std::vector<PersonRecord> updated_rows;
    updated_rows.reserve(matches.size());
    for (const LocatedRecord &record : matches) {
      PersonRecord updated = record.person;
      AssignTextColumn(&updated, target_column, new_value);
      ValidatePersonRecord(updated);
      updated_rows.push_back(std::move(updated));
    }

    std::size_t updated_count = 0;
    try {
      for (std::size_t index = 0; index < matches.size(); ++index) {
        if (!table_heap_->UpdateRecord(
                matches[index].rid, updated_rows[index])) {
          throw std::runtime_error(
              "No se pudo actualizar una fila fisica.");
        }
        ++updated_count;
        results.push_back(updated_rows[index]);
      }
    } catch (...) {
      for (std::size_t index = 0; index < updated_count; ++index) {
        table_heap_->UpdateRecord(
            matches[index].rid, matches[index].person);
      }
      throw;
    }
  }

  last_plan_type_ = QueryPlanType::kUpdate;
  last_output_columns_ = AllColumns();
  if (tracer_ != nullptr) {
    tracer_->RecordOperatorEvent(
        trace_operator_id, "Execute", trace_start,
        ExecutionTracer::Clock::now(), results.size());
  }
  return results;
}

std::vector<Tuple> QueryExecutor::Execute(const DeleteQuery &query) {
  ValidateTable(query.table, table_name_);
  const auto trace_start = ExecutionTracer::Clock::now();
  const int trace_operator_id =
      tracer_ != nullptr
          ? tracer_->RegisterOperator("Delete", "tabla=" + table_name_)
          : -1;

  if (table_heap_ == nullptr || hash_index_ == nullptr) {
    throw std::invalid_argument(
        "DELETE requiere una tabla fisica y un indice persistente.");
  }

  const std::vector<LocatedRecord> matches =
      FindMatchingRecords(table_heap_, hash_index_, query.where);
  std::size_t removed_indexes = 0;
  try {
    for (const LocatedRecord &record : matches) {
      if (!hash_index_->Remove(record.person.id)) {
        throw std::runtime_error(
            "No se encontro un id de TableHeap en el indice.");
      }
      ++removed_indexes;
    }
  } catch (...) {
    for (std::size_t index = 0; index < removed_indexes; ++index) {
      hash_index_->Insert(
          matches[index].person.id, matches[index].rid.Encode());
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
      table_heap_->RestoreRecord(record.rid, record.person);
    }
    for (const LocatedRecord &record : matches) {
      hash_index_->Insert(record.person.id, record.rid.Encode());
    }
    throw;
  }

  std::vector<Tuple> results;
  results.reserve(matches.size());
  for (const LocatedRecord &record : matches) {
    results.push_back(record.person);
  }
  last_plan_type_ = QueryPlanType::kDelete;
  last_output_columns_ = AllColumns();
  if (tracer_ != nullptr) {
    tracer_->RecordOperatorEvent(
        trace_operator_id, "Execute", trace_start,
        ExecutionTracer::Clock::now(), results.size());
  }
  return results;
}

QueryPlanType QueryExecutor::GetLastPlanType() const {
  return last_plan_type_;
}

const std::vector<std::string> &
QueryExecutor::GetLastOutputColumns() const {
  return last_output_columns_;
}

void QueryExecutor::SetExecutionTracer(ExecutionTracer *tracer) {
  tracer_ = tracer;
}

}  // namespace minisgbd
