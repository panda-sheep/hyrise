#pragma once

#include <string>
#include <unordered_map>
#include <utility>

#include <boost/algorithm/string.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>

#include "operators/print.hpp"

namespace opossum {

// All graphviz options, e.g. color, shape, format, can be looked up at
// http://www.graphviz.org/doc/info/attrs.html
// We do not want to create constants here because they would be rather restrictive compared to all possible options
// defined by graphviz.
struct GraphvizConfig {
  std::string renderer = "dot";
  std::string format = "png";
};

struct VizGraphInfo {
  std::string bg_color = "black";
  std::string font_color = "white";
  std::string rankdir = "BT";
  std::string ratio = "compress";
  std::string label = "For tooltips with advanced information, hover your mouse over vertices or edge labels.\n\n\n";
  std::string label_location = "t";
  std::string label_justification = "l";
};

struct VizVertexInfo {
  uintptr_t id;
  std::string label;
  std::string tooltip;
  std::string color = "white";
  std::string font_color = "white";
  std::string shape = "rectangle";
  double pen_width = 1.0;
};

struct VizEdgeInfo {
  std::string label;
  std::string label_tooltip;
  std::string color = "white";
  std::string font_color = "white";
  double pen_width = 1.0;
  std::string dir = "forward";
  std::string style = "solid";
  std::string arrowhead = "normal";
};

// Custom facet for creating a custom locale with thousands separator.
struct SeparateThousandsFacet : std::numpunct<char> {
  string_type do_grouping() const override { return "\3"; }  // groups of 3 digits
};

template <typename GraphBase>
class AbstractVisualizer {
  //                                  Edge list    Vertex list   Directed graph
  using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS,
                                      // Vertex info Edge info    Graph info
                                      VizVertexInfo, VizEdgeInfo, VizGraphInfo>;

  // No label in a node should be wider than this many characters. If it is longer, line breaks should be added.
  static const uint8_t MAX_LABEL_WIDTH = 50;

 public:
  enum class InputSide { Left, Right };

  AbstractVisualizer() : AbstractVisualizer(GraphvizConfig{}, VizGraphInfo{}, VizVertexInfo{}, VizEdgeInfo{}) {}

  AbstractVisualizer(GraphvizConfig graphviz_config, VizGraphInfo graph_info, VizVertexInfo vertex_info,
                     VizEdgeInfo edge_info)
      : _graphviz_config(std::move(graphviz_config)),
        _graph_info(std::move(graph_info)),
        _default_vertex(std::move(vertex_info)),
        _default_edge(std::move(edge_info)) {
    // Add global Graph properties
    _add_graph_property("rankdir", _graph_info.rankdir);
    _add_graph_property("fontcolor", _graph_info.font_color);
    _add_graph_property("bgcolor", _graph_info.bg_color);
    _add_graph_property("ratio", _graph_info.ratio);
    _add_graph_property("label", _graph_info.label);
    _add_graph_property("labelloc", _graph_info.label_location);
    _add_graph_property("labeljust", _graph_info.label_justification);

    // Add vertex properties
    _add_property("node_id", &VizVertexInfo::id);
    _add_property("color", &VizVertexInfo::color);
    _add_property("label", &VizVertexInfo::label);
    _add_property("shape", &VizVertexInfo::shape);
    _add_property("fontcolor", &VizVertexInfo::font_color);
    _add_property("penwidth", &VizVertexInfo::pen_width);
    _add_property("tooltip", &VizVertexInfo::tooltip);

    // Add edge properties
    _add_property("color", &VizEdgeInfo::color);
    _add_property("fontcolor", &VizEdgeInfo::font_color);
    _add_property("label", &VizEdgeInfo::label);
    _add_property("penwidth", &VizEdgeInfo::pen_width);
    _add_property("style", &VizEdgeInfo::style);
    _add_property("dir", &VizEdgeInfo::dir);
    _add_property("arrowhead", &VizEdgeInfo::arrowhead);
    _add_property("labeltooltip", &VizEdgeInfo::label_tooltip);
  }

