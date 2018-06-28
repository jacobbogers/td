//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/detail/BinlogEventsProcessor.h"

#include "td/mtproto/crypto.h"
#include "td/mtproto/utils.h"

#include "td/telegram/Global.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/SecretChatActor.h"
#include "td/telegram/SecretChatId.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/telegram_api.h"

#include "td/tl/tl_object_parse.h"
#include "td/tl/tl_object_store.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/Gzip.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <memory>

REGISTER_TESTS(secret);

namespace my_api {

using namespace td;

//messages_getDhConfig
class messages_getDhConfig {
 public:
  int32 version_;
  int32 random_length_;

  messages_getDhConfig() = default;

  messages_getDhConfig(int32 version_, int32 random_length_);

  static const int32 ID = 651135312;

  explicit messages_getDhConfig(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : version_(TlFetchInt::parse(p))
      , random_length_(TlFetchInt::parse(p))
#undef FAIL
  {
  }
};

//InputUser
class InputUser {
 public:
  static tl_object_ptr<InputUser> fetch(TlBufferParser &p);
};

class inputUser final : public InputUser {
 public:
  int32 user_id_;
  int64 access_hash_;

  static const int32 ID = -668391402;
  inputUser() = default;

  explicit inputUser(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : user_id_(TlFetchInt::parse(p))
      , access_hash_(TlFetchLong::parse(p))
#undef FAIL
  {
  }
};
tl_object_ptr<InputUser> InputUser::fetch(TlBufferParser &p) {
#define FAIL(error)   \
  p.set_error(error); \
  return nullptr;
  int constructor = p.fetch_int();
  switch (constructor) {
    case inputUser::ID:
      return make_tl_object<inputUser>(p);
    default:
      FAIL(PSTRING() << "Unknown constructor found " << format::as_hex(constructor));
  }
#undef FAIL
}

class messages_requestEncryption final {
 public:
  tl_object_ptr<InputUser> user_id_;
  int32 random_id_;
  BufferSlice g_a_;

  static const int32 ID = -162681021;
  messages_requestEncryption();

  explicit messages_requestEncryption(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : user_id_(TlFetchObject<InputUser>::parse(p))
      , random_id_(TlFetchInt::parse(p))
      , g_a_(TlFetchBytes<BufferSlice>::parse(p))
#undef FAIL
  {
  }
};

class inputEncryptedChat final {
 public:
  int32 chat_id_;
  int64 access_hash_;

  inputEncryptedChat() = default;

  static const int32 ID = -247351839;
  explicit inputEncryptedChat(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : chat_id_(TlFetchInt::parse(p))
      , access_hash_(TlFetchLong::parse(p))
#undef FAIL
  {
  }
  static tl_object_ptr<inputEncryptedChat> fetch(TlBufferParser &p) {
    return make_tl_object<inputEncryptedChat>(p);
  }
};

class messages_acceptEncryption final {
 public:
  tl_object_ptr<inputEncryptedChat> peer_;
  BufferSlice g_b_;
  int64 key_fingerprint_;

  messages_acceptEncryption() = default;

  static const int32 ID = 1035731989;

  explicit messages_acceptEncryption(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : peer_(TlFetchBoxed<TlFetchObject<inputEncryptedChat>, -247351839>::parse(p))
      , g_b_(TlFetchBytes<BufferSlice>::parse(p))
      , key_fingerprint_(TlFetchLong::parse(p))
#undef FAIL
  {
  }
};

class messages_sendEncryptedService final {
 public:
  tl_object_ptr<inputEncryptedChat> peer_;
  int64 random_id_;
  BufferSlice data_;

  messages_sendEncryptedService() = default;
  static const int32 ID = 852769188;
  explicit messages_sendEncryptedService(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : peer_(TlFetchBoxed<TlFetchObject<inputEncryptedChat>, -247351839>::parse(p))
      , random_id_(TlFetchLong::parse(p))
      , data_(TlFetchBytes<BufferSlice>::parse(p))
#undef FAIL
  {
  }
};

class messages_sendEncrypted final {
 public:
  tl_object_ptr<inputEncryptedChat> peer_;
  int64 random_id_;
  BufferSlice data_;

  messages_sendEncrypted() = default;
  static const int32 ID = -1451792525;

