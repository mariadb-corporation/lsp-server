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

#include "mdbls/parser_pool.h"

#include <algorithm>

#include "mdbparse.h"

namespace mdbls {

// --- Parser_cache --------------------------------------------------------

Parser_cache::Parser_cache() = default;
Parser_cache::~Parser_cache() = default;

void Parser_cache::Deleter::operator()(mdbp_parser *p) const { mdbp_parser_free(p); }

mdbp_parser *Parser_cache::get(const std::string &sql_mode, unsigned long version_id) {
  const auto key = std::make_pair(sql_mode, version_id);
  auto it = m_parsers.find(key);
  if (it == m_parsers.end()) {
    mdbp_options opts;
    mdbp_options_init(&opts);
    opts.sql_mode = sql_mode.c_str();
    opts.want_derivation = 0;
    std::unique_ptr<mdbp_parser, Deleter> p{mdbp_parser_new(&opts)};
    if (p && mdbp_parser_set_version_id(p.get(), version_id) != MDBP_OK) p.reset();
    it = m_parsers.emplace(key, std::move(p)).first;
  }
  return it->second.get();
}

// --- Parser_pool ---------------------------------------------------------

Parser_pool::Parser_pool(unsigned max_threads, std::chrono::milliseconds idle_timeout)
    : m_max_threads(max_threads != 0 ? max_threads
                                     : std::max(1u, std::thread::hardware_concurrency())),
      m_idle_timeout(idle_timeout) {}

Parser_pool::~Parser_pool() {
  std::map<std::thread::id, std::thread> threads;
  {
    std::lock_guard lock(m_mutex);
    m_stop = true;
    m_queue.clear();
  }
  m_work.notify_all();
  {
    std::lock_guard lock(m_mutex);
    threads.swap(m_threads);
  }
  for (auto &[id, t] : threads) t.join();
}

unsigned Parser_pool::live_threads() const {
  std::lock_guard lock(m_mutex);
  return m_live;
}

void Parser_pool::join_finished() {
  std::vector<std::thread> done;
  {
    std::lock_guard lock(m_mutex);
    for (const auto id : m_finished) {
      auto it = m_threads.find(id);
      if (it != m_threads.end()) {
        done.push_back(std::move(it->second));
        m_threads.erase(it);
      }
    }
    m_finished.clear();
  }
  // An exited worker has left worker_main() or is about to; join waits for that.
  for (auto &t : done) t.join();
}

void Parser_pool::submit(int priority, Job job) {
  join_finished();
  std::lock_guard lock(m_mutex);
  m_queue.push_back({priority, m_seq++, std::move(job)});
  std::push_heap(m_queue.begin(), m_queue.end(), Item_order{});
  // Start a worker when none is free to take the job.
  if (m_waiting < m_queue.size() && m_live < m_max_threads) {
    ++m_live;
    std::thread t(&Parser_pool::worker_main, this);
    const auto id = t.get_id();
    m_threads.emplace(id, std::move(t));
  }
  m_work.notify_one();
}

void Parser_pool::wait() {
  std::unique_lock lock(m_mutex);
  m_done.wait(lock, [this] { return m_queue.empty() && m_running == 0; });
}

void Parser_pool::worker_main() {
  Parser_cache cache;
  std::unique_lock lock(m_mutex);
  for (;;) {
    if (m_stop) break;
    if (m_queue.empty()) {
      ++m_waiting;
      const bool woken = m_work.wait_for(lock, m_idle_timeout,
                                         [this] { return m_stop || !m_queue.empty(); });
      --m_waiting;
      if (!woken) break;  // idle for idle_timeout: let the thread go
      continue;
    }
    std::pop_heap(m_queue.begin(), m_queue.end(), Item_order{});
    Job job = std::move(m_queue.back().job);
    m_queue.pop_back();
    ++m_running;
    lock.unlock();
    try {
      job(cache);
    } catch (...) {
      // A job reports its own failures; the pool keeps running.
    }
    job = nullptr;  // release what the job captured before taking the lock
    lock.lock();
    --m_running;
    if (m_queue.empty() && m_running == 0) m_done.notify_all();
  }
  --m_live;
  if (!m_stop) m_finished.push_back(std::this_thread::get_id());
  if (m_queue.empty() && m_running == 0) m_done.notify_all();
}

}  // namespace mdbls
