/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "mdbls/lsp/server.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <future>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "lsp/features.h"
#include "lsp/language_features.h"
#include "mdbls/connection_uri.h"
#include "mdbls/document.h"
#include "mdbls/keywords.h"
#include "mdbls/live_catalog.h"
#include "mdbls/mcp_catalog.h"
#include "mdbls/workspace_catalog.h"
#include "mdbls/lsp/transport.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/semantic_tokens.h"
#include "mdbls/sql_mode.h"
#include "mdbls/syntax_check.h"

namespace mdbls::lsp {

namespace {

using Clock = std::chrono::steady_clock;
using Json = rapidjson::Value;

// JSON-RPC and LSP error codes.
constexpr int k_parse_error = -32700;
constexpr int k_invalid_request = -32600;
constexpr int k_method_not_found = -32601;
constexpr int k_invalid_params = -32602;
constexpr int k_internal_error = -32603;
constexpr int k_server_not_initialized = -32002;
constexpr int k_request_cancelled = -32800;
constexpr int k_request_failed = -32803;

// Priorities in the pools: requests, then the statements on screen or just
// edited, then the rest.
constexpr int k_priority_request = 100;
constexpr int k_priority_hot = 50;
constexpr int k_priority_cold = 10;

constexpr size_t k_chunk_bytes = 256u << 10;
constexpr size_t k_chunk_statements = 512;
constexpr size_t k_hot_chunk_statements = 32;
constexpr auto k_publish_interval = std::chrono::milliseconds(500);
constexpr auto k_progress_interval = std::chrono::milliseconds(250);

std::string raw_json(const Json &v) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  v.Accept(w);
  return {buf.GetString(), buf.GetSize()};
}

const Json *member(const Json &v, const char *name) {
  if (!v.IsObject()) return nullptr;
  const auto it = v.FindMember(name);
  return it == v.MemberEnd() ? nullptr : &it->value;
}

const Json *path(const Json &v, std::initializer_list<const char *> names) {
  const Json *cur = &v;
  for (const char *n : names) {
    cur = member(*cur, n);
    if (cur == nullptr) return nullptr;
  }
  return cur;
}

std::optional<std::string> string_at(const Json &v, std::initializer_list<const char *> names) {
  const Json *x = path(v, names);
  if (x == nullptr || !x->IsString()) return std::nullopt;
  return std::string(x->GetString(), x->GetStringLength());
}

// sqlMode, serverVersion and delimiter, as initializationOptions and
// mariadb/documentContext give them.
void read_index_options(const Json &v, Index_options &o) {
  if (const auto mode = string_at(v, {"sqlMode"})) o.sql_mode = normalize_sql_mode(*mode);
  if (const auto s = string_at(v, {"serverVersion"})) {
    // An id the parser refuses is ignored like a string that is no version.
    if (const auto id = parse_server_version(*s); id != 0 && version_id_supported(id)) {
      o.version_id = id;
    }
  }
  if (const auto d = string_at(v, {"delimiter"}); d && !d->empty()) o.delimiter = *d;
}

void write_uints(Json_writer &w, std::span<const uint32_t> data) {
  w.StartArray();
  for (uint32_t x : data) w.Uint(x);
  w.EndArray();
}

std::optional<Position> position_of(const Json *v) {
  if (v == nullptr) return std::nullopt;
  const Json *l = member(*v, "line");
  const Json *c = member(*v, "character");
  if (l == nullptr || c == nullptr || !l->IsUint() || !c->IsUint()) return std::nullopt;
  return Position{l->GetUint(), c->GetUint()};
}

// A check pass: the unchecked statements of one snapshot, checked a chunk
// per job.  Each job takes the statements in the hot range first (the
// viewport, the edit), then the next ones in text order, and queues the next
// job itself, so a change of the hot range takes effect within one chunk and
// other work interleaves.
struct Check_pass {
  std::shared_ptr<const Snapshot> snapshot;
  std::mutex mutex;
  std::vector<size_t> todo;    // statement indexes, ascending
  std::vector<uint8_t> taken;  // per todo entry
  size_t cursor = 0;           // first todo entry that may be untaken
  size_t left = 0;             // todo entries not taken
  unsigned running = 0;        // jobs checking a chunk
  size_t hot_begin = 0, hot_end = 0;  // statement indexes
  std::atomic<bool> cancelled{false};

  // Up to a chunk of statements; `hot` tells whether they came from the
  // hot range.
  std::vector<size_t> take(bool *hot) {
    std::vector<size_t> out;
    const auto &stmts = snapshot->index->statements();
    std::lock_guard lock(mutex);
    if (hot_begin < hot_end) {
      auto it = std::lower_bound(todo.begin(), todo.end(), hot_begin);
      for (; it != todo.end() && *it < hot_end && out.size() < k_hot_chunk_statements; ++it) {
        const size_t k = static_cast<size_t>(it - todo.begin());
        if (taken[k]) continue;
        taken[k] = 1;
        out.push_back(*it);
      }
    }
    *hot = !out.empty();
    size_t bytes = 0;
    while (out.empty() || (!*hot && bytes < k_chunk_bytes && out.size() < k_chunk_statements)) {
      while (cursor < todo.size() && taken[cursor]) ++cursor;
      if (cursor == todo.size()) break;
      taken[cursor] = 1;
      const size_t i = todo[cursor];
      bytes += stmts[i].end - stmts[i].begin;
      out.push_back(i);
    }
    left -= out.size();
    if (!out.empty()) ++running;
    return out;
  }
};

// A request the server refuses (RequestFailed), with the reason.
struct Request_failed : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Message {
  std::string body;
  rapidjson::Document json;
};

struct Token_cache {
  std::mutex mutex;
  uint64_t next_id = 1;
  std::string result_id;
  std::vector<uint32_t> data;
};

struct Outline_cache {
  std::mutex mutex;
  uint64_t generation = 0;
  std::shared_ptr<const Outline> outline;
  bool rebuilding = false;  // a job is folding a newer snapshot
};

// Documents larger than this get their first outline in the background:
// a completion does not wait for it.
constexpr size_t k_sync_outline_limit = 4u << 20;

// How long a mariadb/dbCall may take the client (--mcp): a tool call can
// wait on a session the editor is running a script on.
constexpr std::chrono::seconds k_db_call_timeout{120};

struct Open_document {
  std::string uri;
  Document doc;
  std::set<std::shared_ptr<Check_pass>> passes;
  uint64_t check_token = 0;  // the debounce timer that may start a pass
  bool publish_scheduled = false;
  Clock::time_point last_progress{};
  std::optional<std::pair<uint32_t, uint32_t>> viewport;  // lines
  std::optional<uint32_t> focus;                          // offset of the last edit
  std::string progress_token;  // workDoneProgress token, when one is open
  // none: no token, or the client has not accepted it yet.
  enum class Progress : uint8_t { none, accepted, begun } progress = Progress::none;
  std::shared_ptr<Token_cache> tokens = std::make_shared<Token_cache>();
  std::shared_ptr<Outline_cache> outline = std::make_shared<Outline_cache>();
  // mariadb/documentContext: the connection, the schema, and whether the
  // client set the connection, sql_mode and version itself (else the
  // default connection and the server's apply).
  std::shared_ptr<Live_catalog> live;
  std::string schema_hint;
  bool explicit_connection = false;
  bool explicit_mode = false;
  bool explicit_version = false;
  bool explicit_delimiter = false;

  Open_document(std::string u, std::string text, int64_t version, Index_options o)
      : uri(std::move(u)), doc(std::move(text), version, std::move(o)) {}
};

// The messages received, and the reader thread's state, shared with the
// reader so that it may outlive the server when it is blocked reading.
struct Inbox {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::function<void()>> events;
  bool closed = false;  // the server has stopped: drop new events
  bool eof = false;     // the input has ended

  std::mutex cancel_mutex;
  std::set<std::string> cancelled;  // raw JSON ids

  void post(std::function<void()> f) {
    {
      std::lock_guard lock(mutex);
      if (closed) return;
      events.push_back(std::move(f));
    }
    cv.notify_one();
  }
};

}  // namespace