  explicit messages_sendEncrypted(TlBufferParser &p)
#define FAIL(error) p.set_error(error)
      : peer_(TlFetchBoxed<TlFetchObject<inputEncryptedChat>, -247351839>::parse(p))
      , random_id_(TlFetchLong::parse(p))
      , data_(TlFetchBytes<BufferSlice>::parse(p))
#undef FAIL
  {
  }
};

template <class F>
static void downcast_call(TlBufferParser &p, F &&f) {
  auto id = p.fetch_int();
  switch (id) {
    case messages_getDhConfig::ID:
      return f(*make_tl_object<messages_getDhConfig>(p));
    case messages_requestEncryption::ID:
      return f(*make_tl_object<messages_requestEncryption>(p));
    case messages_acceptEncryption::ID:
      return f(*make_tl_object<messages_acceptEncryption>(p));
    case messages_sendEncrypted::ID:
      return f(*make_tl_object<messages_sendEncrypted>(p));
    case messages_sendEncryptedService::ID:
      return f(*make_tl_object<messages_sendEncryptedService>(p));
    default:
      CHECK(0) << id;
      UNREACHABLE();
  }
}

class messages_dhConfig final {
 public:
  int32 g_;
  BufferSlice p_;
  int32 version_;
  BufferSlice random_;

  messages_dhConfig() = default;

  messages_dhConfig(int32 g_, BufferSlice &&p_, int32 version_, BufferSlice &&random_)
      : g_(g_), p_(std::move(p_)), version_(version_), random_(std::move(random_)) {
  }

  static const int32 ID = 740433629;
  int32 get_id() const {
    return ID;
  }

  void store(TlStorerCalcLength &s) const {
    (void)sizeof(s);
    TlStoreBinary::store(g_, s);
    TlStoreString::store(p_, s);
    TlStoreBinary::store(version_, s);
    TlStoreString::store(random_, s);
  }
  void store(TlStorerUnsafe &s) const {
    (void)sizeof(s);
    TlStoreBinary::store(g_, s);
    TlStoreString::store(p_, s);
    TlStoreBinary::store(version_, s);
    TlStoreString::store(random_, s);
  }
};

class encryptedChat final {
 public:
  int32 id_;
  int64 access_hash_;
  int32 date_;
  int32 admin_id_;
  int32 participant_id_;
  BufferSlice g_a_or_b_;
  int64 key_fingerprint_;

  encryptedChat() = default;

  encryptedChat(int32 id_, int64 access_hash_, int32 date_, int32 admin_id_, int32 participant_id_,
                BufferSlice &&g_a_or_b_, int64 key_fingerprint_)
      : id_(id_)
      , access_hash_(access_hash_)
      , date_(date_)
      , admin_id_(admin_id_)
      , participant_id_(participant_id_)
      , g_a_or_b_(std::move(g_a_or_b_))
      , key_fingerprint_(key_fingerprint_) {
  }

  static const int32 ID = -94974410;
  int32 get_id() const {
    return ID;
  }

  void store(TlStorerCalcLength &s) const {
    (void)sizeof(s);
    TlStoreBinary::store(id_, s);
    TlStoreBinary::store(access_hash_, s);
    TlStoreBinary::store(date_, s);
    TlStoreBinary::store(admin_id_, s);
    TlStoreBinary::store(participant_id_, s);
    TlStoreString::store(g_a_or_b_, s);
    TlStoreBinary::store(key_fingerprint_, s);
  }

  void store(TlStorerUnsafe &s) const {
    (void)sizeof(s);
    TlStoreBinary::store(id_, s);
    TlStoreBinary::store(access_hash_, s);
    TlStoreBinary::store(date_, s);
    TlStoreBinary::store(admin_id_, s);
    TlStoreBinary::store(participant_id_, s);
    TlStoreString::store(g_a_or_b_, s);
    TlStoreBinary::store(key_fingerprint_, s);
  }
};

class messages_sentEncryptedMessage final {
 public:
  int32 date_;

  messages_sentEncryptedMessage() = default;

  explicit messages_sentEncryptedMessage(int32 date_) : date_(date_) {
  }

  static const int32 ID = 1443858741;
  int32 get_id() const {
    return ID;
  }

  void store(TlStorerCalcLength &s) const {
    (void)sizeof(s);
    TlStoreBinary::store(date_, s);
  }

