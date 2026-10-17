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

#include "mdbls/lsp/transport.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "text_util.h"

namespace mdbls::lsp {

// Content-Length counts bytes: Windows must not translate line ends.
Fd_input::Fd_input(int fd) : m_fd(fd) {
#ifdef _WIN32
  ::_setmode(fd, _O_BINARY);
#endif
}

Fd_output::Fd_output(int fd) : m_fd(fd) {
#ifdef _WIN32
  ::_setmode(fd, _O_BINARY);
#endif
}

size_t Fd_input::read(char *buf, size_t n) {
  for (;;) {
#ifdef _WIN32
    const int r = ::_read(m_fd, buf, static_cast<unsigned>(std::min<size_t>(n, 1u << 30)));
#else
    const ssize_t r = ::read(m_fd, buf, n);
#endif
    if (r >= 0) return static_cast<size_t>(r);
    if (errno != EINTR) return 0;
  }
}

void Fd_output::write(std::string_view data) {
  while (!data.empty()) {
#ifdef _WIN32
    const int r = ::_write(m_fd, data.data(), static_cast<unsigned>(std::min<size_t>(data.size(), 1u << 30)));
#else
    const ssize_t r = ::write(m_fd, data.data(), data.size());
#endif
    if (r < 0) {
      if (errno == EINTR) continue;
      return;  // the client is gone
    }
    data.remove_prefix(static_cast<size_t>(r));
  }
}

size_t Stream_input::read(char *buf, size_t n) {
  // One byte blocking, then whatever is available, so that a pipe-like
  // stream does not wait for a full buffer.
  if (!m_in.read(buf, 1)) return 0;
  const auto more = m_in.readsome(buf + 1, static_cast<std::streamsize>(n - 1));
  return 1 + static_cast<size_t>(std::max<std::streamsize>(more, 0));
}

bool Message_reader::fill() {
  if (m_pos > 0) {
    m_buf.erase(0, m_pos);
    m_pos = 0;
  }
  const size_t old = m_buf.size();
  m_buf.resize(old + (1u << 16));
  const size_t n = m_in.read(m_buf.data() + old, 1u << 16);
  m_buf.resize(old + n);
  return n > 0;
}

bool Message_reader::next(std::string *body) {
  size_t length = 0;
  bool have_length = false;
  for (;;) {
    // One header line.
    size_t eol;
    while ((eol = m_buf.find('\n', m_pos)) == std::string::npos) {
      if (!fill()) return false;
    }
    std::string_view line(m_buf.data() + m_pos, eol - m_pos);
    m_pos = eol + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (line.empty()) {
      if (have_length) break;
      continue;  // stray blank line between messages
    }
    const auto colon = line.find(':');
    if (colon == std::string_view::npos) return false;
    const std::string name = lower(line.substr(0, colon));
    if (name == "content-length") {
      size_t i = colon + 1;
      while (i < line.size() && line[i] == ' ') ++i;
      const auto r = std::from_chars(line.data() + i, line.data() + line.size(), length);
      if (r.ec != std::errc()) return false;
      have_length = true;
    }
  }
  // The body: what is buffered, then straight into the result.
  const size_t buffered = std::min(length, m_buf.size() - m_pos);
  body->assign(m_buf, m_pos, buffered);
  m_pos += buffered;
  body->resize(length);
  size_t have = buffered;
  while (have < length) {
    const size_t n = m_in.read(body->data() + have, length - have);
    if (n == 0) return false;
    have += n;
  }
  return true;
}

Writer::Writer(Output &out) : m_out(out), m_thread([this] { run(); }) {}

Writer::~Writer() {
  {
    std::lock_guard lock(m_mutex);
    m_stop = true;
  }
  m_cv.notify_all();
  m_thread.join();
}

void Writer::send(std::string body) {
  {
    std::lock_guard lock(m_mutex);
    m_queue.push_back(std::move(body));
  }
  m_cv.notify_one();
}

void Writer::flush() {
  std::unique_lock lock(m_mutex);
  m_drained.wait(lock, [this] { return m_queue.empty() && !m_writing; });
}

void Writer::run() {
  std::unique_lock lock(m_mutex);
  for (;;) {
    m_cv.wait(lock, [this] { return m_stop || !m_queue.empty(); });
    if (m_queue.empty()) {
      if (m_stop) return;
      continue;
    }
    std::deque<std::string> batch;
    batch.swap(m_queue);
    m_writing = true;
    lock.unlock();
    std::string out;
    for (const auto &body : batch) {
      out.clear();
      out.append("Content-Length: ").append(std::to_string(body.size())).append("\r\n\r\n");
      if (body.size() < (1u << 16)) {
        out.append(body);
        m_out.write(out);
      } else {
        m_out.write(out);
        m_out.write(body);
      }
    }
    m_out.flush();
    lock.lock();
    m_writing = false;
    if (m_queue.empty()) m_drained.notify_all();
  }
}

}  // namespace mdbls::lsp