unsigned long parse_server_version(std::string_view v) {
  unsigned long parts[3] = {0, 0, 0};
  size_t n = 0, i = 0;
  while (n < 3 && i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) {
    unsigned long x = 0;
    while (i < v.size() && std::isdigit(static_cast<unsigned char>(v[i]))) x = x * 10 + (v[i++] - '0');
    parts[n++] = x;
    if (i < v.size() && v[i] == '.') ++i;
    else break;
  }
  if (n == 1 && parts[0] >= 10000) return parts[0];  // already an id
  if (n < 2 || parts[0] == 0) return 0;
  return parts[0] * 10000 + parts[1] * 100 + parts[2];
}

struct Server::Impl {
  Impl(Input &in, Output &out, Server_options o)
      : options(std::move(o)), input(in), writer(out), inbox(std::make_shared<Inbox>()) {
    secrets.shell_bin_dir = options.shell_bin_dir;
  }

  Server_options options;
  Input &input;
  Writer writer;
  std::shared_ptr<Inbox> inbox;
  std::mutex log_mutex;

  // Loop thread state.
  std::multimap<Clock::time_point, std::function<void()>> timers;
  std::unordered_map<std::string, std::unique_ptr<Open_document>> documents;
  bool initialized = false;
  bool shutdown = false;
  bool exit = false;
  bool client_progress = false;  // window.workDoneProgress
  uint64_t next_request = 1;     // ids of our requests to the client

  // Catalogs (loop thread).
  std::vector<std::string> catalog_entries;  // initializationOptions.catalogFiles
  std::string root_path;                     // the workspace root, for relative entries
  std::shared_ptr<const Workspace_catalog> workspace;
  uint64_t workspace_generation = 0;
  std::string default_connection;            // initializationOptions.connection
  Secret_store_options secrets;
  std::chrono::seconds live_interval{0};
  std::set<std::string> reported;            // connection errors shown once
  bool snippets = false;                     // the client takes snippets
  bool upper_keywords = true;
  size_t line_width = 80;                    // lineWidth

  // Destroyed first: their running jobs may still post and send.
  std::unique_ptr<Parser_pool> parse_pool;
  std::unique_ptr<Parser_pool> request_pool;
  // Each has a thread that posts to the inbox; joined before the pools go.
  std::map<std::string, std::shared_ptr<Live_catalog>> live;

  // What the client answered a request of the server's.
  struct Client_reply {
    bool ok = false;
    std::string result;   // the result, raw JSON
    std::string message;  // the error's message
    bool permanent = false;  // error.data.permanent: another ask would fail alike
  };
  // The requests sent to the client and not answered yet, by raw id.  Live
  // catalog threads add and wait; the loop thread answers.
  std::mutex pending_mutex;
  std::map<std::string, std::shared_ptr<std::promise<Client_reply>>> pending;
  uint64_t next_client_request = 1;
  bool pending_closed = false;

  // ---- output, from any thread

  const Clock::time_point started = Clock::now();

  void log(const std::string &line) {
    if (options.log == nullptr) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
    std::lock_guard lock(log_mutex);
    *options.log << ms.count() << " ms: " << line << std::endl;
  }

  void send(const std::function<void(Json_writer &)> &body) {
    rapidjson::StringBuffer buf;
    Json_writer w(buf);
    w.StartObject();
    w.Key("jsonrpc");
    w.String("2.0");
    body(w);
    w.EndObject();
    writer.send(std::string(buf.GetString(), buf.GetSize()));
  }

  void reply(const std::string &id, const std::function<void(Json_writer &)> &result) {
    finished(id);
    send([&](Json_writer &w) {
      w.Key("id");
      w.RawValue(id.c_str(), id.size(), rapidjson::kObjectType);
      w.Key("result");
      result(w);
    });
  }

  void reply_null(const std::string &id) {
    reply(id, [](Json_writer &w) { w.Null(); });
  }

  void reply_error(const std::string &id, int code, const std::string &message) {
    finished(id);
    send([&](Json_writer &w) {
      w.Key("id");
      if (id.empty()) {
        w.Null();
      } else {
        w.RawValue(id.c_str(), id.size(), rapidjson::kObjectType);
      }
      w.Key("error");
      w.StartObject();
      w.Key("code");
      w.Int(code);
      w.Key("message");
      w.String(message.c_str());
      w.EndObject();
    });
  }

  void notify(const char *method, const std::function<void(Json_writer &)> &params) {
    send([&](Json_writer &w) {
      w.Key("method");
      w.String(method);
      w.Key("params");
      params(w);
    });
  }

  bool is_cancelled(const std::string &id) {
    std::lock_guard lock(inbox->cancel_mutex);
    return inbox->cancelled.count(id) != 0;
  }

  void finished(const std::string &id) {
    std::lock_guard lock(inbox->cancel_mutex);
    inbox->cancelled.erase(id);
  }

  // Runs `compute` on the request pool and replies with what it writes.
  // A `compute` returning bool may return false: it handed the request on
  // (detail_request) and there is no reply yet.
  template <typename F>
  void run_request(const std::string &id, F compute) {
    request_pool->submit(k_priority_request, [this, id, compute = std::move(compute)](Parser_cache &cache) mutable {
      if (is_cancelled(id)) {
        reply_error(id, k_request_cancelled, "request cancelled");
        return;
      }
      try {
        rapidjson::StringBuffer buf;
        Json_writer w(buf);
        if constexpr (std::is_same_v<std::invoke_result_t<F &, Json_writer &, Parser_cache &>, bool>) {
          if (!compute(w, cache)) return;
        } else {
          compute(w, cache);
        }
        if (is_cancelled(id)) {
          reply_error(id, k_request_cancelled, "request cancelled");
          return;
        }
        const std::string result(buf.GetString(), buf.GetSize());
        reply(id, [&](Json_writer &out) {
          out.RawValue(result.c_str(), result.size(), rapidjson::kObjectType);
        });
      } catch (const Request_failed &e) {
        reply_error(id, k_request_failed, e.what());
      } catch (const std::exception &e) {
        reply_error(id, k_internal_error, e.what());
      }
    });
  }

  // ---- the reader thread

  static void read_loop(Input &in, std::shared_ptr<Inbox> inbox, Impl *self) {
    Message_reader reader(in);
    for (;;) {
      // Parsed in situ: the message's strings stay in its buffer.
      auto msg = std::make_shared<Message>();
      if (!reader.next(&msg->body)) break;
      // `self` is touched on the loop thread only: this thread may outlive
      // the server (see run()), and a post after it stopped is dropped.
      const size_t bytes = msg->body.size();
      msg->json.ParseInsitu(msg->body.data());
      if (!msg->json.HasParseError() && msg->json.IsObject()) {
        // $/cancelRequest acts at once, even while the loop is busy.
        const auto method = string_at(msg->json, {"method"});
        if (method && *method == "$/cancelRequest") {
          if (const Json *id = path(msg->json, {"params", "id"})) {
            std::lock_guard lock(inbox->cancel_mutex);
            if (inbox->cancelled.size() > 10000) inbox->cancelled.clear();
            inbox->cancelled.insert(raw_json(*id));
          }
          continue;
        }
      }
      inbox->post([self, msg, bytes] {
        if (bytes > (1u << 20)) self->log("read a message of " + std::to_string(bytes) + " bytes");
        self->on_message(msg->json);
      });
    }
    std::lock_guard lock(inbox->mutex);
    inbox->eof = true;
    inbox->cv.notify_all();
  }

  // ---- the event loop

  int run() {
    parse_pool = std::make_unique<Parser_pool>(options.threads);
    request_pool = std::make_unique<Parser_pool>(
        std::max(2u, std::min(4u, parse_pool->max_threads())));
    std::thread reader(read_loop, std::ref(input), inbox, this);

    while (!exit) {
      std::function<void()> event;
      {
        std::unique_lock lock(inbox->mutex);
        const auto ready = [&] { return !inbox->events.empty() || inbox->eof; };
        if (timers.empty()) {
          inbox->cv.wait(lock, ready);
        } else {
          inbox->cv.wait_until(lock, timers.begin()->first, ready);
        }
        if (!inbox->events.empty()) {
          event = std::move(inbox->events.front());
          inbox->events.pop_front();
        } else if (inbox->eof) {
          break;  // the input ended without exit: nobody is listening
        }
      }
      if (event) {
        try {
          event();
        } catch (const std::exception &e) {
          log(std::string("error: ") + e.what());
        }
        continue;
      }
      const auto now = Clock::now();
      while (!timers.empty() && timers.begin()->first <= now) {
        auto f = std::move(timers.begin()->second);
        timers.erase(timers.begin());
        f();
      }
    }

    // Dropped outside the lock and after close_pending(): an event, or a
    // request waiting for details in `timers`, may hold the last reference
    // to a live catalog, whose thread is joined then.
    std::deque<std::function<void()>> dropped;
    {
      std::lock_guard lock(inbox->mutex);
      inbox->closed = true;
      dropped.swap(inbox->events);
    }
    for (auto &[uri, d] : documents) cancel_passes(*d);
    // A catalog thread waiting on the client is let go before it is joined.
    close_pending();
    dropped.clear();
    timers.clear();
    for (auto &[uri, d] : documents) d->live.reset();
    live.clear();
    request_pool.reset();
    parse_pool.reset();
    writer.flush();
    bool eof;
    {
      std::lock_guard lock(inbox->mutex);
      eof = inbox->eof;
    }
    // A reader blocked on input that never ends cannot be joined; it holds
    // only the inbox and the stream.
    if (eof) {
      reader.join();
    } else {
      reader.detach();
    }
    return shutdown ? 0 : 1;
  }

