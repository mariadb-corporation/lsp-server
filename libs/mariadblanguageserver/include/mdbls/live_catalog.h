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

#ifndef MDBLS_LIVE_CATALOG_H_
#define MDBLS_LIVE_CATALOG_H_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <set>
#include <tuple>
#include <utility>
#include <vector>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "mdbls/catalog.h"
#include "mdbls/connection_uri.h"
#include "mdbls/secret_store.h"

namespace mdbls {

// Whether this build has the MariaDB client library (live connections).
bool live_connections_supported();

// What a server said about itself when the session opened.
struct Server_info {
  std::string sql_mode;
  std::string version;          // VERSION(): "11.8.3-MariaDB-log"
  unsigned long version_id = 0; // 110803; 0 when the version could not be read
  std::string current_schema;   // DATABASE(), "" for none
};

// One reading of a server's catalog.
struct Live_snapshot {
  std::shared_ptr<const Catalog> catalog;
  Server_info server;
  std::chrono::system_clock::time_point fetched;
  size_t tables = 0;
  size_t columns = 0;
};

struct Session_options {
  unsigned connect_timeout_s = 5;
  unsigned read_timeout_s = 30;
  // Asked between queries: true gives up the reading.
  std::function<bool()> cancelled;
};

// Connects to `uri` with `password` (the password is not kept), makes the
// session read-only, reads @@sql_mode, VERSION() and DATABASE(), then the
// schemas, tables, columns, indexes, foreign keys, views, routines and their
// parameters, triggers and events from information_schema (all but
// performance_schema), and disconnects.  TLS is used when the server offers
// it (ssl-mode=PREFERRED); the URI's ssl-mode, ssl-ca, ssl-cert and ssl-key
// options apply.  nullopt, with `error` set, on failure.
std::optional<Live_snapshot> fetch_live_catalog(const Connection_uri &uri, std::string password,
                                                std::string *error,
                                                const Session_options &options = {});

// Reads a connection's catalog some other way than through a direct
// connection: the language server's --mcp mode asks its client, which has
// MariaDB Shell's MCP server (see mcp_catalog.h).  nullopt with `error` set
// on failure; `permanent` when another attempt would fail the same way.
// `cancelled` is to be asked between steps.
using Catalog_fetcher = std::function<std::optional<Live_snapshot>(
    const std::string &uri, std::string *error, bool *permanent,
    const std::function<bool()> &cancelled)>;

// The details of stubs (Table::stub) read for a request.
struct Catalog_details {
  std::vector<std::pair<std::string, Table>> tables;  // schema, table
  std::vector<std::pair<std::string, View>> views;
  std::vector<std::pair<std::string, Routine>> routines;
  // Foreign keys of other tables that point at a table read: the schema and
  // table they belong to.  Kept on those tables while they are stubs, so a
  // table related to one in use is known as related before it is read.
  std::vector<std::tuple<std::string, std::string, Foreign_key>> incoming;
};

// `catalog` with the stubs `details` describes filled in.
std::shared_ptr<const Catalog> apply_details(const Catalog &catalog, const Catalog_details &details);

// Reads the details of stubs; as Catalog_fetcher.
using Detail_fetcher = std::function<std::optional<Catalog_details>(
    const std::string &uri, const std::vector<Object_key> &objects, std::string *error,
    const std::function<bool()> &cancelled)>;

struct Live_catalog_options {
  // With `fetch`: reads the details of stubs when a request asks.
  Detail_fetcher fetch_details;
  // Set, the catalog is read through this and the secret store and the
  // client library are not used.
  Catalog_fetcher fetch;
  Secret_store_options secrets;
  Session_options session;
  // Read the catalog again this often; 0: only when refresh() asks.
  std::chrono::seconds refresh_interval{0};
  // First retry after a failure; doubled on each further one, up to five
  // minutes.  A missing password or an unsupported URI is not retried.
  std::chrono::milliseconds retry_after{5000};
  std::function<void(const std::string &)> log;
};

// The catalog of one connection, kept current on a background thread of
// its own: read when created, when refresh() is called, at the refresh
// interval, and again with back-off after a failure.  Requests never wait
// for it; they use the last snapshot.
class Live_catalog {
 public:
  // `on_update` runs on the background thread after each attempt, success
  // or not.
  Live_catalog(std::string uri, Live_catalog_options options,
               std::function<void()> on_update = {});
  ~Live_catalog();
  Live_catalog(const Live_catalog &) = delete;
  Live_catalog &operator=(const Live_catalog &) = delete;

  // The URI as given, and normalized.
  const std::string &uri() const { return m_uri; }
  const std::string &normalized_uri() const { return m_normalized; }

  void refresh();

  // Has the details of `objects` (stubs of the snapshot) read on the
  // catalog's thread and calls `done` there once they are in snapshot(),
  // failed or were not needed, or the catalog is going away.  False, and
  // `done` is never called, when there is no detail fetcher or nothing to
  // ask.  Objects that failed are not asked for again until the catalog is
  // read again.
  bool request_details(const std::set<Object_key> &objects, std::function<void()> done);
  // request_details(), waiting up to `wait`: true when the details came in
  // time, false when they come later, not at all, or cannot be asked for.
  bool details(const std::set<Object_key> &objects, std::chrono::milliseconds wait);
  // Reads again this often from now on (0: only on refresh()), counted
  // from the last successful read.
  void set_refresh_interval(std::chrono::seconds interval);

  // The last snapshot read, nullptr before the first one.
  std::shared_ptr<const Live_snapshot> snapshot() const;
  // Why the last attempt failed, "" after a success.
  std::string error() const;
  // Attempts made so far (finished).
  uint64_t attempts() const;

 private:
  void run();
  struct Detail_request {
    std::vector<Object_key> objects;
    std::function<void()> done;
  };
  void read_details(std::vector<Detail_request> requests);

  const std::string m_uri;
  std::string m_normalized;
  const Live_catalog_options m_options;
  const std::function<void()> m_on_update;

  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_stop = false;
  bool m_wanted = true;  // a refresh is due
  std::chrono::seconds m_interval;  // m_options.refresh_interval until set
  bool m_reschedule = false;        // m_interval changed
  std::vector<Detail_request> m_detail_requests;
  std::set<Object_key> m_details_failed;  // since the last reading
  std::shared_ptr<const Live_snapshot> m_snapshot;
  std::string m_error;
  uint64_t m_attempts = 0;
  std::thread m_thread;
};

}  // namespace mdbls

#endif  // MDBLS_LIVE_CATALOG_H_