  void store(TlStorerUnsafe &s) const {
    (void)sizeof(s);
    TlStoreBinary::store(date_, s);
  }
};

}  // namespace my_api

namespace td {
static int32 g = 3;
static string prime_base64 =
    "xxyuucaxyQSObFIvcPE_c5gNQCOOPiHBSTTQN1Y9kw9IGYoKp8FAWCKUk9IlMPTb-jNvbgrJJROVQ67UTM58NyD9UfaUWHBaxozU_mtrE6vcl0ZRKW"
    "kyhFTxj6-MWV9kJHf-lrsqlB1bzR1KyMxJiAcI-ps3jjxPOpBgvuZ8-aSkppWBEFGQfhYnU7VrD2tBDbp02KhLKhSzFE4O8ShHVP0X7ZUNWWW0ud1G"
    "WC2xF40WnGvEZbDW_5yjko_vW5rk5Bj8Feg-vqD4f6n_Xu1wBQ3tKEn0e_lZ2VaFDOkphR8NgRX2NbEF7i5OFdBLJFS_b0-t8DSxBAMRnNjjuS_MW"
    "w";

class FakeDhCallback : public DhCallback {
 public:
  int is_good_prime(Slice prime_str) const override {
    auto it = cache.find(prime_str.str());
    if (it == cache.end()) {
      return -1;
    }
    return it->second;
  }
  void add_good_prime(Slice prime_str) const override {
    cache[prime_str.str()] = 1;
  }
  void add_bad_prime(Slice prime_str) const override {
    cache[prime_str.str()] = 0;
  }
  mutable std::map<string, int> cache;
};

class FakeBinlog
    : public BinlogInterface
    , public Actor {
 public:
  FakeBinlog() {
    register_actor("FakeBinlog", this).release();
  }
  void force_sync(Promise<> promise) override {
    if (pending_events_.empty()) {
      pending_events_.emplace_back();
    }
    pending_events_.back().promises_.push_back(std::move(promise));
    pending_events_.back().sync_flag = true;
    request_sync();
  }
  void request_sync() {
    if (!has_request_sync) {
      has_request_sync = true;
      if (Random::fast(0, 4) == 0) {
        set_timeout_in(Random::fast(0, 99) / 100.0 * 0.005 + 0.001);
      } else {
        yield();
      }
    }
  }
  void force_flush() override {
  }

  uint64 next_id() override {
    auto res = last_id_;
    last_id_++;
    return res;
  }
  uint64 next_id(int32 shift) override {
    auto res = last_id_;
    last_id_ += shift;
    return res;
  }
  template <class F>
  void for_each(const F &f) {
    events_processor_.for_each([&](auto &x) {
      LOG(INFO) << "REPLAY: " << x.id_;
      f(x);
    });
  }

  void restart() {
    has_request_sync = false;
    cancel_timeout();
    for (auto &pending : pending_events_) {
      auto &event = pending.event;
      if (!event.empty()) {
        // LOG(ERROR) << "FORGET EVENT: " << event.id_ << " " << event;
      }
    }
    pending_events_.clear();
  }

  void change_key(DbKey key, Promise<> promise) override {
  }

 protected:
  void close_impl(Promise<> promise) override {
  }
  void close_and_destroy_impl(Promise<> promise) override {
  }
  void add_raw_event_impl(uint64 id, BufferSlice &&raw_event, Promise<> promise, BinlogDebugInfo info) override {
    auto event = BinlogEvent(std::move(raw_event), info);
    LOG(INFO) << "ADD EVENT: " << event.id_ << " " << event;
    pending_events_.emplace_back();
    pending_events_.back().event = std::move(event);
    pending_events_.back().promises_.push_back(std::move(promise));
  }
  void do_force_sync() {
    if (pending_events_.empty()) {
      return;
    }
    cancel_timeout();
    has_request_sync = false;
    auto pos = static_cast<size_t>(Random::fast_uint64() % pending_events_.size());
    // pos = pending_events_.size() - 1;
    std::vector<Promise<>> promises;
    for (size_t i = 0; i <= pos; i++) {
      auto &pending = pending_events_[i];
      auto event = std::move(pending.event);
      if (!event.empty()) {
        LOG(INFO) << "SAVE EVENT: " << event.id_ << " " << event;
        events_processor_.add_event(std::move(event));
      }
      append(promises, std::move(pending.promises_));
    }
    pending_events_.erase(pending_events_.begin(), pending_events_.begin() + pos + 1);
    for (auto &promise : promises) {
      promise.set_value(Unit());
    }

    for (auto &event : pending_events_) {
      if (event.sync_flag) {
        request_sync();
        break;
      }
    }
  }
  void timeout_expired() override {
    do_force_sync();
  }
  void wakeup() override {
    if (has_request_sync) {
      do_force_sync();
    }
  }
  bool has_request_sync = false;
  uint64 last_id_ = 1;
  detail::BinlogEventsProcessor events_processor_;

  struct PendingEvent {
    BinlogEvent event;
    bool sync_flag = false;
    std::vector<Promise<>> promises_;
  };

  std::vector<PendingEvent> pending_events_;
};

using FakeKeyValue = BinlogKeyValue<BinlogInterface>;
class OldFakeKeyValue : public KeyValueSyncInterface {
  SeqNo set(string key, string value) override {
    kv_[key] = value;
    return 0;
  }

  SeqNo erase(const string &key) override {
    kv_.erase(key);
    return 0;
  }

