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

#include "mdbls/live_catalog.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include "mdbls/lsp/server.h"
#include "text_util.h"

#if MDBLS_WITH_CLIENT
#include <mysql.h>
#endif

namespace mdbls {

bool live_connections_supported() { return MDBLS_WITH_CLIENT != 0; }

#if MDBLS_WITH_CLIENT

namespace {

void library_init() {
  static const bool done = [] {
    mysql_library_init(0, nullptr, nullptr);
    return true;
  }();
  (void)done;
}

// Overwrites a password before its memory is given back.
void wipe(std::string &s) {
  volatile char *p = s.data();
  for (size_t i = 0; i < s.size(); ++i) p[i] = 0;
  s.clear();
}

class Session {
 public:
  Session() {
    library_init();
    mysql_thread_init();
    m_ = mysql_init(nullptr);
  }
  ~Session() {
    if (m_ != nullptr) mysql_close(m_);
    mysql_thread_end();
  }
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;

  MYSQL *get() const { return m_; }
  std::string error() const { return m_ == nullptr ? "out of memory" : mysql_error(m_); }

  // Runs `sql`; `row` gets each row (its fields, nullptr for NULL).
  template <typename F>
  bool query(const char *sql, F &&row) {
    if (mysql_real_query(m_, sql, std::strlen(sql)) != 0) return false;
    MYSQL_RES *res = mysql_use_result(m_);
    if (res == nullptr) return mysql_field_count(m_) == 0;
    const unsigned n = mysql_num_fields(res);
    while (MYSQL_ROW r = mysql_fetch_row(res)) {
      unsigned long *lengths = mysql_fetch_lengths(res);
      std::vector<std::optional<std::string>> fields(n);
      for (unsigned i = 0; i < n; ++i) {
        if (r[i] != nullptr) fields[i] = std::string(r[i], lengths[i]);
      }
      row(fields);
    }
    const bool ok = mysql_errno(m_) == 0;
    mysql_free_result(res);
    return ok;
  }

 private:
  MYSQL *m_ = nullptr;
};

using Row = std::vector<std::optional<std::string>>;

std::string str(const Row &r, size_t i) { return i < r.size() && r[i] ? *r[i] : std::string(); }

// The schemas being built, with name lookups.
struct Builder {
  std::vector<Schema> schemas;
  std::unordered_map<std::string, size_t> schema_index;
  std::map<std::pair<std::string, std::string>, std::pair<size_t, size_t>> table_index;
  std::map<std::pair<std::string, std::string>, std::pair<size_t, size_t>> view_index;

  Schema &schema(const std::string &name) {
    const auto it = schema_index.find(name);
    if (it != schema_index.end()) return schemas[it->second];
    schema_index.emplace(name, schemas.size());
    schemas.push_back(Schema{});
    schemas.back().name = name;
    schemas.back().declared = true;
    return schemas.back();
  }
  Table *table(const std::string &s, const std::string &t) {
    const auto it = table_index.find({s, t});
    return it == table_index.end() ? nullptr : &schemas[it->second.first].tables[it->second.second];
  }
  View *view(const std::string &s, const std::string &v) {
    const auto it = view_index.find({s, v});
    return it == view_index.end() ? nullptr : &schemas[it->second.first].views[it->second.second];
  }
};

constexpr const char *k_not_performance = " <> 'performance_schema'";

}  // namespace

std::optional<Live_snapshot> fetch_live_catalog(const Connection_uri &uri, std::string password,
                                                std::string *error,
                                                const Session_options &options) {
  auto fail = [&](const std::string &e) -> std::optional<Live_snapshot> {
    wipe(password);
    if (error != nullptr) *error = e;
    return std::nullopt;
  };
  if (is_ssh_scheme(uri.scheme)) return fail("SSH tunnels (" + uri.scheme + "://) are not supported");
  if (!is_protocol_scheme(uri.scheme)) return fail("scheme " + uri.scheme + ":// is not supported");
  Session s;
  MYSQL *m = s.get();
  if (m == nullptr) return fail("out of memory");

  unsigned int connect_timeout = options.connect_timeout_s;
  unsigned int read_timeout = options.read_timeout_s;
  mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &connect_timeout);
  mysql_options(m, MYSQL_OPT_READ_TIMEOUT, &read_timeout);
  mysql_options(m, MYSQL_OPT_WRITE_TIMEOUT, &read_timeout);
  mysql_options(m, MYSQL_SET_CHARSET_NAME, "utf8mb4");

