#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "resolve_type.hpp"
#include "scheduler/current_scheduler.hpp"
#include "scheduler/job_task.hpp"
#include "storage/create_iterable_from_segment.hpp"
#include "storage/dictionary_segment.hpp"
#include "storage/segment_iterate.hpp"
#include "storage/vector_compression/resolve_compressed_vector_type.hpp"
#include "types.hpp"
#include "uninitialized_vector.hpp"

namespace opossum {

template <typename T>
struct MaterializedValue {
  MaterializedValue() = default;
  MaterializedValue(const RowID row, const T v) : row_id{row}, value{v} {}

  RowID row_id;
  T value;
};

// Initializing the materialized segments takes some time. This is not necessary, because
// it will be overwritten anyway. The uninitialized_vector behaves like a regular std::vector,
// but the entries are initially invalid. It works only with trivial types though.
template <typename T>
using MaterializedSegment = std::conditional_t<std::is_trivial_v<T>, uninitialized_vector<MaterializedValue<T>>,
                                               std::vector<MaterializedValue<T>>>;

template <typename T>
using MaterializedSegmentList = std::vector<std::shared_ptr<MaterializedSegment<T>>>;

/**
 * This data structure is passed as a reference to the jobs which materialize
 * the chunks. Each job then adds `samples_to_collect` samples to its passed
 * SampleRequest. All SampleRequests are later merged to gather a global sample
 * list with which the split values for the radix partitioning are determined.
 */
template <typename T>
struct Subsample {
  explicit Subsample(ChunkOffset sample_count) : samples_to_collect(sample_count), samples() {}
  const ChunkOffset samples_to_collect;
  std::vector<T> samples;
};

/**
 * Materializes a table for a specific segment and sorts it if required. Result is a triple of
 * materialized values, positions of NULL values, and a list of samples.
 **/
template <typename T>
class ColumnMaterializer {
 public:
  explicit ColumnMaterializer(bool sort, bool materialize_null) : _sort{sort}, _materialize_null{materialize_null} {}

 public:
  /**
   * Materializes and sorts all the chunks of an input table in parallel
   * by creating multiple jobs that materialize chunks.
   * Returns the materialized segments and a list of null row ids if materialize_null is enabled.
   **/
  std::tuple<std::unique_ptr<MaterializedSegmentList<T>>, std::unique_ptr<PosList>, std::vector<T>> materialize(
      const std::shared_ptr<const Table> input, const ColumnID column_id) {
    const ChunkOffset samples_per_chunk = 10;  // rather arbitrarily chosen number
    const auto chunk_count = input->chunk_count();

    auto output = std::make_unique<MaterializedSegmentList<T>>(chunk_count);
    auto null_rows = std::make_unique<PosList>();

    std::vector<Subsample<T>> subsamples;
    subsamples.reserve(chunk_count);

    std::vector<std::shared_ptr<AbstractTask>> jobs;
    jobs.reserve(chunk_count);
    for (ChunkID chunk_id{0}; chunk_id < chunk_count; ++chunk_id) {
      const auto samples_to_write = std::min(samples_per_chunk, input->get_chunk(chunk_id)->size());
      subsamples.push_back(Subsample<T>(samples_to_write));

      jobs.push_back(
          _create_chunk_materialization_job(output, null_rows, chunk_id, input, column_id, subsamples.back()));
      jobs.back()->schedule();
    }

    CurrentScheduler::wait_for_tasks(jobs);

    auto gathered_samples = std::vector<T>();
    gathered_samples.reserve(samples_per_chunk * chunk_count);

    for (const auto& subsample : subsamples) {
      gathered_samples.insert(gathered_samples.end(), subsample.samples.begin(), subsample.samples.end());
    }

    return {std::move(output), std::move(null_rows), std::move(gathered_samples)};
  }

 private:
  /**
   * Creates a job to materialize and sort a chunk.
   **/
  std::shared_ptr<AbstractTask> _create_chunk_materialization_job(std::unique_ptr<MaterializedSegmentList<T>>& output,
                                                                  std::unique_ptr<PosList>& null_rows_output,
                                                                  const ChunkID chunk_id,
                                                                  std::shared_ptr<const Table> input,
                                                                  const ColumnID column_id, Subsample<T>& subsample) {
    return std::make_shared<JobTask>([this, &output, &null_rows_output, input, column_id, chunk_id, &subsample] {
      const auto segment = input->get_chunk(chunk_id)->get_segment(column_id);
      (*output)[chunk_id] = _materialize_segment(*segment, chunk_id, null_rows_output, subsample);
    });
  }

  /**
   * Samples values from a materialized segment.
   * We collect samples locally and write once to the global sample collection to limit non-local writes.
   */
  void _gather_samples_from_segment(const MaterializedSegment<T>& segment, Subsample<T>& subsample) const {
    const auto samples_to_collect = subsample.samples_to_collect;
    std::vector<T> collected_samples;
    collected_samples.reserve(samples_to_collect);

    if (segment.size() > 0 && samples_to_collect > 0) {
      const auto step_width = segment.size() / std::max(1u, samples_to_collect);

      for (auto sample_count = size_t{0}; sample_count < samples_to_collect; ++sample_count) {
        // NULL values in passed `segment` vector have already been
        // removed, thus we do not have to check for NULL samples.
        collected_samples.push_back(segment[static_cast<size_t>(sample_count * step_width)].value);
      }

      // Sequential write of result
      subsample.samples.insert(subsample.samples.end(), collected_samples.begin(), collected_samples.end());
    }
  }

  /**
   * Materialization works of all types of segments
   */
  std::shared_ptr<MaterializedSegment<T>> _materialize_segment(const BaseSegment& segment, const ChunkID chunk_id,
                                                               std::unique_ptr<PosList>& null_rows_output,
                                                               Subsample<T>& subsample) {
    auto output = MaterializedSegment<T>();
    output.reserve(segment.size());

    segment_iterate<T>(segment, [&](const auto& position) {
      const auto row_id = RowID{chunk_id, position.chunk_offset()};
      if (position.is_null()) {
        if (_materialize_null) {
          null_rows_output->emplace_back(row_id);
        }
      } else {
        output.emplace_back(row_id, position.value());
      }
    });

    if (_sort) {
      std::sort(output.begin(), output.end(),
                [](const auto& left, const auto& right) { return left.value < right.value; });
    }

    _gather_samples_from_segment(output, subsample);

    return std::make_shared<MaterializedSegment<T>>(std::move(output));
  }

 private:
  bool _sort;
  bool _materialize_null;
};

}  // namespace opossum