  void after(std::chrono::milliseconds delay, std::function<void()> f) {
    timers.emplace(Clock::now() + delay, std::move(f));
  }

  void on_message(const rapidjson::Document &msg) {
    if (msg.HasParseError() || !msg.IsObject()) {
      reply_error("", k_parse_error, "invalid JSON");
      return;
    }
    const Json *id_value = member(msg, "id");
    const std::string id = id_value != nullptr ? raw_json(*id_value) : "";
    const auto method = string_at(msg, {"method"});
    if (!method) {
      if (id_value != nullptr) on_response(msg, id);
      return;
    }
    static const Json k_empty(rapidjson::kObjectType);
    const Json *params_ptr = member(msg, "params");
    const Json &params = params_ptr != nullptr ? *params_ptr : k_empty;
    const bool request = id_value != nullptr;

    if (*method == "initialize") return on_initialize(id, params);
    if (*method == "exit") {
      exit = true;
      return;
    }
    if (!initialized) {
      if (request) reply_error(id, k_server_not_initialized, "not initialized");
      return;
    }
    if (*method == "shutdown") {
      shutdown = true;
      return reply_null(id);
    }
    if (shutdown && request) return reply_error(id, k_invalid_request, "shutting down");

    try {
      if (*method == "initialized") return;
      if (*method == "textDocument/didOpen") return on_did_open(params);
      if (*method == "textDocument/didSave") return on_did_save(params);
      if (*method == "workspace/didChangeConfiguration") return on_did_change_configuration(params);
      if (*method == "workspace/symbol") return on_workspace_symbol(id, params);
      if (*method == "mariadb/refreshCatalog") return on_refresh_catalog(id, params);
      if (*method == "textDocument/didChange") return on_did_change(params);
      if (*method == "textDocument/didClose") return on_did_close(params);
      if (*method == "mariadb/documentContext") {
        on_document_context(params);
        if (request) reply_null(id);
        return;
      }
      if (*method == "mariadb/viewport") {
        on_viewport(params);
        if (request) reply_null(id);
        return;
      }
      if (!request) return;  // other notifications are ignored

      Open_document *d = document(params);
      if (*method == "textDocument/documentSymbol") return on_document_symbol(id, d);
      if (*method == "mariadb/outline") return on_outline(id, d);
      if (*method == "textDocument/foldingRange") return on_folding_range(id, d);
      if (*method == "textDocument/selectionRange") return on_selection_range(id, d, params);
      if (*method == "textDocument/semanticTokens/full") return on_tokens_full(id, d, nullptr);
      if (*method == "textDocument/semanticTokens/full/delta") {
        const auto prev = string_at(params, {"previousResultId"});
        return on_tokens_full(id, d, prev ? &*prev : nullptr);
      }
      if (*method == "textDocument/semanticTokens/range") return on_tokens_range(id, d, params);
      if (*method == "textDocument/completion") return on_completion(id, d, params);
      if (*method == "textDocument/hover") return on_hover(id, d, params);
      if (*method == "textDocument/definition") return on_definition(id, d, params);
      if (*method == "textDocument/references") return on_references(id, d, params);
      if (*method == "textDocument/documentHighlight") return on_highlight(id, d, params);
      if (*method == "textDocument/prepareRename") return on_prepare_rename(id, d, params);
      if (*method == "textDocument/rename") return on_rename(id, d, params);
      if (*method == "textDocument/signatureHelp") return on_signature_help(id, d, params);
      if (*method == "textDocument/formatting") return on_formatting(id, d, params, false);
      if (*method == "textDocument/rangeFormatting") return on_formatting(id, d, params, true);
      reply_error(id, k_method_not_found, "unknown method " + *method);
    } catch (const std::exception &e) {
      if (request) reply_error(id, k_internal_error, e.what());
      log(std::string("error in ") + *method + ": " + e.what());
    }
  }

  Open_document *document(const Json &params) {
    const auto uri = string_at(params, {"textDocument", "uri"});
    if (!uri) return nullptr;
    const auto it = documents.find(*uri);
    return it == documents.end() ? nullptr : it->second.get();
  }

  void on_initialize(const std::string &id, const Json &params) {
    if (initialized) return reply_error(id, k_invalid_request, "initialize sent twice");
    if (const Json *p = path(params, {"capabilities", "window", "workDoneProgress"})) {
      client_progress = p->IsBool() && p->GetBool();
    }
    if (const Json *p = path(params, {"capabilities", "textDocument", "completion",
                                      "completionItem", "snippetSupport"})) {
      snippets = p->IsBool() && p->GetBool();
    }
    // The workspace root: the first folder, else rootUri, else rootPath.
    if (const Json *f = member(params, "workspaceFolders"); f != nullptr && f->IsArray() &&
                                                             !f->Empty()) {
      if (const auto u = string_at((*f)[0], {"uri"})) root_path = file_uri_to_path(*u);
    }
    if (root_path.empty()) {
      if (const auto u = string_at(params, {"rootUri"})) root_path = file_uri_to_path(*u);
    }
    if (root_path.empty()) {
      if (const auto p = string_at(params, {"rootPath"})) root_path = *p;
    }
    if (const Json *o = member(params, "initializationOptions")) apply_settings(*o);
    initialized = true;
    // The keyword tables are built on first use; build them now, off the
    // loop, so the first completion does not pay for it.
    request_pool->submit(k_priority_cold, [](Parser_cache &) {
      (void)keyword_spellings(0);
      (void)needs_quoting("x");
    });
    rebuild_workspace();
    if (!default_connection.empty()) live_catalog(default_connection);
    reply(id, [](Json_writer &w) {
      w.StartObject();
      w.Key("capabilities");
      w.StartObject();
      w.Key("positionEncoding");
      w.String("utf-16");
      w.Key("textDocumentSync");
      w.StartObject();
      w.Key("openClose");
      w.Bool(true);
      w.Key("change");
      w.Int(2);  // incremental
      w.EndObject();
      w.Key("documentSymbolProvider");
      w.Bool(true);
      w.Key("foldingRangeProvider");
      w.Bool(true);
      w.Key("selectionRangeProvider");
      w.Bool(true);
      w.Key("completionProvider");
      w.StartObject();
      w.Key("triggerCharacters");
      w.StartArray();
      w.String(".");
      w.String("`");
      w.EndArray();
      w.Key("resolveProvider");
      w.Bool(false);
      w.EndObject();
      w.Key("hoverProvider");
      w.Bool(true);
      w.Key("definitionProvider");
      w.Bool(true);
      w.Key("referencesProvider");
      w.Bool(true);
      w.Key("documentHighlightProvider");
      w.Bool(true);
      w.Key("renameProvider");
      w.StartObject();
      w.Key("prepareProvider");
      w.Bool(true);
      w.EndObject();
      w.Key("signatureHelpProvider");
      w.StartObject();
      w.Key("triggerCharacters");
      w.StartArray();
      w.String("(");
      w.String(",");
      w.EndArray();
      w.EndObject();
      w.Key("workspaceSymbolProvider");
      w.Bool(true);
      w.Key("documentFormattingProvider");
      w.Bool(true);
      w.Key("documentRangeFormattingProvider");
      w.Bool(true);
      w.Key("semanticTokensProvider");
      w.StartObject();
      w.Key("legend");
      w.StartObject();
      w.Key("tokenTypes");
      w.StartArray();
      for (const char *t : semantic_token_types()) w.String(t);
      w.EndArray();
      w.Key("tokenModifiers");
      w.StartArray();
      for (const char *t : semantic_token_modifiers()) w.String(t);
      w.EndArray();
      w.EndObject();
      w.Key("range");
      w.Bool(true);
      w.Key("full");
      w.StartObject();
      w.Key("delta");
      w.Bool(true);
      w.EndObject();
      w.EndObject();
      w.EndObject();
      w.Key("serverInfo");
      w.StartObject();
      w.Key("name");
      w.String("mariadb-lsp");
      w.Key("version");
      w.String(MDBLS_VERSION);
      w.EndObject();
      w.EndObject();
    });
  }