  // TLS: libmariadb has no ssl-mode option.  TLS is attempted when
  // MYSQL_OPT_SSL_ENFORCE is set, and the server certificate verified when
  // MYSQL_OPT_SSL_VERIFY_SERVER_CERT is; without verification it goes on
  // without TLS when the server offers none (PREFERRED).  REQUIRED is
  // checked after connecting.  As rest-daemon's MySQLSession does.
  std::string mode = "PREFERRED";
  if (const std::string *v = uri.option("ssl-mode")) mode = upper(*v);
  if (mode != "DISABLED" && mode != "PREFERRED" && mode != "REQUIRED" && mode != "VERIFY_CA" &&
      mode != "VERIFY_IDENTITY") {
    return fail("ssl-mode " + mode + " is not one of DISABLED, PREFERRED, REQUIRED, VERIFY_CA, "
                "VERIFY_IDENTITY");
  }
  for (const auto &[key, opt] : std::initializer_list<std::pair<const char *, mysql_option>>{
           {"ssl-ca", MYSQL_OPT_SSL_CA},
           {"ssl-capath", MYSQL_OPT_SSL_CAPATH},
           {"ssl-cert", MYSQL_OPT_SSL_CERT},
           {"ssl-key", MYSQL_OPT_SSL_KEY},
           {"ssl-cipher", MYSQL_OPT_SSL_CIPHER}}) {
    if (const std::string *v = uri.option(key)) mysql_options(m, opt, v->c_str());
  }
  const my_bool verify = mode == "VERIFY_CA" || mode == "VERIFY_IDENTITY";
  const my_bool enforce = mode != "DISABLED";
  mysql_options(m, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &verify);
  mysql_options(m, MYSQL_OPT_SSL_ENFORCE, &enforce);

  const char *host = uri.socket.empty() ? uri.host.c_str() : "localhost";
  const char *socket = uri.socket.empty() ? nullptr : uri.socket.c_str();
  const unsigned port = uri.port.value_or(uri.socket.empty() ? 3306 : 0);
  const char *user = uri.user.empty() ? nullptr : uri.user.c_str();
  const char *schema = uri.schema.empty() ? nullptr : uri.schema.c_str();
  const bool connected = mysql_real_connect(m, host, user, password.c_str(), schema, port, socket,
                                            CLIENT_MULTI_RESULTS) != nullptr;
  wipe(password);
  if (!connected) return fail("cannot connect: " + s.error());
  if (mode == "REQUIRED" && mysql_get_ssl_cipher(m) == nullptr) {
    return fail("ssl-mode REQUIRED, but the server offers no TLS");
  }

  Live_snapshot snap;
  if (!s.query("SET SESSION TRANSACTION READ ONLY", [](const Row &) {})) {
    return fail("cannot make the session read-only: " + s.error());
  }
  if (!s.query("SELECT @@sql_mode, VERSION(), DATABASE()", [&](const Row &r) {
        snap.server.sql_mode = str(r, 0);
        snap.server.version = str(r, 1);
        snap.server.current_schema = str(r, 2);
      })) {
    return fail("cannot read the server's sql_mode and version: " + s.error());
  }
  snap.server.version_id = lsp::parse_server_version(snap.server.version);

