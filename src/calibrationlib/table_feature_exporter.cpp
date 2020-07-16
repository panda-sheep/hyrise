#include "table_feature_exporter.hpp"

#include <fstream>
#include <string>

#include "constant_mappings.hpp"
#include "import_export/csv/csv_writer.hpp"
#include "storage/abstract_encoded_segment.hpp"
#include "storage/vector_compression/compressed_vector_type.hpp"

namespace opossum {
TableFeatureExporter::TableFeatureExporter(const std::string& path_to_dir) : _path_to_dir(path_to_dir) {}

void TableFeatureExporter::export_table(std::shared_ptr<const CalibrationTableWrapper> table_wrapper) {
  _export_table_data(table_wrapper);
  _export_column_data(table_wrapper);
  _export_segment_data(table_wrapper);
}

void TableFeatureExporter::flush() {
  for (const auto& [export_type, table] : _tables) {
    const auto path = _path_to_dir + "/" + _table_names.at(export_type) + ".csv";
    CsvWriter::write(*table, path);
  }
}

void TableFeatureExporter::_export_table_data(std::shared_ptr<const CalibrationTableWrapper> table_wrapper) {
  const auto table_name = pmr_string{table_wrapper->get_name()};
  const auto row_count = static_cast<int64_t>(table_wrapper->get_table()->row_count());
  const auto chunk_size = static_cast<int32_t>(table_wrapper->get_table()->target_chunk_size());

  _tables.at(TableFeatureExportType::TABLE)->append({table_name, row_count, chunk_size});
}

void TableFeatureExporter::_export_column_data(std::shared_ptr<const CalibrationTableWrapper> table_wrapper) {
  auto const table = table_wrapper->get_table();
  int column_count = table->column_count();
  const auto column_names = table->column_names();

  for (ColumnID column_id = ColumnID{0}; column_id < column_count; ++column_id) {
    const auto table_name = pmr_string{table_wrapper->get_name()};
    const auto column_name = pmr_string{table->column_name(column_id)};
    const auto column_data_type = pmr_string{data_type_to_string.left.at(table->column_data_type(column_id))};
    bool sorted_ascending = true;
    bool sorted_descending = true;

    for (auto chunk_id = ChunkID{0}; chunk_id < table->chunk_count(); ++chunk_id) {
      const auto chunk = table->get_chunk(chunk_id);
      const auto& sort_definitions = chunk->sorted_by();
      if (sort_definitions.empty()) {
        sorted_ascending = false;
        sorted_descending = false;
        break;
      }

      bool chunk_sorted_ascending = false;
      bool chunk_sorted_descending = false;
      for (const auto& sort_definition : sort_definitions) {
        if (sort_definition.column == column_id) {
          if (sort_definition.sort_mode == SortMode::Ascending) {
            chunk_sorted_ascending = true;
          } else
            chunk_sorted_descending = true;
        }
      }
      sorted_ascending &= chunk_sorted_ascending;
      sorted_descending &= chunk_sorted_descending;
    }

    _tables.at(TableFeatureExportType::COLUMN)
        ->append({table_name, column_name, column_data_type, static_cast<int32_t>(sorted_ascending),
                  static_cast<int32_t>(sorted_descending)});
  }
}

void TableFeatureExporter::_export_segment_data(std::shared_ptr<const CalibrationTableWrapper> table_wrapper) {
  const auto table = table_wrapper->get_table();
  const auto table_name = pmr_string{table_wrapper->get_name()};
  const auto chunk_count = table->chunk_count();
  const auto column_count = table->column_count();

  for (ColumnID column_id{0}; column_id < column_count; ++column_id) {
    const auto column_name = pmr_string{table->column_name(column_id)};

    for (ChunkID chunk_id{0}; chunk_id < chunk_count; ++chunk_id) {
      auto const segment = table->get_chunk(chunk_id)->get_segment(column_id);
      AllTypeVariant encoding_type = {pmr_string{encoding_type_to_string.left.at(EncodingType::Unencoded)}};
      AllTypeVariant compression_type = NULL_VALUE;
      // if segment is encoded write out values
      if (const auto encoded_segment = std::dynamic_pointer_cast<AbstractEncodedSegment>(segment)) {
        // Encoding Type
        encoding_type = AllTypeVariant{pmr_string{encoding_type_to_string.left.at(encoded_segment->encoding_type())}};

        // Compressed Vector Type
        if (const auto compressed_vector_type = encoded_segment->compressed_vector_type()) {
          std::stringstream ss;
          ss << *compressed_vector_type;
          compression_type = pmr_string{ss.str()};
        }
      }
      _tables.at(TableFeatureExportType::SEGMENT)
          ->append({table_name, column_name, static_cast<int32_t>(chunk_id), encoding_type, compression_type});
    }
  }
}
}  // namespace opossum