  void apply_settings(const Json &o) {
    read_index_options(o, options.defaults);
    auto uint_at = [&](const char *name) -> std::optional<uint64_t> {
      const Json *x = member(o, name);
      return x != nullptr && x->IsUint64() ? std::optional<uint64_t>(x->GetUint64()) : std::nullopt;
    };
    if (const auto v = uint_at("debounceMs")) options.debounce = std::chrono::milliseconds(*v);
    if (const auto v = uint_at("maxDiagnostics")) options.max_diagnostics = *v;
    if (const auto v = uint_at("semanticTokensFullLimit")) options.full_semantic_tokens_limit = *v;
    if (const auto v = uint_at("progressThreshold")) options.progress_threshold = *v;
    if (const auto v = uint_at("catalogRefreshSeconds")) live_interval = std::chrono::seconds(*v);
    if (const auto v = uint_at("lineWidth")) line_width = *v;
    if (const Json *f = member(o, "catalogFiles"); f != nullptr && f->IsArray()) {
      catalog_entries.clear();
      for (const auto &e : f->GetArray()) {
        if (e.IsString()) catalog_entries.emplace_back(e.GetString(), e.GetStringLength());
      }
    }
    if (const auto v = string_at(o, {"connection"})) default_connection = *v;
    if (const auto v = string_at(o, {"shellBinary"}); v && !v->empty()) {
      secrets.shell_bin_dir = std::filesystem::path(*v).parent_path().string();
    }
    if (const auto v = string_at(o, {"shellBinDir"}); v && !v->empty()) secrets.shell_bin_dir = *v;
    if (const auto v = string_at(o, {"credentialStoreHelper"})) secrets.helper = *v;
    if (const auto v = string_at(o, {"keywordCase"})) upper_keywords = *v != "lower";
  }

  // Sends a request to the client; its answer comes through the future.
  // `id_out`, when given, gets the request's raw id, for forget_request().
  std::future<Client_reply> client_request(const char *method, const std::string &params_json,
                                           std::string *id_out = nullptr) {
    auto promise = std::make_shared<std::promise<Client_reply>>();
    auto future = promise->get_future();
    std::string id;
    {
      std::lock_guard lock(pending_mutex);
      if (pending_closed) {
        promise->set_value({false, "", "the server is shutting down", true});
        return future;
      }
      id = "\"mdbls-" + std::to_string(next_client_request++) + "\"";
      pending.emplace(id, promise);
    }
    if (id_out != nullptr) *id_out = id;
    send([&](Json_writer &w) {
      w.Key("id");
      w.RawValue(id.c_str(), id.size(), rapidjson::kStringType);
      w.Key("method");
      w.String(method);
      w.Key("params");
      w.RawValue(params_json.c_str(), params_json.size(), rapidjson::kObjectType);
    });
    return future;
  }

  // Stops waiting for the answer to request `id`: one that comes later is
  // dropped.
  void forget_request(const std::string &id) {
    std::lock_guard lock(pending_mutex);
    pending.erase(id);
  }

  // Answers every request still waiting: the server is going down.
  void close_pending() {
    std::lock_guard lock(pending_mutex);
    pending_closed = true;
    for (auto &[id, promise] : pending) {
      promise->set_value({false, "", "the server is shutting down", true});
    }
    pending.clear();
  }

  void on_response(const Json &msg, const std::string &id) {
    std::shared_ptr<std::promise<Client_reply>> promise;
    {
      std::lock_guard lock(pending_mutex);
      if (const auto it = pending.find(id); it != pending.end()) {
        promise = it->second;
        pending.erase(it);
      }
    }
    if (promise) {
      Client_reply r;
      if (const Json *err = member(msg, "error"); err != nullptr && !err->IsNull()) {
        r.message = string_at(*err, {"message"}).value_or("error");
        if (const Json *p = path(*err, {"data", "permanent"})) r.permanent = p->IsTrue();
      } else {
        r.ok = true;
        const Json *result = member(msg, "result");
        r.result = result == nullptr ? "null" : raw_json(*result);
      }
      promise->set_value(std::move(r));
      return;
    }
    // The client accepted a workDoneProgress token: begin reporting.
    for (auto &[uri, d] : documents) {
      if (d->progress == Open_document::Progress::none && id == d->progress_token) {
        d->progress = Open_document::Progress::accepted;
        progress(*d, true);
      }
    }
  }

  // ---- documents

  void on_did_open(const Json &params) {
    const auto uri = string_at(params, {"textDocument", "uri"});
    auto text = string_at(params, {"textDocument", "text"});
    const Json *version = path(params, {"textDocument", "version"});
    if (!uri || !text) return;
    if (auto it = documents.find(*uri); it != documents.end()) {
      cancel_passes(*it->second);
      end_progress(*it->second);
    }
    log("didOpen " + *uri + ": indexing");
    const size_t bytes = text->size();
    auto d = std::make_unique<Open_document>(*uri, std::move(*text),
                                             version != nullptr && version->IsInt64() ? version->GetInt64() : 0,
                                             options.defaults);
    auto &ref = *d;
    if (!default_connection.empty()) {
      ref.live = live_catalog(default_connection);
      Index_options o = ref.doc.options();
      apply_server_options(ref, o);
      set_options(ref, o, false);
    }
    documents[*uri] = std::move(d);
    log("open " + *uri + " (" + std::to_string(bytes) + " bytes, " +
        std::to_string(ref.doc.index().statements().size()) + " statements)");
    start_checks(ref);
  }

  void on_did_change(const Json &params) {
    Open_document *d = document(params);
    const Json *changes = member(params, "contentChanges");
    if (d == nullptr || changes == nullptr || !changes->IsArray()) return;
    for (const auto &c : changes->GetArray()) {
      const Json *t = member(c, "text");
      if (t == nullptr || !t->IsString()) continue;
      const std::string_view text(t->GetString(), t->GetStringLength());
      const Json *range = member(c, "range");
      if (range == nullptr) {
        cancel_passes(*d);
        d->doc.change(std::nullopt, text);
        d->focus.reset();
        continue;
      }
      const auto a = position_of(member(*range, "start"));
      const auto b = position_of(member(*range, "end"));
      if (!a || !b) continue;
      d->focus = d->doc.change(std::make_pair(*a, *b), text);
    }
    if (const Json *v = path(params, {"textDocument", "version"}); v != nullptr && v->IsInt64()) {
      d->doc.set_version(v->GetInt64());
    }
    schedule_checks(*d);
  }

  void on_did_close(const Json &params) {
    Open_document *d = document(params);
    if (d == nullptr) return;
    cancel_passes(*d);
    const std::string uri = d->uri;
    end_progress(*d);
    documents.erase(uri);
    notify("textDocument/publishDiagnostics", [&](Json_writer &w) {
      w.StartObject();
      w.Key("uri");
      w.String(uri.c_str());
      w.Key("diagnostics");
      w.StartArray();
      w.EndArray();
      w.EndObject();
    });
  }

  void on_document_context(const Json &params) {
    Open_document *d = document(params);
    if (d == nullptr) return;
    if (string_at(params, {"sqlMode"})) d->explicit_mode = true;
    if (string_at(params, {"serverVersion"})) d->explicit_version = true;
    if (const auto v = string_at(params, {"delimiter"}); v && !v->empty()) d->explicit_delimiter = true;
    if (const Json *c = member(params, "connection")) {
      d->explicit_connection = true;
      if (c->IsString() && c->GetStringLength() > 0) {
        d->live = live_catalog(std::string(c->GetString(), c->GetStringLength()));
      } else {
        d->live.reset();
      }
    }
    if (const auto schema = string_at(params, {"currentSchema"})) d->schema_hint = *schema;
    Index_options o = d->doc.options();
    read_index_options(params, o);
    apply_server_options(*d, o);
    set_options(*d, o, true);
  }

