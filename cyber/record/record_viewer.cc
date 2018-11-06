/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "cyber/record/record_viewer.h"

#include <algorithm>
#include <utility>

#include "cyber/common/log.h"

namespace apollo {
namespace cyber {
namespace record {

RecordViewer::RecordViewer(const RecordReaderPtr& reader, uint64_t begin_time,
                           uint64_t end_time,
                           const std::set<std::string>& channels)
    : begin_time_(begin_time), end_time_(end_time), channels_(channels) {
  readers_.emplace_back(reader);
  UpdateTime();
}

RecordViewer::RecordViewer(const std::vector<RecordReaderPtr>& readers,
                           uint64_t begin_time, uint64_t end_time,
                           const std::set<std::string>& channels)
    : begin_time_(begin_time),
      end_time_(end_time),
      channels_(channels),
      readers_(readers) {
  Sort();
  UpdateTime();
}

bool RecordViewer::IsValid() const {
  if (begin_time_ > end_time_) {
    AERROR << "Begin time must be earlier than end time"
           << ", begin_time=" << begin_time_ << ", end_time=" << end_time_;
    return false;
  }
  return true;
}

bool RecordViewer::Update(RecordMessage* message) {
  bool find = false;
  while (!find) {
    if (msg_buffer_.empty() && !FillBuffer()) {
      break;
    }
    auto& msg = msg_buffer_.begin()->second;
    if (channels_.empty() || channels_.count(msg->channel_name) == 1) {
      *message = *msg;
      find = true;
    }
    msg_buffer_.erase(msg_buffer_.begin());
  }
  return find;
}

uint64_t RecordViewer::begin_time() const { return begin_time_; }

uint64_t RecordViewer::end_time() const { return end_time_; }

std::set<std::string> RecordViewer::GetChannelList() const {
  std::set<std::string> channel_list;

  for (auto& reader : readers_) {
    auto all_channel = reader->GetChannelList();
    std::set_intersection(all_channel.begin(), all_channel.end(),
                          channels_.begin(), channels_.end(),
                          std::inserter(channel_list, channel_list.end()));
  }

  return channel_list;
}

RecordViewer::Iterator RecordViewer::begin() { return Iterator(this); }

RecordViewer::Iterator RecordViewer::end() { return Iterator(this, true); }

void RecordViewer::Sort() {
  std::sort(readers_.begin(), readers_.end(),
            [](const RecordReaderPtr& lhs, const RecordReaderPtr& rhs) {
              const auto& lhs_header = lhs->header();
              const auto& rhs_header = rhs->header();
              if (lhs_header.begin_time() == rhs_header.begin_time()) {
                return lhs_header.end_time() < rhs_header.end_time();
              }
              return lhs_header.begin_time() < rhs_header.begin_time();
            });
}

void RecordViewer::Reset() {
  for (auto& reader : readers_) {
    reader->Reset();
  }
  curr_begin_time_ = begin_time_;
}

void RecordViewer::UpdateTime() {
  uint64_t min_begin_time = UINT64_MAX;
  uint64_t max_end_time = 0;

  for (auto& reader : readers_) {
    const auto& header = reader->header();
    if (min_begin_time > header.begin_time()) {
      min_begin_time = header.begin_time();
    }
    if (max_end_time < header.end_time()) {
      max_end_time = header.end_time();
    }
  }

  if (begin_time_ < min_begin_time) {
    begin_time_ = min_begin_time;
  }

  if (end_time_ > max_end_time) {
    end_time_ = max_end_time;
  }

  curr_begin_time_ = begin_time_;
}

bool RecordViewer::FillBuffer() {
  while (curr_begin_time_ <= end_time_ && msg_buffer_.size() < kBufferMinSize) {
    uint64_t this_begin_time = curr_begin_time_;
    uint64_t this_end_time = this_begin_time + kStepTimeNanoSec;
    if (this_end_time > end_time_) {
      this_end_time = end_time_;
    }

    for (auto& reader : readers_) {
      while (true) {
        auto record_msg = std::make_shared<RecordMessage>();
        if (!reader->ReadMessage(record_msg.get(), this_begin_time,
                                 this_end_time)) {
          break;
        }
        msg_buffer_.emplace(std::make_pair(record_msg->time, record_msg));
      }
    }

    // because ReadMessage of RecordReader is closed interval, so we add 1 here
    curr_begin_time_ = this_end_time + 1;
  }

  return !msg_buffer_.empty();
}

RecordViewer::Iterator::Iterator(RecordViewer* viewer, bool end)
    : end_(end), viewer_(viewer) {
  if (end_) {
    return;
  }
  viewer_->Reset();
  if (!viewer_->IsValid()) {
    end_ = true;
  } else {
    if (!viewer_->Update(&message_instance_)) {
      end_ = true;
    }
  }
}

bool RecordViewer::Iterator::operator==(Iterator const& other) const {
  if (other.end_) {
    return end_;
  }
  return index_ == other.index_ && viewer_ == other.viewer_;
}

bool RecordViewer::Iterator::operator!=(const Iterator& rhs) const {
  return !(*this == rhs);
}

void RecordViewer::Iterator::operator++() {
  index_++;
  if (!viewer_->Update(&message_instance_)) {
    end_ = true;
  }
}

RecordViewer::Iterator::pointer RecordViewer::Iterator::operator->() {
  return &message_instance_;
}

RecordViewer::Iterator::reference RecordViewer::Iterator::operator*() {
  return message_instance_;
}

}  // namespace record
}  // namespace cyber
}  // namespace apollo
