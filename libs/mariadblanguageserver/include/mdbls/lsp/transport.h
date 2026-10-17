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

#ifndef MDBLS_LSP_TRANSPORT_H_
#define MDBLS_LSP_TRANSPORT_H_

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <istream>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>

namespace mdbls::lsp {

// Where messages come from and go to.  std::cin reads a character at a
// time in some C++ libraries, which makes a 256 MB didOpen take seconds, so
// the server reads and writes the file descriptors directly.
class Input {
 public:
  virtual ~Input() = default;
  // Up to `n` bytes into `buf`; 0 at the end of the input.
  virtual size_t read(char *buf, size_t n) = 0;
};

class Output {
 public:
  virtual ~Output() = default;
  virtual void write(std::string_view data) = 0;
  virtual void flush() {}
};

class Fd_input : public Input {
 public:
  explicit Fd_input(int fd);  // binary mode on Windows
  size_t read(char *buf, size_t n) override;

 private:
  int m_fd;
};

class Fd_output : public Output {
 public:
  explicit Fd_output(int fd);  // binary mode on Windows
  void write(std::string_view data) override;

 private:
  int m_fd;
};

class Stream_input : public Input {
 public:
  explicit Stream_input(std::istream &in) : m_in(in) {}
  size_t read(char *buf, size_t n) override;

 private:
  std::istream &m_in;
};

class Stream_output : public Output {
 public:
  explicit Stream_output(std::ostream &out) : m_out(out) {}
  void write(std::string_view data) override { m_out.write(data.data(), static_cast<std::streamsize>(data.size())); }
  void flush() override { m_out.flush(); }

 private:
  std::ostream &m_out;
};

// Reads JSON-RPC messages framed by LSP's base protocol ("Content-Length:
// N\r\n\r\n" and N bytes).
class Message_reader {
 public:
  explicit Message_reader(Input &in) : m_in(in) {}
  // The next message's body; false at the end of the input or on a
  // malformed header.
  bool next(std::string *body);

 private:
  bool fill();  // reads more into the buffer; false at the end

  Input &m_in;
  std::string m_buf;
  size_t m_pos = 0;
};

// Writes framed messages from a thread of its own, so that no thread
// producing a message waits for a slow client.  send() may be called from
// any thread.  The destructor writes what was queued, then stops.
class Writer {
 public:
  explicit Writer(Output &out);
  ~Writer();
  Writer(const Writer &) = delete;
  Writer &operator=(const Writer &) = delete;

  void send(std::string body);
  // Blocks until everything sent so far is written.
  void flush();

 private:
  void run();

  Output &m_out;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::condition_variable m_drained;
  std::deque<std::string> m_queue;
  bool m_writing = false;
  bool m_stop = false;
  std::thread m_thread;
};

}  // namespace mdbls::lsp

#endif  // MDBLS_LSP_TRANSPORT_H_
