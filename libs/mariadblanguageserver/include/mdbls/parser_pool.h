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

#ifndef MDBLS_PARSER_POOL_H_
#define MDBLS_PARSER_POOL_H_

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

struct mdbp_parser;

namespace mdbls {

// Whether the parser accepts `version_id` (a MariaDB release id,
// major*10000 + minor*100 + patch), as mdbp_parser_set_version_id() decides.
// Asked once where an id comes in, so a refused one is reported as such
// rather than as Parser_cache::get() returning nullptr for every mode.
bool version_id_supported(unsigned long version_id);

// The parsers of one worker thread, one per (sql_mode, release id).  A
// libmariadbparser parser must never be used by two threads at once; each
// worker owns its cache, so its parsers are only ever used by that worker.
class Parser_cache {
 public:
  Parser_cache();
  ~Parser_cache();
  Parser_cache(const Parser_cache &) = delete;
  Parser_cache &operator=(const Parser_cache &) = delete;

  // The parser for this mode and release id, created on first use; nullptr
  // when the parser refuses the mode (ORACLE, MSSQL) or the id.
  // `derivation`: the parser records derivations, which syntax trees need.
  mdbp_parser *get(const std::string &sql_mode, unsigned long version_id,
                   bool derivation = false);

  size_t size() const { return m_parsers.size(); }

 private:
  struct Deleter {
    void operator()(mdbp_parser *p) const;
  };
  // Compares keys as string_views, so get() looks up without copying sql_mode.
  struct Key_less {
    using is_transparent = void;
    using View = std::tuple<std::string_view, unsigned long, bool>;
    template <typename A, typename B>
    bool operator()(const A &a, const B &b) const {
      return View(a) < View(b);
    }
  };
  std::map<std::tuple<std::string, unsigned long, bool>, std::unique_ptr<mdbp_parser, Deleter>,
           Key_less>
      m_parsers;
};

// A pool of worker threads that run parse jobs, highest priority first and
// in submission order within a priority.
//
// Threads are started on demand, up to max_threads (default: one per CPU
// core).  A worker that has had nothing to do for `idle_timeout` (default
// two minutes) exits and frees its parsers; the next job starts a new one.
// Each worker passes its own Parser_cache to the jobs it runs.
class Parser_pool {
 public:
  using Job = std::function<void(Parser_cache &)>;

  explicit Parser_pool(unsigned max_threads = 0,
                       std::chrono::milliseconds idle_timeout = std::chrono::minutes(2));
  // Jobs not started yet are dropped; running jobs are waited for.
  ~Parser_pool();
  Parser_pool(const Parser_pool &) = delete;
  Parser_pool &operator=(const Parser_pool &) = delete;

  void submit(int priority, Job job);

  // Runs fn(0, cache) ... fn(n_jobs - 1, cache) as n_jobs jobs at `priority`
  // and blocks until those have finished; other jobs may still be running.
  // It must not be called from a job of this pool.
  void run_all(int priority, size_t n_jobs,
               const std::function<void(size_t job, Parser_cache &)> &fn);

  // Blocks until no job is queued or running.
  void wait();

  unsigned max_threads() const { return m_max_threads; }
  unsigned live_threads() const;

 private:
  struct Item {
    int priority;
    uint64_t seq;
    Job job;
  };
  struct Item_order {
    bool operator()(const Item &a, const Item &b) const {
      return a.priority != b.priority ? a.priority < b.priority : a.seq > b.seq;
    }
  };

  void worker_main();
  void join_finished();

  const unsigned m_max_threads;
  const std::chrono::milliseconds m_idle_timeout;

  mutable std::mutex m_mutex;
  std::condition_variable m_work;  // a job was queued, or stop
  std::condition_variable m_done;  // the pool became idle
  std::vector<Item> m_queue;       // a heap ordered by Item_order
  uint64_t m_seq = 0;
  unsigned m_live = 0;     // worker threads running worker_main
  unsigned m_waiting = 0;  // of those, waiting for a job
  unsigned m_running = 0;  // jobs being run
  bool m_stop = false;
  std::map<std::thread::id, std::thread> m_threads;
  std::vector<std::thread::id> m_finished;  // exited, not joined yet
};

}  // namespace mdbls

#endif  // MDBLS_PARSER_POOL_H_
