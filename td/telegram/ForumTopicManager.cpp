//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ForumTopicManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/Random.h"

namespace td {

class CreateForumTopicQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::forumTopicInfo>> promise_;
  ChannelId channel_id_;
  DialogId creator_dialog_id_;
  int64 random_id_;

 public:
  explicit CreateForumTopicQuery(Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, const string &title, int32 icon_color, CustomEmojiId icon_custom_emoji_id) {
    channel_id_ = channel_id;
    creator_dialog_id_ = DialogId(td_->contacts_manager_->get_my_id());

    int32 flags = 0;
    if (icon_color != -1) {
      flags |= telegram_api::channels_createForumTopic::ICON_COLOR_MASK;
    }
    if (icon_custom_emoji_id.is_valid()) {
      flags |= telegram_api::channels_createForumTopic::ICON_EMOJI_ID_MASK;
    }

    do {
      random_id_ = Random::secure_int64();
    } while (random_id_ == 0);

    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::channels_createForumTopic(flags, std::move(input_channel), title, icon_color,
                                                icon_custom_emoji_id.get(), random_id_, nullptr),
        {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_createForumTopic>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateForumTopicQuery: " << to_string(ptr);
    auto message = UpdatesManager::get_message_by_random_id(ptr.get(), DialogId(channel_id_), random_id_);
    if (message == nullptr || message->get_id() != telegram_api::messageService::ID) {
      LOG(ERROR) << "Receive invalid result for CreateForumTopicQuery: " << to_string(ptr);
      return promise_.set_error(Status::Error(400, "Invalid result received"));
    }
    auto service_message = static_cast<const telegram_api::messageService *>(message);
    if (service_message->action_->get_id() != telegram_api::messageActionTopicCreate::ID) {
      LOG(ERROR) << "Receive invalid result for CreateForumTopicQuery: " << to_string(ptr);
      return promise_.set_error(Status::Error(400, "Invalid result received"));
    }

    auto action = static_cast<const telegram_api::messageActionTopicCreate *>(service_message->action_.get());
    ForumTopicInfo forum_topic_info(MessageId(ServerMessageId(service_message->id_)), action->title_,
                                    ForumTopicIcon(action->icon_color_, action->icon_emoji_id_), service_message->date_,
                                    creator_dialog_id_, true, false);
    td_->updates_manager_->on_get_updates(
        std::move(ptr), PromiseCreator::lambda([forum_topic_info = std::move(forum_topic_info),
                                                promise = std::move(promise_)](Unit result) mutable {
          send_closure(G()->forum_topic_manager(), &ForumTopicManager::on_forum_topic_created,
                       std::move(forum_topic_info), std::move(promise));
        }));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "CreateForumTopicQuery");
    promise_.set_error(std::move(status));
  }
};

ForumTopicManager::ForumTopicManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

ForumTopicManager::~ForumTopicManager() = default;

void ForumTopicManager::tear_down() {
  parent_.reset();
}

void ForumTopicManager::create_forum_topic(DialogId dialog_id, string &&title,
                                           td_api::object_ptr<td_api::forumTopicIcon> &&icon,
                                           Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, is_forum(dialog_id));
  auto channel_id = dialog_id.get_channel_id();

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_create_topics()) {
    return promise.set_error(Status::Error(400, "Not enough rights to create a topic"));
  }

  auto new_title = clean_name(std::move(title), MAX_FORUM_TOPIC_TITLE_LENGTH);
  if (new_title.empty()) {
    return promise.set_error(Status::Error(400, "Title must be non-empty"));
  }

  int32 icon_color = -1;
  CustomEmojiId icon_custom_emoji_id;
  if (icon != nullptr) {
    icon_color = icon->color_;
    if (icon_color < 0 || icon_color > 0xFFFFFF) {
      return promise.set_error(Status::Error(400, "Invalid icon color specified"));
    }
    icon_custom_emoji_id = CustomEmojiId(icon->custom_emoji_id_);
  }

  td_->create_handler<CreateForumTopicQuery>(std::move(promise))
      ->send(channel_id, new_title, icon_color, icon_custom_emoji_id);
}

void ForumTopicManager::on_forum_topic_created(ForumTopicInfo &&forum_topic_info,
                                               Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  promise.set_value(forum_topic_info.get_forum_topic_info_object(td_));
}

Status ForumTopicManager::is_forum(DialogId dialog_id) {
  if (!td_->messages_manager_->have_dialog_force(dialog_id, "ForumTopicManager::is_forum")) {
    return Status::Error(400, "Chat not found");
  }
  if (dialog_id.get_type() != DialogType::Channel ||
      !td_->contacts_manager_->is_forum_channel(dialog_id.get_channel_id())) {
    return Status::Error(400, "The chat is not a forum");
  }
  return Status::OK();
}

}  // namespace td
