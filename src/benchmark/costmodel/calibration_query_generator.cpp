#include "calibration_query_generator.hpp"

#include <algorithm>
#include <boost/algorithm/string/join.hpp>
#include <boost/format.hpp>
#include <experimental/iterator>
#include <iostream>
#include <random>

namespace opossum {

    const std::vector<std::string> CalibrationQueryGenerator::generate_queries(const nlohmann::json& table_definitions) {

      std::vector<std::string> queries;

      for (const auto & table_definition : table_definitions) {
        std::cout << "Generating queries for " << table_definition["table_name"] << std::endl;

        queries.push_back(CalibrationQueryGenerator::_generate_select_star(table_definition));
        queries.push_back(CalibrationQueryGenerator::_generate_table_scan(table_definition));
      }

      //    "SELECT column_a FROM SomeTable;",
////            "SELECT column_b FROM SomeTable;",
////            "SELECT column_c FROM SomeTable;",
////            "SELECT column_a, column_b, column_c FROM SomeTable;",
////            "SELECT * FROM SomeTable;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a = 753;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a = 345;",
//            "SELECT column_a, column_b, column_c, column_d FROM SomeTable WHERE column_d = 4;",
//            "SELECT column_a, column_b, column_c, column_d FROM SomeTable WHERE column_d = 7;",
//            "SELECT column_a, column_b, column_c, column_d FROM SomeTable WHERE column_d = 9;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a < 200;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a < 600;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a < 900;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a < 900 AND column_d = 4;",
//            "SELECT column_a, column_b, column_c FROM SomeTable WHERE column_a < 900 AND column_b < 'Bradley Davis';",
//            "SELECT column_b FROM SomeTable WHERE column_b < 'Bradley Davis';",
//            "SELECT column_a FROM SomeSecondTable WHERE column_b = 4"
////            "SELECT COUNT(*) FROM SomeTable"

      return queries;
    }

    const std::string CalibrationQueryGenerator::_generate_select_star(const nlohmann::json& table_definition) {
      auto string_template = "SELECT %1% FROM %2%;";

      auto columns = _generate_select_columns(table_definition["columns"]);

      return boost::str(boost::format(string_template) % columns % table_definition["table_name"].get<std::string>());
    }

    const std::string CalibrationQueryGenerator::_generate_table_scan(const nlohmann::json& table_definition) {
      auto string_template = "SELECT %1% FROM %2% WHERE %3% %4% %5%;";

      auto select_columns = _generate_select_columns(table_definition["columns"]);
      auto table_name = table_definition["table_name"].get<std::string>();

//      auto columns_definitions = table_definition["columns"];
//      std::random_device random_device;
//      std::mt19937 engine{random_device()};
//      std::uniform_int_distribution<long> dist(0, columns_definitions.size() - 1);
//      auto filter_column = columns_definitions[dist(engine)];
//
//      auto filter_value = "10";

//      std::string separator;
//      if (filter_column["type"] == "string") {
//        separator = "<";
//      } else {
//        separator = "=";
//      }

      return boost::str(boost::format(string_template) % select_columns % table_name % "column_a" % "=" % "10");
    }

    const std::vector<std::string> CalibrationQueryGenerator::_get_column_names(const nlohmann::json& column_definitions) {
      std::vector<std::string> column_names;

      for (auto it = column_definitions.begin(); it != column_definitions.end(); ++it) {
        column_names.push_back(it.key());
      }

      return column_names;
    }

    const std::string CalibrationQueryGenerator::_generate_select_columns(const nlohmann::json& column_definitions) {
      auto column_names = CalibrationQueryGenerator::_get_column_names(column_definitions);
      std::vector<std::string> out;

      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(column_names.begin(), column_names.end(), g);

      std::random_device random_device;
      std::mt19937 engine{random_device()};
      std::uniform_int_distribution<long> dist(1, column_names.size() - 1);

      long num = dist(engine);
      std::sample(column_names.begin(), column_names.end(), std::back_inserter(out), num, std::mt19937{std::random_device{}()});


      return boost::algorithm::join(out, ", ");
    }
}