  Builder b;
  auto run = [&](const std::string &sql, auto &&row) {
    if (options.cancelled && options.cancelled()) throw std::runtime_error("cancelled");
    if (!s.query(sql.c_str(), row)) throw std::runtime_error(s.error());
  };
  try {
    run(std::string("SELECT SCHEMA_NAME, DEFAULT_CHARACTER_SET_NAME, DEFAULT_COLLATION_NAME "
                    "FROM information_schema.SCHEMATA WHERE SCHEMA_NAME") +
            k_not_performance + " ORDER BY SCHEMA_NAME",
        [&](const Row &r) {
          Schema &sc = b.schema(str(r, 0));
          if (r[1]) sc.options.emplace_back("charset", *r[1]);
          if (r[2]) sc.options.emplace_back("collation", *r[2]);
        });
    run(std::string("SELECT TABLE_SCHEMA, TABLE_NAME, TABLE_TYPE, ENGINE, TABLE_COMMENT "
                    "FROM information_schema.TABLES WHERE TABLE_SCHEMA") +
            k_not_performance + " ORDER BY TABLE_SCHEMA, TABLE_NAME",
        [&](const Row &r) {
          const std::string sname = str(r, 0), name = str(r, 1), type = str(r, 2);
          Schema &sc = b.schema(sname);
          const size_t si = b.schema_index[sname];
          if (type == "VIEW" || type == "SYSTEM VIEW") {
            View v;
            v.name = name;
            b.view_index[{sname, name}] = {si, sc.views.size()};
            sc.views.push_back(std::move(v));
          } else if (type == "SEQUENCE") {
            sc.sequences.push_back({name, {}});
          } else {
            Table t;
            t.name = name;
            t.temporary = type == "TEMPORARY";
            if (r[3] && !r[3]->empty()) t.options.emplace_back("engine", *r[3]);
            if (r[4] && !r[4]->empty()) t.options.emplace_back("comment", *r[4]);
            b.table_index[{sname, name}] = {si, sc.tables.size()};
            sc.tables.push_back(std::move(t));
          }
        });
    run(std::string("SELECT TABLE_SCHEMA, TABLE_NAME, COLUMN_NAME, COLUMN_TYPE, IS_NULLABLE, "
                    "COLUMN_DEFAULT, EXTRA, COLUMN_COMMENT, COLLATION_NAME, GENERATION_EXPRESSION "
                    "FROM information_schema.COLUMNS WHERE TABLE_SCHEMA") +
            k_not_performance + " ORDER BY TABLE_SCHEMA, TABLE_NAME, ORDINAL_POSITION",
        [&](const Row &r) {
          const std::string sname = str(r, 0), tname = str(r, 1);
          if (Table *t = b.table(sname, tname)) {
            Column c;
            c.name = str(r, 2);
            c.type = str(r, 3);
            c.nullable = str(r, 4) == "YES";
            if (r[5]) c.default_value = *r[5];
            const std::string extra = lower(str(r, 6));
            c.auto_increment = extra.find("auto_increment") != std::string::npos;
            if (r[7] && !r[7]->empty()) c.comment = *r[7];
            if (r[8]) c.collation = *r[8];
            if (r[9] && !r[9]->empty()) c.generated = *r[9];
            t->columns.push_back(std::move(c));
            ++snap.columns;
          } else if (View *v = b.view(sname, tname)) {
            v->columns.push_back(str(r, 2));
          }
        });
    run(std::string("SELECT TABLE_SCHEMA, TABLE_NAME, INDEX_NAME, NON_UNIQUE, INDEX_TYPE, "
                    "COLUMN_NAME FROM information_schema.STATISTICS WHERE TABLE_SCHEMA") +
            k_not_performance + " ORDER BY TABLE_SCHEMA, TABLE_NAME, INDEX_NAME, SEQ_IN_INDEX",
        [&](const Row &r) {
          Table *t = b.table(str(r, 0), str(r, 1));
          if (t == nullptr) return;
          const std::string name = str(r, 2);
          if (t->indexes.empty() || t->indexes.back().name != (name == "PRIMARY" ? "" : name) ||
              (name == "PRIMARY") != (t->indexes.back().kind == "primary")) {
            Index ix;
            ix.name = name == "PRIMARY" ? "" : name;
            const std::string type = upper(str(r, 4));
            ix.kind = name == "PRIMARY"  ? "primary"
                      : type == "FULLTEXT" ? "fulltext"
                      : type == "SPATIAL"  ? "spatial"
                      : type == "VECTOR"   ? "vector"
                      : str(r, 3) == "0"   ? "unique"
                                           : "index";
            t->indexes.push_back(std::move(ix));
          }
          t->indexes.back().columns.push_back(str(r, 5));
        });
    run("SELECT k.TABLE_SCHEMA, k.TABLE_NAME, k.CONSTRAINT_NAME, k.COLUMN_NAME, "
        "k.REFERENCED_TABLE_SCHEMA, k.REFERENCED_TABLE_NAME, k.REFERENCED_COLUMN_NAME, "
        "r.UPDATE_RULE, r.DELETE_RULE FROM information_schema.KEY_COLUMN_USAGE k "
        "JOIN information_schema.REFERENTIAL_CONSTRAINTS r ON r.CONSTRAINT_SCHEMA = "
        "k.CONSTRAINT_SCHEMA AND r.CONSTRAINT_NAME = k.CONSTRAINT_NAME AND r.TABLE_NAME = "
        "k.TABLE_NAME WHERE k.REFERENCED_TABLE_NAME IS NOT NULL "
        "ORDER BY k.TABLE_SCHEMA, k.TABLE_NAME, k.CONSTRAINT_NAME, k.ORDINAL_POSITION",
        [&](const Row &r) {
          Table *t = b.table(str(r, 0), str(r, 1));
          if (t == nullptr) return;
          const std::string name = str(r, 2);
          if (t->foreign_keys.empty() || t->foreign_keys.back().name != name) {
            Foreign_key fk;
            fk.name = name;
            fk.ref_schema = str(r, 4) == str(r, 0) ? "" : str(r, 4);
            fk.ref_table = str(r, 5);
            fk.on_update = str(r, 7);
            fk.on_delete = str(r, 8);
            t->foreign_keys.push_back(std::move(fk));
          }
          t->foreign_keys.back().columns.push_back(str(r, 3));
          t->foreign_keys.back().ref_columns.push_back(str(r, 6));
        });
    run("SELECT TABLE_SCHEMA, TABLE_NAME, VIEW_DEFINITION FROM information_schema.VIEWS",
        [&](const Row &r) {
          if (View *v = b.view(str(r, 0), str(r, 1))) v->definition = str(r, 2);
        });
    std::map<std::tuple<std::string, std::string, std::string>, std::pair<size_t, size_t>> routines;
    run("SELECT ROUTINE_SCHEMA, ROUTINE_NAME, ROUTINE_TYPE, DTD_IDENTIFIER "
        "FROM information_schema.ROUTINES ORDER BY ROUTINE_SCHEMA, ROUTINE_NAME",
        [&](const Row &r) {
          const std::string sname = str(r, 0);
          Schema &sc = b.schema(sname);
          Routine rt;
          rt.name = str(r, 1);
          rt.kind = lower(str(r, 2));
          if (rt.kind == "function") rt.returns = str(r, 3);
          routines[{sname, rt.name, rt.kind}] = {b.schema_index[sname], sc.routines.size()};
          sc.routines.push_back(std::move(rt));
        });
    run("SELECT SPECIFIC_SCHEMA, SPECIFIC_NAME, ROUTINE_TYPE, ORDINAL_POSITION, PARAMETER_MODE, "
        "PARAMETER_NAME, DTD_IDENTIFIER FROM information_schema.PARAMETERS "
        "ORDER BY SPECIFIC_SCHEMA, SPECIFIC_NAME, ORDINAL_POSITION",
        [&](const Row &r) {
          const auto it = routines.find({str(r, 0), str(r, 1), lower(str(r, 2))});
          if (it == routines.end() || str(r, 3) == "0") return;  // 0: a function's result
          Routine &rt = b.schemas[it->second.first].routines[it->second.second];
          Parameter p;
          p.mode = rt.kind == "procedure" ? str(r, 4) : "";
          p.name = str(r, 5);
          p.type = str(r, 6);
          rt.parameters.push_back(std::move(p));
        });
    run("SELECT TRIGGER_SCHEMA, TRIGGER_NAME, ACTION_TIMING, EVENT_MANIPULATION, "
        "EVENT_OBJECT_TABLE FROM information_schema.TRIGGERS",
        [&](const Row &r) {
          Trigger tr;
          tr.name = str(r, 1);
          tr.timing = str(r, 2);
          tr.events = str(r, 3);
          tr.table = str(r, 4);
          b.schema(str(r, 0)).triggers.push_back(std::move(tr));
        });
    run("SELECT EVENT_SCHEMA, EVENT_NAME FROM information_schema.EVENTS", [&](const Row &r) {
      b.schema(str(r, 0)).events.push_back({str(r, 1), {}});
    });
  } catch (const std::exception &e) {
    return fail(std::string("cannot read the catalog: ") + e.what());
  }
  for (const auto &sc : b.schemas) snap.tables += sc.tables.size();
  snap.catalog = std::make_shared<const Catalog>(Catalog::from_schemas(std::move(b.schemas)));
  snap.fetched = std::chrono::system_clock::now();
  return snap;
}