  bool isset(const string &key) override {
    return kv_.count(key) > 0;
  }

  string get(const string &key) override {
    auto it = kv_.find(key);
    if (it != kv_.end()) {
      return it->second;
    }
    return string();
  }

 private:
  std::map<string, string> kv_;
};

class Master;
class FakeSecretChatContext : public SecretChatActor::Context {
 public:
  FakeSecretChatContext(std::shared_ptr<BinlogInterface> binlog, std::shared_ptr<KeyValueSyncInterface> key_value,
                        std::shared_ptr<bool> close_flag, ActorShared<Master> master)
      : binlog_(std::move(binlog))
      , key_value_(std::move(key_value))
      , close_flag_(std::move(close_flag))
      , master_(std::move(master)) {
    secret_chat_db_ = std::make_unique<SecretChatDb>(key_value_, 1);
    net_query_creator_.stop_check();  // :(
  }
  DhCallback *dh_callback() override {
    return &fake_dh_callback_;
  }
  NetQueryCreator &net_query_creator() override {
    return net_query_creator_;
  }
  int32 unix_time() override {
    return static_cast<int32>(std::time(nullptr));
  }
  bool close_flag() override {
    return *close_flag_;
  }
  BinlogInterface *binlog() override {
    return binlog_.get();
  }
  SecretChatDb *secret_chat_db() override {
    return secret_chat_db_.get();
  }
  std::shared_ptr<DhConfig> dh_config() override {
    static auto config = [] {
      DhConfig dh_config;
      dh_config.version = 12;
      dh_config.g = g;
      dh_config.prime = base64url_decode(prime_base64).move_as_ok();
      return std::make_shared<DhConfig>(dh_config);
    }();

    return config;
  }
  void set_dh_config(std::shared_ptr<DhConfig> dh_config) override {
    // empty
  }

  bool get_config_option_boolean(const string &name) const override {
    return false;
  }

  // We don't want to expose the whole NetQueryDispatcher, MessagesManager and ContactsManager.
  // So it is more clear which parts of MessagesManager is really used. And it is much easier to create tests.
  void send_net_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback, bool ordered) override;

  void on_update_secret_chat(int64 access_hash, UserId user_id, SecretChatState state, bool is_outbound, int32 ttl,
                             int32 date, string key_hash, int32 layer) override {
  }

  void on_inbound_message(UserId user_id, MessageId message_id, int32 date,
                          tl_object_ptr<telegram_api::encryptedFile> file,
                          tl_object_ptr<secret_api::decryptedMessage> message, Promise<>) override;

  void on_send_message_error(int64 random_id, Status error, Promise<>) override;
  void on_send_message_ack(int64 random_id) override;
  void on_send_message_ok(int64 random_id, MessageId message_id, int32 date,
                          tl_object_ptr<telegram_api::EncryptedFile> file, Promise<>) override;
  void on_delete_messages(std::vector<int64> random_id, Promise<>) override;
  void on_flush_history(MessageId, Promise<>) override;
  void on_read_message(int64, Promise<>) override;

  void on_screenshot_taken(UserId user_id, MessageId message_id, int32 date, int64 random_id,
                           Promise<> promise) override {
  }
  void on_set_ttl(UserId user_id, MessageId message_id, int32 date, int32 ttl, int64 random_id,
                  Promise<> promise) override {
  }

 private:
  FakeDhCallback fake_dh_callback_;
  static NetQueryCreator net_query_creator_;
  std::shared_ptr<BinlogInterface> binlog_;
  std::shared_ptr<KeyValueSyncInterface> key_value_;
  std::shared_ptr<bool> close_flag_;
  ActorShared<Master> master_;

  std::shared_ptr<SecretChatDb> secret_chat_db_;
};
NetQueryCreator FakeSecretChatContext::net_query_creator_;

class Master : public Actor {
 public:
  explicit Master(Status *status) : status_(status) {
  }
  class SecretChatProxy : public Actor {
   public:
    SecretChatProxy(string name, ActorShared<Master> parent) : name_(std::move(name)) {
      binlog_ = std::make_shared<FakeBinlog>();
      key_value_ = std::make_shared<FakeKeyValue>();
      key_value_->external_init_begin(LogEvent::HandlerType::BinlogPmcMagic);
      key_value_->external_init_finish(binlog_);
      close_flag_ = std::make_shared<bool>(false);
      parent_ = parent.get();
      parent_token_ = parent.token();
      actor_ = create_actor<SecretChatActor>(
          "SecretChat " + name_, 123,
          std::make_unique<FakeSecretChatContext>(binlog_, key_value_, close_flag_, std::move(parent)), true);
      on_binlog_replay_finish();
    }

