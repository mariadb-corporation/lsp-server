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

// The sanitizers' defaults, linked into every executable of a WITH_ASAN or
// WITH_UBSAN build (cmake/compiler.cmake), so that ctest, the end-to-end
// tests and a binary run by hand behave alike.  ASAN_OPTIONS, LSAN_OPTIONS
// and UBSAN_OPTIONS still override them.

extern "C" {

// Leaks are reported on macOS too, where LeakSanitizer is off by default;
// not with Xcode's clang, whose runtime aborts at startup on detect_leaks=1
// ("not supported on this platform").
#if defined(__apple_build_version__)
const char *__asan_default_options() { return ""; }
#else
const char *__asan_default_options() { return "detect_leaks=1"; }
#endif

const char *__ubsan_default_options() { return "print_stacktrace=1"; }

// The language server's stdin reader, a thread detached when the server
// exits with its input still open (Server::Impl::run in lsp/server.cc): a
// read that never returns cannot be joined, and what std::thread allocated
// for it is still in use when the process exits.  libFuzzer detaches its RSS
// monitor alike.
const char *__lsan_default_suppressions() {
  return "leak:std::*thread::thread*lsp::Input&\n"
         "leak:fuzzer::StartRssThread\n";
}

}
