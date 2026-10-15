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

#ifndef MDBLS_LSP_SERVER_H_
#define MDBLS_LSP_SERVER_H_

#include <chrono>
#include <cstddef>
#include <memory>
#include <ostream>
#include <string_view>

#include "mdbls/statement_index.h"

namespace mdbls::lsp {

struct Server_options {
  unsigned threads = 0;  // parser threads; 0: one per CPU core
  // Quiet time after an edit before its statements are checked.
  std::chrono::milliseconds debounce{150};
  size_t max_diagnostics = 1000;  // per document
  // semanticTokens/full answers null above this size; the client then asks
  // for ranges.
  size_t full_semantic_tokens_limit = 4u << 20;
  // Documents at least this large report check progress.
  size_t progress_threshold = 4u << 20;
  Index_options defaults;  // sql_mode, version id and delimiter of new documents
  std::ostream *log = nullptr;
};

// A language server speaking LSP over `in` and `out` (JSON-RPC with
// Content-Length framing; see transport.h).  The initialize request's initializationOptions
// may override the options: sqlMode, serverVersion, delimiter, debounceMs,
// maxDiagnostics, semanticTokensFullLimit, progressThreshold.
//
// Threads: one reads `in`, one writes `out`, the caller's thread runs the
// event loop that owns the documents, a small pool answers requests and the
// parser pool checks statements.
class Input;
class Output;

class Server {
 public:
  Server(Input &in, Output &out, Server_options options = {});
  ~Server();
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;

  // Serves until the exit notification or the end of the input.  Returns
  // the process exit code: 0 when shutdown came before, 1 otherwise.
  int run();

 private:
  struct Impl;
  std::unique_ptr<Impl> m;
};

// "11.4.2", "11.4.2-MariaDB-log", "110402" -> 110402; 0 when not a version.
unsigned long parse_server_version(std::string_view version);

}  // namespace mdbls::lsp

#endif  // MDBLS_LSP_SERVER_H_
