//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ForumTopicIcon.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class ForumTopicInfo {
  MessageId top_thread_message_id_;
  string title_;
  ForumTopicIcon icon_;
  int32 creation_date_ = 0;
  DialogId creator_dialog_id_;
  bool is_outgoing_ = false;
  bool is_closed_ = false;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const ForumTopicInfo &topic_info);

 public:
  ForumTopicInfo() = default;

  explicit ForumTopicInfo(const tl_object_ptr<telegram_api::ForumTopic> &forum_topic_ptr);

  ForumTopicInfo(MessageId top_thread_message_id, string title, ForumTopicIcon icon, int32 creation_date,
                 DialogId creator_dialog_id, bool is_outgoing, bool is_closed)
      : top_thread_message_id_(top_thread_message_id)
      , title_(std::move(title))
      , icon_(std::move(icon))
      , creation_date_(creation_date)
      , creator_dialog_id_(creator_dialog_id)
      , is_outgoing_(is_outgoing)
      , is_closed_(is_closed) {
  }

  bool is_empty() const {
    return !top_thread_message_id_.is_valid();
  }

  MessageId get_thread_id() const {
    return top_thread_message_id_;
  }

  td_api::object_ptr<td_api::forumTopicInfo> get_forum_topic_info_object(Td *td) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const ForumTopicInfo &topic_info);

}  // namespace td