    ActorOwn<SecretChatActor> actor_;

    void add_inbound_message(int32 chat_id, BufferSlice data, uint64 crc) {
      CHECK(crc64(data.as_slice()) == crc);
      auto event = std::make_unique<logevent::InboundSecretMessage>();
      event->qts = 0;
      event->chat_id = chat_id;
      event->date = 0;
      event->encrypted_message = std::move(data);
      event->qts_ack = PromiseCreator::lambda(
          [actor_id = actor_id(this), chat_id, data = event->encrypted_message.copy(), crc](Result<> result) mutable {
            if (result.is_ok()) {
              LOG(INFO) << "FINISH add_inbound_message " << tag("crc", crc);
              return;
            }
            LOG(INFO) << "RESEND add_inbound_message " << tag("crc", crc) << result.error();
            send_closure(actor_id, &SecretChatProxy::add_inbound_message, chat_id, std::move(data), crc);
          });

      add_event(Event::delayed_closure(&SecretChatActor::add_inbound_message, std::move(event)));
    }

    void send_message(tl_object_ptr<secret_api::decryptedMessage> message) {
      BufferSlice serialized_message(serialize(*message));
      auto resend_promise = PromiseCreator::lambda(
          [actor_id = actor_id(this), serialized_message = std::move(serialized_message)](Result<> result) mutable {
            TlBufferParser parser(&serialized_message);
            auto message = secret_api::decryptedMessage::fetch(parser);
            if (result.is_ok()) {
              LOG(INFO) << "FINISH send_message " << tag("message", to_string(message));
              return;
            }
            LOG(INFO) << "RESEND send_message " << tag("message", to_string(message)) << result.error();
            CHECK(serialize(*message) == serialized_message.as_slice());
            send_closure(actor_id, &SecretChatProxy::send_message, std::move(message));
          });
      auto sync_promise = PromiseCreator::lambda([actor_id = actor_id(this), generation = this->binlog_generation_,
                                                  resend_promise = std::move(resend_promise)](Result<> result) mutable {
        if (result.is_error()) {
          resend_promise.set_error(result.move_as_error());
          return;
        }
        send_closure(actor_id, &SecretChatProxy::sync_binlog, generation, std::move(resend_promise));
      });

      add_event(
          Event::delayed_closure(&SecretChatActor::send_message, std::move(message), nullptr, std::move(sync_promise)));
    }
    int32 binlog_generation_ = 0;
    void sync_binlog(int32 binlog_generation, Promise<> promise) {
      if (binlog_generation != binlog_generation_) {
        return promise.set_error(Status::Error("binlog generation mismatch"));
      }
      binlog_->force_sync(std::move(promise));
    }
    void on_closed() {
      LOG(INFO) << "CLOSED";
      ready_ = false;
      *close_flag_ = false;

      key_value_ = std::make_shared<FakeKeyValue>();
      key_value_->external_init_begin(LogEvent::HandlerType::BinlogPmcMagic);

      std::vector<BinlogEvent> events;
      binlog_generation_++;
      binlog_->restart();
      binlog_->for_each([&](const BinlogEvent &event) {
        if (event.type_ == LogEvent::HandlerType::BinlogPmcMagic) {
          key_value_->external_init_handle(event);
        } else {
          events.push_back(event.clone());
        }
      });

      key_value_->external_init_finish(binlog_);

      actor_ = create_actor<SecretChatActor>(
          "SecretChat " + name_, 123,
          std::make_unique<FakeSecretChatContext>(binlog_, key_value_, close_flag_,
                                                  ActorShared<Master>(parent_, parent_token_)),
          true);

      for (auto &event : events) {
        CHECK(event.type_ == LogEvent::HandlerType::SecretChats);
        auto r_message = logevent::SecretChatEvent::from_buffer_slice(event.data_as_buffer_slice());
        LOG_IF(FATAL, r_message.is_error()) << "Failed to deserialize event: " << r_message.error();
        auto message = r_message.move_as_ok();
        message->set_logevent_id(event.id_);
        LOG(INFO) << "Process binlog event " << *message;
        switch (message->get_type()) {
          case logevent::SecretChatEvent::Type::InboundSecretMessage:
            send_closure_later(actor_, &SecretChatActor::replay_inbound_message,
                               std::unique_ptr<logevent::InboundSecretMessage>(
                                   static_cast<logevent::InboundSecretMessage *>(message.release())));
            break;
          case logevent::SecretChatEvent::Type::OutboundSecretMessage:
            send_closure_later(actor_, &SecretChatActor::replay_outbound_message,
                               std::unique_ptr<logevent::OutboundSecretMessage>(
                                   static_cast<logevent::OutboundSecretMessage *>(message.release())));
            break;
          default:
            UNREACHABLE();
        }
      };
      start_test();
      on_binlog_replay_finish();
    }
    void on_binlog_replay_finish() {
      ready_ = true;
      LOG(INFO) << "on_binlog_replay_finish!";
      send_closure(actor_, &SecretChatActor::binlog_replay_finish);
      for (auto &event : pending_events_) {
        send_event(actor_, std::move(event));
      }
      pending_events_.clear();
    }
    void start_test() {
      set_timeout_in(Random::fast(50, 99) * 0.3 / 50);
      events_cnt_ = 0;
    }