#else

std::optional<Live_snapshot> fetch_live_catalog(const Connection_uri &, std::string password,
                                                std::string *error, const Session_options &) {
  for (auto &c : password) c = 0;
  if (error != nullptr) *error = "this build has no MariaDB client library";
  return std::nullopt;
}

#endif

Live_catalog::Live_catalog(std::string uri, Live_catalog_options options,
                           std::function<void()> on_update)
    : m_uri(std::move(uri)), m_options(std::move(options)), m_on_update(std::move(on_update)) {
  m_normalized = normalize_connection_uri(m_uri).value_or("<invalid connection URI>");
  m_thread = std::thread([this] { run(); });
}

Live_catalog::~Live_catalog() {
  {
    std::lock_guard lock(m_mutex);
    m_stop = true;
  }
  m_cv.notify_all();
  m_thread.join();
}

void Live_catalog::refresh() {
  {
    std::lock_guard lock(m_mutex);
    m_wanted = true;
  }
  m_cv.notify_all();
}

std::shared_ptr<const Live_snapshot> Live_catalog::snapshot() const {
  std::lock_guard lock(m_mutex);
  return m_snapshot;
}

std::string Live_catalog::error() const {
  std::lock_guard lock(m_mutex);
  return m_error;
}

uint64_t Live_catalog::attempts() const {
  std::lock_guard lock(m_mutex);
  return m_attempts;
}