  // New options for `d`: indexed again, and checked again when `check`.
  void set_options(Open_document &d, const Index_options &o, bool check) {
    if (o == d.doc.options()) return;
    cancel_passes(d);
    d.doc.set_options(o);
    if (check) start_checks(d);
  }

  // The sql_mode and version of the document's connection, where the
  // client did not set them; the defaults when it has no connection, or
  // none read yet (so a connection taken away takes its settings along).
  void apply_server_options(const Open_document &d, Index_options &o) {
    const auto snap = d.live ? d.live->snapshot() : nullptr;
    if (!d.explicit_mode) {
      o.sql_mode = snap ? normalize_sql_mode(snap->server.sql_mode) : options.defaults.sql_mode;
    }
    if (!d.explicit_version) {
      const bool known = snap && snap->server.version_id != 0 &&
                         version_id_supported(snap->server.version_id);
      o.version_id = known ? snap->server.version_id : options.defaults.version_id;
    }
  }

  // ---- catalogs

  std::shared_ptr<Live_catalog> live_catalog(const std::string &uri) {
    const auto normalized = normalize_connection_uri(uri);
    if (!normalized) {
      // Once per URI (every open of a document would repeat it otherwise),
      // and not shown as given: it may hold a password.
      if (reported.insert("\n" + uri).second) {
        notify("window/showMessage", [&](Json_writer &w) {
          w.StartObject();
          w.Key("type");
          w.Int(2);
          w.Key("message");
          w.String("MariaDB: the document's connection is not a connection URI");
          w.EndObject();
        });
      }
      return nullptr;
    }
    const std::string &key = *normalized;
    if (const auto it = live.find(key); it != live.end()) return it->second;
    Live_catalog_options o;
    o.secrets = secrets;
    if (options.mcp) {
      o.fetch = mcp_fetcher();
      o.fetch_details = mcp_detail_fetcher();
    }
    o.refresh_interval = live_interval;
    o.log = [this](const std::string &line) { log(line); };
    auto box = inbox;
    auto lc = std::make_shared<Live_catalog>(uri, o, [this, box, key] {
      box->post([this, key] { on_live_update(key); });
    });
    live.emplace(key, lc);
    return lc;
  }