   private:
    string name_;

    ActorId<Master> parent_;
    uint64 parent_token_;
    std::shared_ptr<FakeBinlog> binlog_;
    std::shared_ptr<FakeKeyValue> key_value_;
    std::shared_ptr<bool> close_flag_;
    int events_cnt_ = 0;

    std::vector<Event> pending_events_;
    bool ready_ = false;

    bool is_active() {
      return !actor_.empty() && ready_;
    }
    void add_event(Event event) {
      events_cnt_++;
      if (is_active()) {
        LOG(INFO) << "EMIT";
        send_event(actor_, std::move(event));
      } else {
        LOG(INFO) << "DELAY";
        pending_events_.push_back(std::move(event));
      }
    }

    int32 bad_cnt_ = 0;
    void timeout_expired() override {
      LOG(INFO) << "TIMEOUT EXPIRED";
      if (events_cnt_ < 4) {
        bad_cnt_++;
        CHECK(bad_cnt_ < 10);
      } else {
        bad_cnt_ = 0;
      }
      *close_flag_ = true;
      actor_.reset();
    }
  };

  auto &get_by_id(uint64 id) {
    if (id == 1) {
      return alice_;
    } else {
      return bob_;
    }
  }
  auto &from() {
    return get_by_id(get_link_token());
  }
  auto &to() {
    return get_by_id(3 - get_link_token());
  }
  void start_up() override {
    set_context(std::make_shared<Global>());
    alice_ = create_actor<SecretChatProxy>("SecretChatProxy alice", "alice", actor_shared(this, 1));
    bob_ = create_actor<SecretChatProxy>("SecretChatProxy bob", "bob", actor_shared(this, 2));
    send_closure(alice_->get_actor_unsafe()->actor_, &SecretChatActor::create_chat, 2, 0, 123,
                 PromiseCreator::lambda([actor_id = actor_id(this)](Result<SecretChatId> res) {
                   send_closure(actor_id, &Master::got_secret_chat_id, std::move(res), 0);
                 }));
  }
  void got_secret_chat_id(Result<SecretChatId> res, int) {  // second parameter is needed to workaround clang bug
    CHECK(res.is_ok());
    auto id = res.move_as_ok();
    LOG(INFO) << "SecretChatId = " << id;
  }
  bool can_fail(NetQueryPtr &query) {
    static int cnt = 20;
    if (cnt > 0) {
      cnt--;
      return false;
    }
    if (query->tl_constructor() == telegram_api::messages_sendEncrypted::ID ||
        query->tl_constructor() == telegram_api::messages_sendEncryptedFile::ID) {
      return true;
    }
    return false;
  }
  void send_net_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback, bool ordered) {
    if (can_fail(query) && Random::fast(0, 1) == 0) {
      LOG(INFO) << "Fail query " << query;
      auto resend_promise =
          PromiseCreator::lambda([id = actor_shared(this, get_link_token()), callback_actor = callback.get(),
                                  callback_token = callback.token()](Result<NetQueryPtr> r_net_query) mutable {
            if (r_net_query.is_error()) {
              id.release();
              return;
            }
            send_closure(std::move(id), &Master::send_net_query, r_net_query.move_as_ok(),
                         ActorShared<NetQueryCallback>(callback_actor, callback_token), true);
          });
      query->set_error(Status::Error(429, "Test error"));
      send_closure(std::move(callback), &NetQueryCallback::on_result_resendable, std::move(query),
                   std::move(resend_promise));
      return;
    } else {
      LOG(INFO) << "Do not fail " << query;
    }
    auto query_slice = query->query().clone();
    if (query->gzip_flag() == NetQuery::GzipFlag::On) {
      query_slice = gzdecode(query_slice.as_slice());
    }
    TlBufferParser parser(&query_slice);
    //auto object = telegram_api::Function::fetch(parser);
    //LOG(INFO) << query_slice.size();
    //parser.get_status().ensure();
    my_api::downcast_call(parser, [&](auto &object) {
      this->process_net_query(std::move(object), std::move(query), std::move(callback));
    });
  }
  template <class T>
  void process_net_query(T &&object, NetQueryPtr query, ActorShared<NetQueryCallback> callback) {
    LOG(FATAL) << "Unsupported query: " << to_string(object);
  }
  void process_net_query(my_api::messages_getDhConfig &&get_dh_config, NetQueryPtr net_query,
                         ActorShared<NetQueryCallback> callback) {
    //LOG(INFO) << "Got query " << to_string(get_dh_config);
    my_api::messages_dhConfig config;
    config.p_ = BufferSlice(base64url_decode(prime_base64).move_as_ok());
    config.g_ = g;
    config.version_ = 12;
    auto storer = TLObjectStorer<my_api::messages_dhConfig>(config);
    BufferSlice answer(storer.size());
    storer.store(answer.as_slice().ubegin());
    net_query->set_ok(std::move(answer));
    send_closure(std::move(callback), &NetQueryCallback::on_result, std::move(net_query));
  }
  void process_net_query(my_api::messages_requestEncryption &&request_encryption, NetQueryPtr net_query,
                         ActorShared<NetQueryCallback> callback) {
    CHECK(get_link_token() == 1);
    send_closure(alice_->get_actor_unsafe()->actor_, &SecretChatActor::update_chat,
                 make_tl_object<telegram_api::encryptedChatWaiting>(123, 321, 0, 1, 2));
    send_closure(
        bob_->get_actor_unsafe()->actor_, &SecretChatActor::update_chat,
        make_tl_object<telegram_api::encryptedChatRequested>(123, 321, 0, 1, 2, request_encryption.g_a_.clone()));
    net_query->clear();
  }
  void process_net_query(my_api::messages_acceptEncryption &&request_encryption, NetQueryPtr net_query,
                         ActorShared<NetQueryCallback> callback) {
    CHECK(get_link_token() == 2);
    send_closure(alice_->get_actor_unsafe()->actor_, &SecretChatActor::update_chat,
                 make_tl_object<telegram_api::encryptedChat>(123, 321, 0, 1, 2, request_encryption.g_b_.clone(),
                                                             request_encryption.key_fingerprint_));

    my_api::encryptedChat encrypted_chat(123, 321, 0, 1, 2, BufferSlice(), request_encryption.key_fingerprint_);
    auto storer = TLObjectStorer<my_api::encryptedChat>(encrypted_chat);
    BufferSlice answer(storer.size());
    storer.store(answer.as_slice().ubegin());
    net_query->set_ok(std::move(answer));
    send_closure(std::move(callback), &NetQueryCallback::on_result, std::move(net_query));
    send_closure(alice_, &SecretChatProxy::start_test);
    send_closure(bob_, &SecretChatProxy::start_test);
    send_ping(1, 5000);
    set_timeout_in(1);
  }
  void timeout_expired() override {
    send_message(1, "oppa");
    send_message(2, "appo");
    set_timeout_in(1);
  }
  void send_ping(int id, int cnt) {
    if (cnt % 200 == 0) {
      LOG(ERROR) << "send ping " << tag("id", id) << tag("cnt", cnt);
    } else {
      LOG(INFO) << "send ping " << tag("id", id) << tag("cnt", cnt);
    }
    string text = PSTRING() << "PING: " << cnt;
    send_message(id, std::move(text));
  }
  void send_message(int id, string text) {
    auto random_id = Random::secure_int64();
    LOG(INFO) << "send message: " << tag("id", id) << tag("text", text) << tag("random_id", random_id);
    sent_messages_[random_id] = Message{id, text};
    send_closure(get_by_id(id), &SecretChatProxy::send_message,
                 secret_api::make_object<secret_api::decryptedMessage>(0, random_id, 0, text, Auto(), Auto(), Auto(),
                                                                       Auto(), 0));
  }
  void process_net_query(my_api::messages_sendEncryptedService &&message, NetQueryPtr net_query,
                         ActorShared<NetQueryCallback> callback) {
    process_net_query_send_enrypted(std::move(message.data_), std::move(net_query), std::move(callback));
  }
  void process_net_query(my_api::messages_sendEncrypted &&message, NetQueryPtr net_query,
                         ActorShared<NetQueryCallback> callback) {
    process_net_query_send_enrypted(std::move(message.data_), std::move(net_query), std::move(callback));
  }
  void process_net_query_send_enrypted(BufferSlice data, NetQueryPtr net_query,
                                       ActorShared<NetQueryCallback> callback) {
    my_api::messages_sentEncryptedMessage sent_message;
    sent_message.date_ = 0;
    auto storer = TLObjectStorer<my_api::messages_sentEncryptedMessage>(sent_message);
    BufferSlice answer(storer.size());
    storer.store(answer.as_slice().ubegin());
    net_query->set_ok(std::move(answer));
    send_closure(std::move(callback), &NetQueryCallback::on_result, std::move(net_query));

    // We can't loose updates yet :(
    auto crc = crc64(data.as_slice());
    LOG(INFO) << "send SecretChatProxy::add_inbound_message" << tag("crc", crc);
    send_closure(to(), &SecretChatProxy::add_inbound_message, narrow_cast<int32>(3 - get_link_token()), std::move(data),
                 crc);
  }