void Live_catalog::run() {
  using Clock = std::chrono::steady_clock;
  auto retry = m_options.retry_after;
  Clock::time_point next = Clock::time_point::max();
  for (;;) {
    {
      std::unique_lock lock(m_mutex);
      while (!m_stop && !m_wanted) {
        if (next == Clock::time_point::max()) {
          m_cv.wait(lock);
        } else if (m_cv.wait_until(lock, next) == std::cv_status::timeout) {
          break;
        }
      }
      if (m_stop) return;
      m_wanted = false;
    }
    next = Clock::time_point::max();

    std::string error;
    bool permanent = false;
    std::optional<Live_snapshot> snap;
    const auto parsed = parse_connection_uri(m_uri);
    if (!parsed) {
      error = "not a connection URI";
      permanent = true;
    } else if (is_ssh_scheme(parsed->scheme)) {
      error = "SSH tunnels (" + parsed->scheme + "://) are not supported";
      permanent = true;
    } else if (!live_connections_supported()) {
      error = "this build has no MariaDB client library";
      permanent = true;
    } else {
      std::string source;
      std::optional<std::string> password;
      if (parsed->password) {
        password = *parsed->password;
      } else {
        const Secret_store store(m_options.secrets);
        password = store.password_for(m_uri, &source, &error);
        if (!password) {
          error = "connection not configured: " + error;
          permanent = true;
        }
      }
      if (password) {
        if (m_options.log) {
          m_options.log("live catalog " + m_normalized + ": connecting" +
                        (source.empty() ? "" : " (password from " + source + ")"));
        }
        Session_options session = m_options.session;
        session.cancelled = [this] {
          std::lock_guard lock(m_mutex);
          return m_stop;
        };
        snap = fetch_live_catalog(*parsed, std::move(*password), &error, session);
      }
    }
    {
      std::lock_guard lock(m_mutex);
      ++m_attempts;
      if (snap) {
        m_snapshot = std::make_shared<const Live_snapshot>(std::move(*snap));
        m_error.clear();
      } else {
        m_error = error;
      }
    }
    if (m_options.log) {
      if (snap) {
        const auto s = snapshot();
        m_options.log("live catalog " + m_normalized + ": " + std::to_string(s->tables) +
                      " tables, " + std::to_string(s->columns) + " columns, server " +
                      s->server.version);
      } else {
        m_options.log("live catalog " + m_normalized + ": " + error);
      }
    }
    if (snap) {
      retry = m_options.retry_after;
      if (m_options.refresh_interval.count() > 0) next = Clock::now() + m_options.refresh_interval;
    } else if (!permanent) {
      next = Clock::now() + retry;
      retry = std::min<std::chrono::milliseconds>(retry * 2, std::chrono::minutes(5));
    }
    if (m_on_update) m_on_update();
  }
}

}  // namespace mdbls