  // Reads a catalog through the client's MCP server: each db.* tool call is
  // a mariadb/dbCall request, answered by the client.  Runs on the live
  // catalog's thread; the loop thread hands the answers over.
  // Calls a db.* tool on `uri` through the client; for as long as `uri`
  // and `cancelled` live.
  Tool_caller mcp_tool_caller(const std::string &uri, const std::function<bool()> &cancelled) {
    return [this, &uri, &cancelled](std::string_view tool, const std::string &arguments,
                                    rapidjson::Document *out) -> std::optional<Tool_failure> {
      rapidjson::StringBuffer buf;
      Json_writer w(buf);
      w.StartObject();
      w.Key("connection");
      w.String(uri.c_str());
      w.Key("tool");
      w.String(tool.data(), static_cast<rapidjson::SizeType>(tool.size()));
      w.Key("arguments");
      w.RawValue(arguments.c_str(), arguments.size(), rapidjson::kObjectType);
      w.EndObject();
      std::string id;
      auto future = client_request("mariadb/dbCall", std::string(buf.GetString(), buf.GetSize()), &id);
      const auto deadline = Clock::now() + k_db_call_timeout;
      while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        if (cancelled()) {
          forget_request(id);
          return Tool_failure{"cancelled", false};
        }
        if (Clock::now() > deadline) {
          forget_request(id);
          return Tool_failure{"the client did not answer within " +
                                  std::to_string(k_db_call_timeout.count()) + " s",
                              false};
        }
      }
      const Client_reply reply = future.get();
      if (!reply.ok) return Tool_failure{reply.message, reply.permanent};
      out->Parse(reply.result.c_str(), reply.result.size());
      if (out->HasParseError()) return Tool_failure{"the client's answer is not JSON", false};
      return std::nullopt;
    };
  }

  Catalog_fetcher mcp_fetcher() {
    return [this](const std::string &uri, std::string *error, bool *permanent,
                  const std::function<bool()> &cancelled) {
      Mcp_catalog_options mo;
      mo.cancelled = cancelled;
      return fetch_mcp_catalog(mcp_tool_caller(uri, cancelled), error, permanent, mo);
    };
  }

  // Reads the details of stubs through the client, as mcp_fetcher reads
  // the rest.
  Detail_fetcher mcp_detail_fetcher() {
    return [this](const std::string &uri, const std::vector<Object_key> &objects,
                  std::string *error, const std::function<bool()> &cancelled) {
      Mcp_catalog_options mo;
      mo.cancelled = cancelled;
      bool permanent = false;
      return fetch_mcp_details(mcp_tool_caller(uri, cancelled), objects, error, &permanent, mo);
    };
  }

  void on_live_update(const std::string &key) {
    const auto it = live.find(key);
    if (it == live.end()) return;
    const auto &lc = it->second;
    const std::string error = lc->error();
    if (!error.empty()) {
      if (reported.insert(key + "\n" + error).second) {
        notify("window/showMessage", [&](Json_writer &w) {
          w.StartObject();
          w.Key("type");
          w.Int(2);  // warning
          w.Key("message");
          const std::string msg = "MariaDB: " + key + ": " + error;
          w.String(msg.c_str());
          w.EndObject();
        });
      }
      return;
    }
    for (auto &[uri, d] : documents) {
      if (d->live != lc) continue;
      Index_options o = d->doc.options();
      apply_server_options(*d, o);
      set_options(*d, o, true);
    }
  }

  // Reads the catalog files again, in the background.
  void rebuild_workspace() {
    const uint64_t gen = ++workspace_generation;
    if (catalog_entries.empty()) {
      workspace.reset();
      return;
    }
    request_pool->submit(k_priority_cold, [this, gen, entries = catalog_entries, root = root_path,
                                           defaults = options.defaults](Parser_cache &) {
      std::vector<std::string> errors;
      const auto paths = expand_catalog_paths(entries, root, &errors);
      auto ws = std::make_shared<Workspace_catalog>(
          build_workspace_catalog(paths, *parse_pool, defaults, k_priority_cold));
      ws->errors.insert(ws->errors.begin(), errors.begin(), errors.end());
      inbox->post([this, gen, ws] {
        if (gen != workspace_generation) return;
        workspace = ws;
        log("workspace catalog: " + std::to_string(ws->files.size()) + " files, " +
            std::to_string(ws->outline.applied) + " statements applied");
        for (const auto &e : ws->errors) log("workspace catalog: " + e);
      });
    });
  }

  void on_did_save(const Json &params) {
    const auto uri = string_at(params, {"textDocument", "uri"});
    if (uri && workspace && workspace->file(*uri) != nullptr) rebuild_workspace();
  }

  void on_did_change_configuration(const Json &params) {
    const Json *settings = path(params, {"settings", "mariadb"});
    if (settings == nullptr || !settings->IsObject()) return;
    const auto entries = catalog_entries;
    const auto connection = default_connection;
    const auto defaults = options.defaults;
    const auto interval = live_interval;
    apply_settings(*settings);
    if (catalog_entries != entries) rebuild_workspace();
    if (live_interval != interval) {
      for (auto &[key, lc] : live) lc->set_refresh_interval(live_interval);
    }
    const bool moved = default_connection != connection;
    if (!moved && options.defaults == defaults) return;
    // The documents the client gave no connection of their own follow the
    // default one, and every document takes the new defaults where neither
    // the client nor its server set them.
    std::shared_ptr<Live_catalog> now;
    if (moved && !default_connection.empty()) now = live_catalog(default_connection);
    for (auto &[uri, d] : documents) {
      if (moved && !d->explicit_connection) d->live = now;
      Index_options o = d->doc.options();
      if (!d->explicit_delimiter) o.delimiter = options.defaults.delimiter;
      apply_server_options(*d, o);
      set_options(*d, o, true);
    }
  }

  void on_refresh_catalog(const std::string &id, const Json &params) {
    rebuild_workspace();
    if (Open_document *d = document(params)) {
      if (d->live) d->live->refresh();
    } else {
      for (auto &[key, lc] : live) lc->refresh();
    }
    if (!id.empty()) reply_null(id);
  }

  // What a request about `d` sees; the outline is read in the job.
  struct Request_sources {
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<Outline_cache> outline;
    Catalog_sources catalogs;
  };

  Request_sources sources(Open_document &d) {
    Request_sources r;
    r.snapshot = d.doc.snapshot();
    r.outline = d.outline;
    r.catalogs.workspace = workspace;
    if (d.live) {
      r.catalogs.live = d.live->snapshot();
      r.catalogs.live_source = d.live;
    }
    r.catalogs.schema_hint = d.schema_hint;
    return r;
  }

  // The document's outline for a request job.  `fresh`: of this very
  // snapshot (definitions need its offsets).  Otherwise the last one built
  // will do, and a newer one is folded in the background.
  std::shared_ptr<const Outline> document_outline(const std::shared_ptr<const Snapshot> &keep,
                                                  const std::shared_ptr<Outline_cache> &cache,
                                                  bool fresh) {
    const Snapshot &snap = *keep;
    if (fresh) return outline_of(snap, *cache);
    std::shared_ptr<const Outline> have;
    bool schedule = false;
    {
      std::lock_guard lock(cache->mutex);
      have = cache->outline;
      if ((!have || cache->generation != snap.generation) && !cache->rebuilding &&
          (have || snap.text->size() >= k_sync_outline_limit)) {
        cache->rebuilding = true;
        schedule = true;
      }
    }
    if (!have && snap.text->size() < k_sync_outline_limit) return outline_of(snap, *cache);
    if (schedule) {
      // Queued by the loop thread: a job never submits to its own pool,
      // which may be shutting down.
      inbox->post([this, keep, cache] {
        request_pool->submit(k_priority_cold, [this, keep, cache](Parser_cache &) {
          struct Done {
            Outline_cache &c;
            ~Done() {
              std::lock_guard lock(c.mutex);
              c.rebuilding = false;
            }
          } done{*cache};
          (void)outline_of(*keep, *cache);
        });
      });
    }
    return have;
  }

  // Runs `f(sources, snapshot, cache)` as a request about `d` at the
  // position in params.
  template <typename F>
  void position_request(const std::string &id, Open_document *d, const Json &params, bool fresh,
                        F f) {
    const auto pos = position_of(member(params, "position"));
    if (d == nullptr || !pos) return reply_error(id, k_invalid_params, "position expected");
    auto src = sources(*d);
    run_request(id, [this, src = std::move(src), pos = *pos, fresh, f = std::move(f)](
                        Json_writer &w, Parser_cache &cache) mutable {
      src.catalogs.document = document_outline(src.snapshot, src.outline, fresh);
      f(w, *src.snapshot, pos, src.catalogs, cache);
    });
  }

  // A position request whose answer may need the details of live stubs:
  // `f(w, snapshot, pos, sources, cache, pass)` runs as Details_pass says.
  // Between the passes no thread waits: the request is parked until the
  // live catalog has read the details or k_details_wait has passed.
  template <typename F>
  void detail_request(const std::string &id, Open_document *d, const Json &params, F f) {
    const auto pos = position_of(member(params, "position"));
    if (d == nullptr || !pos) return reply_error(id, k_invalid_params, "position expected");
    detail_pass(id, sources(*d), *pos, std::move(f), Details_pass{});
  }

  template <typename F>
  void detail_pass(const std::string &id, Request_sources src, Position pos, F f, Details_pass pass) {
    run_request(id, [this, id, src = std::move(src), pos, f = std::move(f), pass = std::move(pass)](
                        Json_writer &w, Parser_cache &cache) mutable {
      src.catalogs.document = document_outline(src.snapshot, src.outline, false);
      f(w, *src.snapshot, pos, src.catalogs, cache, pass);
      if (pass.wanted.empty()) return true;
      // Parked by the loop thread, which owns the timers and submits the
      // final pass: a job never submits to its own pool.
      inbox->post([this, id, src = std::move(src), pos, f = std::move(f),
                   wanted = std::move(pass.wanted)]() mutable {
        const auto catalog = src.catalogs.live_source;
        // The final pass, run once: by the details or by the timer,
        // whichever comes first.  Loop thread only.
        auto resume = std::make_shared<std::function<void(bool)>>(
            [this, id, src = std::move(src), pos, f = std::move(f)](bool read) mutable {
              src.catalogs.live = src.catalogs.live_source->snapshot();
              Details_pass last;
              last.final = true;
              last.read = read;
              detail_pass(id, std::move(src), pos, std::move(f), std::move(last));
            });
        const auto finish = [](const std::shared_ptr<std::function<void(bool)>> &r, bool read) {
          if (!*r) return;
          auto run = std::move(*r);
          *r = nullptr;
          run(read);
        };
        // The catalog's thread holds it weakly: it never drops the last
        // reference, which may own the catalog itself.
        const bool asked = catalog->request_details(
            wanted, [inbox = inbox, finish, weak = std::weak_ptr(resume)] {
              inbox->post([finish, weak] {
                if (const auto r = weak.lock()) finish(r, true);
              });
            });
        if (!asked) return finish(resume, false);
        after(k_details_wait, [finish, resume] { finish(resume, false); });
      });
      return false;
    });
  }

  void on_viewport(const Json &params) {
    Open_document *d = document(params);
    const auto a = position_of(path(params, {"range", "start"}));
    const auto b = position_of(path(params, {"range", "end"}));
    if (d == nullptr || !a || !b) return;
    d->viewport = std::make_pair(a->line, b->line);
    for (const auto &pass : d->passes) set_hot_range(*d, *pass);
  }

  // ---- checking

  void schedule_checks(Open_document &d) {
    const uint64_t token = ++d.check_token;
    if (options.debounce.count() == 0) {
      start_checks(d);
      return;
    }
    const std::string uri = d.uri;
    after(options.debounce, [this, uri, token] {
      const auto it = documents.find(uri);
      if (it != documents.end() && it->second->check_token == token) start_checks(*it->second);
    });
  }

  void cancel_passes(Open_document &d) {
    for (const auto &p : d.passes) p->cancelled = true;
    d.passes.clear();
  }

  // The statements of the viewport, or around the last edit, go first.
  void set_hot_range(Open_document &d, Check_pass &pass) {
    const Snapshot &s = *pass.snapshot;
    size_t lo = 0, hi = 0;
    if (d.viewport) {
      lo = s.index->find(s.lines->line_start(d.viewport->first));
      hi = s.index->find(s.lines->line_start(d.viewport->second + 1));
      if (hi < s.index->statements().size()) ++hi;
    } else if (d.focus) {
      const size_t at = s.index->find(*d.focus);
      lo = at > 8 ? at - 8 : 0;
      hi = at + 8;
    }
    std::lock_guard lock(pass.mutex);
    pass.hot_begin = lo;
    pass.hot_end = hi;
  }

  void start_checks(Open_document &d) {
    auto todo = d.doc.take_unchecked();
    if (todo.empty()) {
      if (d.passes.empty()) publish(d);
      return;
    }
    auto pass = std::make_shared<Check_pass>();
    pass->snapshot = d.doc.snapshot();
    pass->todo = std::move(todo);
    pass->taken.assign(pass->todo.size(), 0);
    pass->left = pass->todo.size();
    set_hot_range(d, *pass);
    d.passes.insert(pass);

    size_t bytes = 0;
    for (size_t i : pass->todo) {
      const auto &st = pass->snapshot->index->statements()[i];
      bytes += st.end - st.begin;
    }
    if (bytes >= options.progress_threshold) begin_progress(d);
    const size_t jobs = std::clamp<size_t>(bytes / k_chunk_bytes, 1, parse_pool->max_threads());
    for (size_t k = 0; k < jobs; ++k) submit_chunk(d.uri, pass);
  }

  void submit_chunk(const std::string &uri, const std::shared_ptr<Check_pass> &pass) {
    bool hot_pending;
    {
      std::lock_guard lock(pass->mutex);
      hot_pending = pass->hot_begin < pass->hot_end;
    }
    parse_pool->submit(hot_pending ? k_priority_hot : k_priority_cold,
                       [this, uri, pass](Parser_cache &cache) { check_chunk(uri, pass, cache); });
  }

  // On a parser thread.
  void check_chunk(const std::string &uri, const std::shared_ptr<Check_pass> &pass,
                   Parser_cache &cache) {
    if (pass->cancelled) return;
    bool hot = false;
    const auto which = pass->take(&hot);
    if (which.empty()) return;
    const Snapshot &s = *pass->snapshot;
    std::vector<Check_result> results;
    results.reserve(which.size());
    for (size_t i : which) {
      if (pass->cancelled) break;
      auto c = check_statement(*s.text, *s.index, i, cache, s.version_id);
      if (c.status == Check_status::syntax_error) c.error_offset -= s.index->statements()[i].begin;
      results.push_back({i, std::move(c)});
    }
    bool done, more;
    {
      std::lock_guard lock(pass->mutex);
      --pass->running;
      more = pass->left > 0;
      done = !more && pass->running == 0;
    }
    if (more && !pass->cancelled) submit_chunk(uri, pass);
    inbox->post([this, uri, pass, hot, done, results = std::move(results)] {
      on_checked(uri, pass, hot, done, results);
    });
  }

  void on_checked(const std::string &uri, const std::shared_ptr<Check_pass> &pass, bool hot,
                  bool done, const std::vector<Check_result> &results) {
    const auto it = documents.find(uri);
    if (it == documents.end() || pass->cancelled) return;
    Open_document &d = *it->second;
    d.doc.apply_checks(pass->snapshot->generation, results);
    if (done) d.passes.erase(pass);
    if (d.passes.empty()) {
      publish(d);
      end_progress(d);
      // Statements dropped because an edit replaced them while they were
      // checked are taken by the pass the edit scheduled.
      return;
    }
    progress(d, false);
    if (hot) {
      publish(d);
    } else if (!d.publish_scheduled) {
      d.publish_scheduled = true;
      after(std::chrono::duration_cast<std::chrono::milliseconds>(k_publish_interval), [this, uri] {
        const auto i = documents.find(uri);
        if (i == documents.end() || !i->second->publish_scheduled) return;
        publish(*i->second);
      });
    }
  }

  void publish(Open_document &d) {
    d.publish_scheduled = false;
    const auto snap = d.doc.snapshot();
    const auto &text = *snap->text;
    const auto &index = *snap->index;
    const auto &checks = d.doc.checks();
    notify("textDocument/publishDiagnostics", [&](Json_writer &w) {
      w.StartObject();
      w.Key("uri");
      w.String(d.uri.c_str());
      w.Key("version");
      w.Int64(d.doc.version());
      w.Key("diagnostics");
      w.StartArray();
      size_t n = 0;
      auto diagnostic = [&](uint32_t b, uint32_t e, int severity, const std::string &message,
                            const char *code) {
        w.StartObject();
        w.Key("range");
        write_range(w, *snap, b, e);
        w.Key("severity");
        w.Int(severity);
        w.Key("source");
        w.String("mariadb");
        if (code != nullptr) {
          w.Key("code");
          w.String(code);
        }
        w.Key("message");
        w.String(message.c_str());
        w.EndObject();
        ++n;
      };
      Unchecked_notes unchecked(index);
      for (size_t i = 0; i < checks.size() && n < options.max_diagnostics; ++i) {
        const auto &c = checks[i];
        const auto &st = index.statements()[i];
        if (c.status == Check_status::syntax_error) {
          diagnostic(c.error_offset, c.error_offset + c.error_length, 1,
                     syntax_error_message(text, c), "syntax-error");
        } else if (const auto note = unchecked(i, c.status); !note.empty()) {
          // An unsupported sql_mode is information at the start of its run.
          if (c.status == Check_status::unsupported_mode) {
            diagnostic(st.begin, st.begin, 3, note, "not-checked");
          } else {
            diagnostic(st.begin, st.body_end, 2, note, "not-checked");
          }
        }
      }
      if (const char *open = open_context_name(index.open_at_end())) {
        const auto end = static_cast<uint32_t>(text.size());
        diagnostic(end, end, 1, std::string("the text ends inside an unterminated ") + open,
                   "unterminated");
      }
      for (const auto &e : index.errors()) diagnostic(0, 0, 2, e, "splitter");
      w.EndArray();
      w.EndObject();
    });
  }

  // ---- progress

  void begin_progress(Open_document &d) {
    d.last_progress = {};
    if (client_progress && d.progress_token.empty()) {
      d.progress_token = "\"mariadb-check-" + std::to_string(next_request++) + "\"";
      d.progress = Open_document::Progress::none;
      const std::string &token = d.progress_token;
      send([&](Json_writer &w) {
        w.Key("id");
        w.RawValue(token.c_str(), token.size(), rapidjson::kStringType);
        w.Key("method");
        w.String("window/workDoneProgress/create");
        w.Key("params");
        w.StartObject();
        w.Key("token");
        w.RawValue(token.c_str(), token.size(), rapidjson::kStringType);
        w.EndObject();
      });
    }
    progress(d, true);
  }

  void progress(Open_document &d, bool force) {
    if (d.doc.text().size() < options.progress_threshold) return;
    const auto now = Clock::now();
    if (!force && now - d.last_progress < k_progress_interval) return;
    d.last_progress = now;
    const size_t total = d.doc.sql_statements();
    const size_t checked = d.doc.checked_statements();
    const bool done = d.passes.empty();
    notify("mariadb/parseProgress", [&](Json_writer &w) {
      w.StartObject();
      w.Key("uri");
      w.String(d.uri.c_str());
      w.Key("version");
      w.Int64(d.doc.version());
      w.Key("checked");
      w.Uint64(checked);
      w.Key("total");
      w.Uint64(total);
      w.Key("done");
      w.Bool(done);
      w.EndObject();
    });
    if (d.progress == Open_document::Progress::none || done) return;
    const unsigned percent = total == 0 ? 100 : static_cast<unsigned>(checked * 100 / total);
    const bool first = d.progress == Open_document::Progress::accepted;
    d.progress = Open_document::Progress::begun;
    notify_progress(d.progress_token, first ? "begin" : "report", [&](Json_writer &w) {
      if (first) {
        w.Key("title");
        w.String("Checking SQL");
      }
      w.Key("message");
      w.String((std::to_string(checked) + " of " + std::to_string(total) + " statements").c_str());
      w.Key("percentage");
      w.Uint(percent);
    });
  }

  void end_progress(Open_document &d) {
    progress(d, true);
    if (d.progress != Open_document::Progress::none) {
      notify_progress(d.progress_token, "end", [](Json_writer &) {});
    }
    d.progress_token.clear();
    d.progress = Open_document::Progress::none;
  }

  // $/progress of `token`: a value of `kind` with what `fields` writes.
  void notify_progress(const std::string &token, const char *kind,
                       const std::function<void(Json_writer &)> &fields) {
    notify("$/progress", [&](Json_writer &w) {
      w.StartObject();
      w.Key("token");
      w.RawValue(token.c_str(), token.size(), rapidjson::kStringType);
      w.Key("value");
      w.StartObject();
      w.Key("kind");
      w.String(kind);
      fields(w);
      w.EndObject();
      w.EndObject();
    });
  }

  // ---- requests

  std::shared_ptr<const Outline> outline_of(const Snapshot &s, Outline_cache &cache) {
    {
      std::lock_guard lock(cache.mutex);
      if (cache.outline && cache.generation == s.generation) return cache.outline;
    }
    Outline_options o;
    o.version_id = s.version_id;
    o.priority = k_priority_request;
    auto outline = std::make_shared<const Outline>(build_outline(*s.text, *s.index, *parse_pool, o));
    std::lock_guard lock(cache.mutex);
    if (cache.generation <= s.generation) {
      cache.generation = s.generation;
      cache.outline = outline;
    }
    return outline;
  }

  void on_document_symbol(const std::string &id, Open_document *d) {
    if (d == nullptr) return reply_null(id);
    run_request(id, [this, snap = d->doc.snapshot(), cache = d->outline](Json_writer &w, Parser_cache &) {
      document_symbols(w, *snap, *outline_of(*snap, *cache));
    });
  }

  void on_outline(const std::string &id, Open_document *d) {
    if (d == nullptr) return reply_null(id);
    run_request(id, [this, snap = d->doc.snapshot(), cache = d->outline, uri = d->uri](
                        Json_writer &w, Parser_cache &) {
      const std::string json = outline_json(*outline_of(*snap, *cache), *snap->text, *snap->lines, uri);
      w.RawValue(json.c_str(), json.size(), rapidjson::kObjectType);
    });
  }

  void on_folding_range(const std::string &id, Open_document *d) {
    if (d == nullptr) return reply_null(id);
    run_request(id, [snap = d->doc.snapshot()](Json_writer &w, Parser_cache &cache) {
      folding_ranges(w, *snap, cache);
    });
  }

  void on_selection_range(const std::string &id, Open_document *d, const Json &params) {
    const Json *ps = member(params, "positions");
    if (d == nullptr || ps == nullptr || !ps->IsArray()) {
      return reply_error(id, k_invalid_params, "positions expected");
    }
    std::vector<Position> positions;
    for (const auto &p : ps->GetArray()) {
      if (const auto pos = position_of(&p)) positions.push_back(*pos);
    }
    run_request(id, [snap = d->doc.snapshot(), positions](Json_writer &w, Parser_cache &cache) {
      selection_ranges(w, *snap, positions, cache);
    });
  }

  void on_tokens_full(const std::string &id, Open_document *d, const std::string *previous) {
    if (d == nullptr) return reply_null(id);
    if (d->doc.text().size() > options.full_semantic_tokens_limit) return reply_null(id);
    run_request(id, [this, snap = d->doc.snapshot(), tokens = d->tokens,
                     previous = previous ? std::optional<std::string>(*previous) : std::nullopt](
                        Json_writer &w, Parser_cache &) {
      auto data = semantic_tokens(*snap, 0, static_cast<uint32_t>(snap->text->size()), *parse_pool,
                                  k_priority_request);
      std::lock_guard lock(tokens->mutex);
      const std::string result_id = std::to_string(tokens->next_id++);
      w.StartObject();
      w.Key("resultId");
      w.String(result_id.c_str());
      if (previous && *previous == tokens->result_id) {
        // One edit: the common prefix and suffix stay.
        const auto &old = tokens->data;
        size_t pre = 0;
        while (pre < old.size() && pre < data.size() && old[pre] == data[pre]) ++pre;
        size_t suf = 0;
        while (suf < old.size() - pre && suf < data.size() - pre &&
               old[old.size() - 1 - suf] == data[data.size() - 1 - suf])
          ++suf;
        w.Key("edits");
        w.StartArray();
        if (pre < old.size() || pre < data.size()) {
          w.StartObject();
          w.Key("start");
          w.Uint64(pre);
          w.Key("deleteCount");
          w.Uint64(old.size() - pre - suf);
          w.Key("data");
          write_uints(w, std::span(data).subspan(pre, data.size() - suf - pre));
          w.EndObject();
        }
        w.EndArray();
      } else {
        w.Key("data");
        write_uints(w, data);
      }
      w.EndObject();
      tokens->result_id = result_id;
      tokens->data = std::move(data);
    });
  }

  void on_tokens_range(const std::string &id, Open_document *d, const Json &params) {
    const auto a = position_of(path(params, {"range", "start"}));
    const auto b = position_of(path(params, {"range", "end"}));
    if (d == nullptr || !a || !b) return reply_error(id, k_invalid_params, "range expected");
    const uint32_t begin = d->doc.offset(*a), end = d->doc.offset(*b);
    run_request(id, [this, snap = d->doc.snapshot(), begin, end](Json_writer &w, Parser_cache &) {
      const auto data =
          semantic_tokens(*snap, begin, std::max(end, begin + 1), *parse_pool, k_priority_request);
      w.StartObject();
      w.Key("data");
      write_uints(w, data);
      w.EndObject();
    });
  }

  // ---- P5: completion, hover, navigation

  void on_completion(const std::string &id, Open_document *d, const Json &params) {
    Completion_options o;
    o.upper_case_keywords = upper_keywords;
    o.snippets = snippets;
    o.line_width = line_width;
    detail_request(id, d, params,
                   [o](Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                       Parser_cache &cache, Details_pass &pass) {
                     Completion_options co = o;
                     co.version_id = s.version_id;
                     completion(w, s, pos, c, cache, co, pass);
                   });
  }

  void on_hover(const std::string &id, Open_document *d, const Json &params) {
    detail_request(id, d, params,
                   [](Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                      Parser_cache &cache, Details_pass &pass) { hover(w, s, pos, c, cache, pass); });
  }

  void on_definition(const std::string &id, Open_document *d, const Json &params) {
    const std::string uri = d != nullptr ? d->uri : std::string();
    position_request(id, d, params, true,
                     [uri](Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                           Parser_cache &cache) { definition(w, s, uri, pos, c, cache); });
  }

  void on_references(const std::string &id, Open_document *d, const Json &params) {
    const std::string uri = d != nullptr ? d->uri : std::string();
    const Json *decl = path(params, {"context", "includeDeclaration"});
    const bool include = decl == nullptr || !decl->IsBool() || decl->GetBool();
    position_request(id, d, params, false,
                     [this, uri, include](Json_writer &w, const Snapshot &s, Position pos,
                                          const Catalog_sources &c, Parser_cache &cache) {
                       references(w, s, uri, pos, include, c, *parse_pool, cache,
                                  k_priority_request);
                     });
  }

  void on_highlight(const std::string &id, Open_document *d, const Json &params) {
    position_request(id, d, params, false,
                     [](Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                        Parser_cache &cache) { document_highlight(w, s, pos, c, cache); });
  }

  void on_prepare_rename(const std::string &id, Open_document *d, const Json &params) {
    // A fresh outline, as rename uses: both must agree on what can be renamed.
    position_request(id, d, params, true,
                     [](Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                        Parser_cache &cache) {
                       std::string error;
                       if (!prepare_rename(w, s, pos, c, cache, &error)) throw Request_failed(error);
                     });
  }

  void on_rename(const std::string &id, Open_document *d, const Json &params) {
    const auto name = string_at(params, {"newName"});
    if (!name) return reply_error(id, k_invalid_params, "newName expected");
    const std::string uri = d != nullptr ? d->uri : std::string();
    position_request(id, d, params, true,
                     [this, uri, name = *name](Json_writer &w, const Snapshot &s, Position pos,
                                               const Catalog_sources &c, Parser_cache &cache) {
                       std::string error;
                       if (!rename(w, s, uri, pos, name, c, *parse_pool, cache, k_priority_request,
                                   &error)) {
                         throw Request_failed(error);
                       }
                     });
  }

  // The indentation unit from the request's FormattingOptions (tabSize,
  // insertSpaces), the keyword case and width from the settings.
  void on_formatting(const std::string &id, Open_document *d, const Json &params, bool range) {
    if (d == nullptr) return reply_null(id);
    Position from{}, to{};
    if (range) {
      const auto a = position_of(path(params, {"range", "start"}));
      const auto b = position_of(path(params, {"range", "end"}));
      if (!a || !b) return reply_error(id, k_invalid_params, "range expected");
      from = *a;
      to = *b;
    }
    Format_options o;
    o.upper_case_keywords = upper_keywords;
    o.line_width = line_width;
    const Json *spaces = path(params, {"options", "insertSpaces"});
    const Json *size = path(params, {"options", "tabSize"});
    if (spaces != nullptr && spaces->IsBool() && !spaces->GetBool()) {
      o.indent = "\t";
    } else if (size != nullptr && size->IsUint() && size->GetUint() > 0 && size->GetUint() <= 16) {
      o.indent = std::string(size->GetUint(), ' ');
    }
    run_request(id, [snap = d->doc.snapshot(), range, from, to, o](Json_writer &w, Parser_cache &cache) mutable {
      o.version_id = snap->version_id;
      formatting(w, *snap, range, from, to, o, cache);
    });
  }

  void on_signature_help(const std::string &id, Open_document *d, const Json &params) {
    detail_request(id, d, params,
                   [](Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                      Parser_cache &cache, Details_pass &pass) { signature_help(w, s, pos, c, cache, pass); });
  }

  void on_workspace_symbol(const std::string &id, const Json &params) {
    if (id.empty()) return;
    const std::string query = string_at(params, {"query"}).value_or("");
    std::vector<std::tuple<std::string, std::shared_ptr<const Snapshot>, std::shared_ptr<Outline_cache>>>
        docs;
    for (auto &[uri, d] : documents) docs.emplace_back(uri, d->doc.snapshot(), d->outline);
    run_request(id, [this, query, docs, ws = workspace](Json_writer &w, Parser_cache &) {
      std::vector<Symbol_document> out;
      for (const auto &[uri, snap, cache] : docs) {
        // Fresh: the locations are computed from this snapshot's text.
        out.push_back({uri, snap, document_outline(snap, cache, true)});
      }
      workspace_symbols(w, query, out, ws.get(), 1000);
    });
  }
};

Server::Server(Input &in, Output &out, Server_options options)
    : m(std::make_unique<Impl>(in, out, std::move(options))) {}

Server::~Server() = default;

int Server::run() { return m->run(); }

}  // namespace mdbls::lsp