  int32 last_ping_ = std::numeric_limits<int32>::max();
  void on_inbound_message(string message, Promise<> promise) {
    promise.set_value(Unit());
    LOG(INFO) << "GOT INBOUND MESSAGE: " << message << " " << get_link_token();
    int32 cnt;
    int x = std::sscanf(message.c_str(), "PING: %d", &cnt);
    if (x != 1) {
      return;
    }
    if (cnt == 0) {
      Scheduler::instance()->finish();
      *status_ = Status::OK();
      return;
    }
    if (cnt >= last_ping_) {
      return;
    }
    last_ping_ = cnt;
    send_ping(narrow_cast<int32>(get_link_token()), cnt - 1);
  }
  void on_send_message_error(int64 random_id, Status error, Promise<> promise) {
    promise.set_value(Unit());
    LOG(INFO) << "on_send_message_error: " << tag("random_id", random_id) << error;
    auto it = sent_messages_.find(random_id);
    if (it == sent_messages_.end()) {
      LOG(INFO) << "TODO: try fix errors about message after it is sent";
      return;
    }
    CHECK(it != sent_messages_.end());
    auto message = it->second;
    // sent_messages_.erase(it);
    send_message(message.id, message.text);
  }
  void on_send_message_ok(int64 random_id, Promise<> promise) {
    promise.set_value(Unit());
    LOG(INFO) << "on_send_message_ok: " << tag("random_id", random_id);
    auto it = sent_messages_.find(random_id);
    if (it == sent_messages_.end()) {
      LOG(INFO) << "TODO: try fix errors about message after it is sent";
      return;
    }
    CHECK(it != sent_messages_.end());
    // sent_messages_.erase(it);
  }