  virtual ~AbstractVisualizer() = default;

  void visualize(const GraphBase& graph_base, const std::string& img_filename) {
    _build_graph(graph_base);

    char* tmpname = strdup("/tmp/hyrise_viz_XXXXXX");
    auto file_descriptor = mkstemp(tmpname);
    Assert(file_descriptor > 0, "mkstemp failed");

    // mkstemp returns a file descriptor. Unfortunately, we cannot directly create an ofstream from a file descriptor.
    close(file_descriptor);
    std::ofstream file(tmpname);

    // This unique_ptr serves as a scope guard that guarantees the deletion of the temp file once we return from this
    // method.
    const auto delete_temp_file = [&tmpname](auto ptr) {
      delete ptr;
      std::remove(tmpname);
    };
    const auto delete_guard = std::unique_ptr<char, decltype(delete_temp_file)>(new char, delete_temp_file);

    // The caller set the pen widths to either the number of rows (for edges) or the execution time in ns (for
    // vertices). As some plans have only operators that take microseconds and others take minutes, normalize this
    // so that the thickest pen has a width of max_normalized_width and the thinnest one has a width of 1. Using
    // a logarithm makes the operators that follow the most expensive one more visible. Not sure if this is what
    // statisticians would do, but it makes for beautiful images.
    const auto normalize_penwidths = [&](auto iter_pair) {
      const auto max_normalized_width = 8.0;
      const auto log_base = std::log(1.5);
      double max_unnormalized_width = 0.0;
// False positive with gcc and tsan (https://gcc.gnu.org/bugzilla/show_bug.cgi?id=92194)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
      for (auto iter = iter_pair.first; iter != iter_pair.second; ++iter) {
        max_unnormalized_width = std::max(max_unnormalized_width, std::log(_graph[*iter].pen_width) / log_base);
      }

      double offset = max_unnormalized_width - (max_normalized_width - 1.0);

      for (auto iter = iter_pair.first; iter != iter_pair.second; ++iter) {
        auto& pen_width = _graph[*iter].pen_width;
        if (max_unnormalized_width == 0.0) {
          // All widths are the same, set pen width to 1
          pen_width = 1.0;
        } else {
          // Set normalized pen width
          pen_width = 1.0 + std::max(0.0, std::log(pen_width) / log_base - offset);
        }
      }
#pragma GCC diagnostic pop
    };
    normalize_penwidths(boost::vertices(_graph));
    normalize_penwidths(boost::edges(_graph));

    boost::write_graphviz_dp(file, _graph, _properties);

    auto renderer = _graphviz_config.renderer;
    auto format = _graphviz_config.format;

    auto cmd = renderer + " -T" + format + " \"" + tmpname + "\" > \"" + img_filename + "\"";
    auto ret = system(cmd.c_str());

    Assert(ret == 0, "Calling graphviz' " + renderer +
                         " failed. Have you installed graphviz "
                         "(apt-get install graphviz / brew install graphviz)?");
    // We do not want to make graphviz a requirement for Hyrise as visualization is just a gimmick
  }

 protected:
  virtual void _build_graph(const GraphBase& graph_base) = 0;

  template <typename T>
  static uintptr_t _get_id(const T& v) {
    return reinterpret_cast<uintptr_t>(&v);
  }

  template <typename T>
  static uintptr_t _get_id(const std::shared_ptr<T>& v) {
    return reinterpret_cast<uintptr_t>(v.get());
  }

  enum class WrapLabel { On, Off };

  template <typename T>
  void _add_vertex(const T& vertex, const std::string& label = "", const WrapLabel wrap_label = WrapLabel::On) {
    VizVertexInfo info = _default_vertex;
    info.id = _get_id(vertex);
    info.label = label;
    _add_vertex(vertex, info, wrap_label);
  }

