// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PDF_CHUNK_STREAM_H_
#define PDF_CHUNK_STREAM_H_

#include <stddef.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "pdf/range_set.h"

namespace chrome_pdf {

// This class collects a chunks of data into one data stream. Client can check
// if data in certain range is available, and get missing chunks of data.
template <uint32_t N>
class ChunkStream {
 public:
  static constexpr uint32_t kChunkSize = N;
  using ChunkData = typename std::array<unsigned char, N>;

  ChunkStream() {}
  ~ChunkStream() {}

  void SetChunkData(uint32_t chunk_index, std::unique_ptr<ChunkData> data) {
    if (!data)
      return;

    if (chunk_index >= data_.size())
      data_.resize(chunk_index + 1);

    if (!data_[chunk_index])
      ++filled_chunks_count_;

    data_[chunk_index] = std::move(data);
    filled_chunks_.Union(gfx::Range(chunk_index, chunk_index + 1));
  }

  bool ReadData(const gfx::Range& range, void* buffer) const {
    if (!IsRangeAvailable(range))
      return false;

    unsigned char* data_buffer = static_cast<unsigned char*>(buffer);
    uint32_t start = range.start();
    while (start != range.end()) {
      const uint32_t chunk_index = GetChunkIndex(start);
      const uint32_t chunk_start = start % kChunkSize;
      const uint32_t len =
          std::min(kChunkSize - chunk_start, range.end() - start);
      memcpy(data_buffer, data_[chunk_index]->data() + chunk_start, len);
      data_buffer += len;
      start += len;
    }
    return true;
  }

  uint32_t GetChunkIndex(uint32_t offset) const { return offset / kChunkSize; }

  gfx::Range GetChunksRange(uint32_t offset, uint32_t size) const {
    return gfx::Range(GetChunkIndex(offset), GetChunkEnd(offset + size));
  }

  bool IsRangeAvailable(const gfx::Range& range) const {
    if (!range.IsValid() || range.is_reversed() ||
        (eof_pos_ > 0 && eof_pos_ < range.end())) {
      return false;
    }

    if (range.is_empty())
      return true;

    const gfx::Range chunks_range(GetChunkIndex(range.start()),
                                  GetChunkEnd(range.end()));
    return filled_chunks_.Contains(chunks_range);
  }

  bool IsChunkAvailable(uint32_t chunk_index) const {
    return filled_chunks_.Contains(chunk_index);
  }

  void set_eof_pos(uint32_t eof_pos) { eof_pos_ = eof_pos; }
  uint32_t eof_pos() const { return eof_pos_; }

  const RangeSet& filled_chunks() const { return filled_chunks_; }

  bool IsComplete() const {
    return eof_pos_ > 0 && IsRangeAvailable(gfx::Range(0, eof_pos_));
  }

  bool IsValidChunkIndex(uint32_t chunk_index) const {
    return !eof_pos_ || (chunk_index <= GetChunkIndex(eof_pos_ - 1));
  }

  void Clear() {
    data_.clear();
    eof_pos_ = 0;
    filled_chunks_.Clear();
    filled_chunks_count_ = 0;
  }

  uint32_t filled_chunks_count() const { return filled_chunks_count_; }
  uint32_t total_chunks_count() const { return GetChunkEnd(eof_pos_); }

 private:
  uint32_t GetChunkEnd(uint32_t offset) const {
    return (offset + kChunkSize - 1) / kChunkSize;
  }

  std::vector<std::unique_ptr<ChunkData>> data_;
  uint32_t eof_pos_ = 0;
  RangeSet filled_chunks_;
  uint32_t filled_chunks_count_ = 0;
};

}  // namespace chrome_pdf

#endif  // PDF_CHUNK_STREAM_H_