 private:
  Status *status_;
  ActorOwn<SecretChatProxy> alice_;
  ActorOwn<SecretChatProxy> bob_;
  struct Message {
    int32 id;
    string text;
  };
  std::map<int64, Message> sent_messages_;

  void hangup_shared() override {
    LOG(INFO) << "GOT HANGUP: " << get_link_token();
    send_closure(from(), &SecretChatProxy::on_closed);
  }
};

void FakeSecretChatContext::send_net_query(NetQueryPtr query, ActorShared<NetQueryCallback> callback, bool ordered) {
  send_closure(master_, &Master::send_net_query, std::move(query), std::move(callback), ordered);
}
void FakeSecretChatContext::on_inbound_message(UserId user_id, MessageId message_id, int32 date,
                                               tl_object_ptr<telegram_api::encryptedFile> file,
                                               tl_object_ptr<secret_api::decryptedMessage> message, Promise<> promise) {
  send_closure(master_, &Master::on_inbound_message, message->message_, std::move(promise));
}
void FakeSecretChatContext::on_send_message_error(int64 random_id, Status error, Promise<> promise) {
  send_closure(master_, &Master::on_send_message_error, random_id, std::move(error), std::move(promise));
}
void FakeSecretChatContext::on_send_message_ack(int64 random_id) {
}
void FakeSecretChatContext::on_send_message_ok(int64 random_id, MessageId message_id, int32 date,
                                               tl_object_ptr<telegram_api::EncryptedFile> file, Promise<> promise) {
  send_closure(master_, &Master::on_send_message_ok, random_id, std::move(promise));
}
void FakeSecretChatContext::on_delete_messages(std::vector<int64> random_id, Promise<> promise) {
  promise.set_value(Unit());
}
void FakeSecretChatContext::on_flush_history(MessageId, Promise<> promise) {
  promise.set_error(Status::Error("unsupported"));
}
void FakeSecretChatContext::on_read_message(int64, Promise<> promise) {
  promise.set_error(Status::Error("unsupported"));
}

TEST(Secret, go) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  Status result;
  sched.create_actor_unsafe<Master>(0, "HandshakeTestActor", &result).release();
  sched.start();
  while (sched.run_main(10)) {
    // empty;
  }
  sched.finish();

  if (result.is_error()) {
    LOG(ERROR) << result;
  }
  ASSERT_TRUE(result.is_ok());
}

}  // namespace td