  template <typename T>
  void _add_vertex(const T& vertex, VizVertexInfo& vertex_info, const WrapLabel wrap_label = WrapLabel::On) {
    auto vertex_id = _get_id(vertex);
    auto inserted = _id_to_position.insert({vertex_id, _id_to_position.size()}).second;
    if (!inserted) {
      // Vertex already exists, do nothing
      return;
    }

    vertex_info.id = vertex_id;
    if (wrap_label == WrapLabel::On) {
      vertex_info.label = _wrap_label(vertex_info.label);
    }
    boost::add_vertex(vertex_info, _graph);
  }

  template <typename T, typename K>
  void _add_edge(const T& from, const K& to) {
    _add_edge(from, to, _default_edge);
  }

  template <typename T, typename K>
  void _add_edge(const T& from, const K& to, const VizEdgeInfo& edge_info) {
    auto from_id = _get_id(from);
    auto to_id = _get_id(to);

    auto from_pos = _id_to_position.at(from_id);
    auto to_pos = _id_to_position.at(to_id);

    boost::add_edge(from_pos, to_pos, edge_info, _graph);
  }

  template <typename T>
  void _add_graph_property(const std::string& property_name, const T& value) {
    // Use this to add a global property to the graph. This results in a config line in the graph file:
    // property_name=value;
    _properties.property(property_name, boost::make_constant_property<Graph*>(value));
  }

  template <typename T>
  void _add_property(const std::string& property_name, const T& value) {
    // Use this to add a property that is read from each vertex/edge (depending on the value). This will result in:
    // <node_id> [..., property_name=value, ...];
    _properties.property(property_name, boost::get(value, _graph));
  }

  std::string _wrap_label(const std::string& label) {
    if (label.length() <= MAX_LABEL_WIDTH) {
      return label;
    }
    std::stringstream label_stream;

    // 1. Split label into lines
    auto lines = std::vector<std::string>();
    boost::split(lines, label, boost::is_any_of("\n"));
    const auto line_count = lines.size();
    for (auto line_idx = size_t{0}; line_idx < line_count; ++line_idx) {
      if (line_idx > 0) {
        label_stream << '\n';
      }
      const auto& line = lines[line_idx];
      if (line.length() <= MAX_LABEL_WIDTH) {
        label_stream << line;
        continue;
      }
      // 2. Split line into words, so we don't break a line in the middle of a word
      auto line_words = std::vector<std::string>();
      boost::split(line_words, line, boost::is_any_of(" "));
      auto line_length = size_t{0};
      auto word_idx = size_t{0};
      while (true) {
        label_stream << line_words.at(word_idx);
        line_length += line_words.at(word_idx).length();

        // Exit on last word
        if (word_idx == line_words.size() - 1) {
          break;
        }

        line_length++;  // include whitespace
        word_idx++;
        auto next_line_length = line_length + line_words.at(word_idx).length();
        if (next_line_length <= MAX_LABEL_WIDTH) {
          label_stream << ' ';
        } else {
          label_stream << '\n';
          line_length = 0;
        }
      }
    }

    return label_stream.str();
  }

  std::string _random_color() {
    // Favor a hand picked list of nice-to-look-at colors over random generation for now.
    static std::vector<std::string> colors(
        {"#008A2A", "#005FAF", "#5F7E7E", "#9C2F2F", "#A0666C", "#9F9F00", "#9FC0CB", "#9F4C00", "#AF00AF"});

    _random_color_index = (_random_color_index + 1) % colors.size();
    return colors[_random_color_index];
  }

  Graph _graph;
  std::unordered_map<uintptr_t, uint16_t> _id_to_position;
  boost::dynamic_properties _properties;

  GraphvizConfig _graphviz_config;
  VizGraphInfo _graph_info;
  VizVertexInfo _default_vertex;
  VizEdgeInfo _default_edge;

  // Current index of color in _random_color()
  size_t _random_color_index{0};
};

}  // namespace opossum
