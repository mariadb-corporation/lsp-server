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

#include "mdbls/secret_store.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>
#include <mutex>

#include "mdbls/connection_uri.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace mdbls {

namespace fs = std::filesystem;

namespace {

constexpr const char *k_helper_prefix = "mariadb-secret-store-";
constexpr const char *k_disabled = "<disabled>";

// The connection kinds and the key prefixes they are stored under, in the
// order they are looked up; the long forms are the older spelling.
constexpr std::pair<const char *, const char *> k_prefixes[] = {
    {"gui", "GUI:CONN:"},
    {"mcp", "MCP:CONN:"},
    {"gui", "GUI:Connection:"},
    {"mcp", "MCP:Connection:"},
};

const char *getenv_shell(const char *name) {
  if (const char *v = std::getenv(name)) return v;
  // The variables were called MYSQLSH_* before the rename.
  const std::string_view n(name);
  constexpr std::string_view prefix = "MARIADB_SHELL_";
  if (n.substr(0, prefix.size()) != prefix) return nullptr;
  const std::string legacy = "MYSQLSH_" + std::string(n.substr(prefix.size()));
  return std::getenv(legacy.c_str());
}

std::string default_helper() {
#ifdef _WIN32
  return "windows-credential";
#elif defined(__APPLE__)
  return "keychain";
#else
  return "login-path";
#endif
}

// credentialStore.helper from the shell's options.json, "" when unset.
std::string helper_from_options() {
  const std::string dir = shell_user_config_dir();
  if (dir.empty()) return {};
  std::ifstream in((fs::path(dir) / "options.json").string(), std::ios::binary);
  if (!in) return {};
  const std::string text{std::istreambuf_iterator<char>(in), {}};
  rapidjson::Document d;
  d.Parse(text.c_str(), text.size());
  if (d.HasParseError() || !d.IsObject()) return {};
  if (const auto it = d.FindMember("credentialStore.helper");
      it != d.MemberEnd() && it->value.IsString()) {
    return it->value.GetString();
  }
  if (const auto it = d.FindMember("credentialStore");
      it != d.MemberEnd() && it->value.IsObject()) {
    if (const auto h = it->value.FindMember("helper");
        h != it->value.MemberEnd() && h->value.IsString()) {
      return h->value.GetString();
    }
  }
  return {};
}

std::string spec_json(const std::string &type, const std::string *id) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  w.StartObject();
  w.Key("SecretType");
  w.String(type.c_str(), static_cast<rapidjson::SizeType>(type.size()));
  if (id != nullptr) {
    w.Key("SecretID");
    w.String(id->c_str(), static_cast<rapidjson::SizeType>(id->size()));
  }
  w.EndObject();
  return {buf.GetString(), buf.GetSize()};
}

// What a failing helper said, without anything that may be a secret.
std::string helper_message(const std::string &out, int code) {
  std::string msg = out.substr(0, 200);
  if (msg.find("\"Secret\"") != std::string::npos) msg = "(output withheld)";
  for (auto &c : msg) {
    if (c == '\n' || c == '\r') c = ' ';
  }
  return "the secret store helper failed (exit " + std::to_string(code) + ")" +
         (msg.empty() ? "" : ": " + msg);
}

std::optional<std::string> base64_decode(std::string_view in) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int bits = 0;
  uint32_t acc = 0;
  for (const char c : in) {
    if (c == '=') break;
    if (c == '\n' || c == '\r') continue;
    const int v = value(c);
    if (v < 0) return std::nullopt;
    acc = (acc << 6) | static_cast<uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xFF));
    }
  }
  return out;
}

}  // namespace

std::string decode_shell_secret(std::string_view stored) {
  if (stored.empty()) return {};
  if (stored.front() == 'r') return std::string(stored.substr(1));
  if (stored.front() == 'b') {
    if (auto d = base64_decode(stored.substr(1))) return *d;
  }
  return std::string(stored);
}

std::string shell_user_config_dir() {
  if (const char *v = getenv_shell("MARIADB_SHELL_USER_CONFIG_HOME"); v != nullptr && *v) return v;
#ifdef _WIN32
  if (const char *appdata = std::getenv("APPDATA"); appdata != nullptr && *appdata) {
    return (fs::path(appdata) / "MariaDB" / "mariadb-shell").string();
  }
  return {};
#else
  if (const char *home = std::getenv("HOME"); home != nullptr && *home) {
    return (fs::path(home) / ".mariadb-shell").string();
  }
  return {};
#endif
}

std::string find_program_dir(const std::string &name) {
  const char *path = std::getenv("PATH");
  if (path == nullptr) return {};
#ifdef _WIN32
  const char sep = ';';
  const std::string exe = name + ".exe";
#else
  const char sep = ':';
  const std::string &exe = name;
#endif
  std::string_view p(path);
  while (!p.empty()) {
    const size_t end = std::min(p.size(), p.find(sep));
    const std::string dir(p.substr(0, end));
    p.remove_prefix(std::min(p.size(), end + 1));
    if (dir.empty()) continue;
    std::error_code ec;
    const fs::path candidate = fs::path(dir) / exe;
    if (!fs::is_regular_file(candidate, ec)) continue;
#ifndef _WIN32
    if (::access(candidate.c_str(), X_OK) != 0) continue;
#endif
    const fs::path real = fs::canonical(candidate, ec);
    return (ec ? candidate : real).parent_path().string();
  }
  return {};
}

#ifdef _WIN32

std::optional<std::string> run_program(const std::string &program,
                                       const std::vector<std::string> &args,
                                       std::string_view input, int *exit_code,
                                       std::chrono::milliseconds timeout) {
  static std::mutex one_at_a_time;
  std::lock_guard serial(one_at_a_time);
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr;
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) return std::nullopt;
  if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
    CloseHandle(in_r);
    CloseHandle(in_w);
    return std::nullopt;
  }
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  std::string cmd = "\"" + program + "\"";
  for (const auto &a : args) cmd += " \"" + a + "\"";
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessA(program.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(in_r);
  CloseHandle(out_w);
  if (!ok) {
    CloseHandle(in_w);
    CloseHandle(out_r);
    return std::nullopt;
  }
  std::thread writer([&] {
    DWORD written = 0;
    size_t at = 0;
    while (at < input.size() &&
           WriteFile(in_w, input.data() + at, static_cast<DWORD>(input.size() - at), &written,
                     nullptr)) {
      at += written;
    }
    CloseHandle(in_w);
  });
  // Read on a thread too: the wait below bounds a helper that never finishes
  // (a keychain prompt nobody answers) and so keeps its stdout open; killing
  // it ends both the read and the write.
  std::string out;
  std::thread reader([&] {
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(out_r, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
  });
  const bool timed_out =
      WaitForSingleObject(pi.hProcess, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0;
  if (timed_out) TerminateProcess(pi.hProcess, 1);
  writer.join();
  reader.join();
  CloseHandle(out_r);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  if (timed_out) return std::nullopt;
  if (exit_code != nullptr) *exit_code = static_cast<int>(code);
  return out;
}

#else

std::optional<std::string> run_program(const std::string &program,
                                       const std::vector<std::string> &args,
                                       std::string_view input, int *exit_code,
                                       std::chrono::milliseconds timeout) {
  // Serialized, and the pipes are close-on-exec: a helper started by another
  // thread must not inherit this one's pipe ends (it would hold them open).
  static std::mutex one_at_a_time;
  std::lock_guard serial(one_at_a_time);
  // A helper that exits before reading its input leaves the pipe without a
  // reader: writing to it must fail with EPIPE, not kill the process.  Done
  // once, and only while SIGPIPE has its default action.
  static const bool sigpipe_ignored = [] {
    struct sigaction old{};
    if (::sigaction(SIGPIPE, nullptr, &old) == 0 && old.sa_handler == SIG_DFL) {
      struct sigaction ignore{};
      ignore.sa_handler = SIG_IGN;
      ::sigaction(SIGPIPE, &ignore, nullptr);
    }
    return true;
  }();
  (void)sigpipe_ignored;
  int in_pipe[2], out_pipe[2];
  if (::pipe(in_pipe) != 0) return std::nullopt;
  if (::pipe(out_pipe) != 0) {
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    return std::nullopt;
  }
  for (const int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]}) {
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in_pipe[0], 0);
  posix_spawn_file_actions_adddup2(&fa, out_pipe[1], 1);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  for (const int fd : {in_pipe[0], in_pipe[1], out_pipe[0], out_pipe[1]}) {
    posix_spawn_file_actions_addclose(&fa, fd);
  }
  std::vector<char *> argv;
  argv.push_back(const_cast<char *>(program.c_str()));
  for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
  argv.push_back(nullptr);
  pid_t pid = 0;
  const int rc = posix_spawn(&pid, program.c_str(), &fa, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&fa);
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);
  if (rc != 0) {
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    return std::nullopt;
  }
  // Write and read together, so neither side blocks the other.
  std::string out;
  size_t written = 0;
  int wfd = in_pipe[1];
  if (input.empty()) {
    ::close(wfd);
    wfd = -1;
  } else {
    ::fcntl(wfd, F_SETFL, ::fcntl(wfd, F_GETFL) | O_NONBLOCK);
  }
  const int rfd = out_pipe[0];
  bool reading = true;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool timed_out = false;
  while (reading || wfd >= 0) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
      timed_out = true;
      break;
    }
    pollfd fds[2];
    nfds_t n = 0;
    if (reading) fds[n++] = {rfd, POLLIN, 0};
    if (wfd >= 0) fds[n++] = {wfd, POLLOUT, 0};
    if (::poll(fds, n, static_cast<int>(std::min<long long>(left.count(), 1000))) < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (nfds_t k = 0; k < n; ++k) {
      if (fds[k].fd == rfd && (fds[k].revents & (POLLIN | POLLHUP | POLLERR))) {
        char buf[4096];
        const ssize_t got = ::read(rfd, buf, sizeof(buf));
        if (got > 0) {
          out.append(buf, static_cast<size_t>(got));
        } else if (got == 0 || errno != EINTR) {
          reading = false;
        }
      } else if (fds[k].fd == wfd && (fds[k].revents & (POLLOUT | POLLHUP | POLLERR))) {
        const ssize_t put = ::write(wfd, input.data() + written, input.size() - written);
        if (put > 0) written += static_cast<size_t>(put);
        if (put < 0 && errno != EAGAIN && errno != EINTR) written = input.size();
        if (written >= input.size()) {
          ::close(wfd);
          wfd = -1;
        }
      }
    }
  }
  ::close(rfd);
  if (wfd >= 0) ::close(wfd);
  if (timed_out) ::kill(pid, SIGKILL);
  int status = 0;
  while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  if (timed_out) return std::nullopt;
  if (exit_code != nullptr) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return out;
}

#endif

Secret_store::Secret_store(Secret_store_options options) {
  m_name = options.helper;
  if (m_name.empty()) {
    if (const char *v = getenv_shell("MARIADB_SHELL_CREDENTIAL_STORE_HELPER"); v != nullptr && *v) {
      m_name = v;
    }
  }
  if (m_name.empty()) m_name = helper_from_options();
  if (m_name.empty() || m_name == "default") m_name = default_helper();
  if (m_name == k_disabled) {
    m_error = "the shell's credential store is disabled";
    return;
  }
  std::string dir = options.shell_bin_dir;
  if (dir.empty()) dir = find_program_dir("mariadb-shell");
  if (dir.empty()) {
    m_error = "mariadb-shell was not found on PATH, so neither was its secret store helper";
    return;
  }
#ifdef _WIN32
  const std::string exe = std::string(k_helper_prefix) + m_name + ".exe";
#else
  const std::string exe = std::string(k_helper_prefix) + m_name;
#endif
  const fs::path path = fs::path(dir) / exe;
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    m_error = "no secret store helper " + path.string();
    return;
  }
  m_path = path.string();
}

namespace {

// The helper's JSON answer to `command`, or nullopt with `error` set.
std::optional<rapidjson::Document> ask(const std::string &path, const std::string &not_found,
                                       const char *command, const std::string &input,
                                       std::string *error) {
  auto fail = [&](std::string e) -> std::optional<rapidjson::Document> {
    if (error != nullptr) *error = std::move(e);
    return std::nullopt;
  };
  if (path.empty()) return fail(not_found);
  int code = 0;
  const auto out = run_program(path, {command}, input, &code);
  if (!out) return fail("cannot run " + path);
  if (code != 0) return fail(helper_message(*out, code));
  rapidjson::Document d;
  d.Parse(out->c_str(), out->size());
  if (d.HasParseError()) return fail("the secret store helper's answer is not JSON");
  return d;
}

// The connections the stored generic secrets are for: (kind, URI as
// stored, secret), in lookup order.
std::vector<std::tuple<const char *, std::string, const Secret_spec *>> stored_connections(
    const std::vector<Secret_spec> &specs) {
  std::vector<std::tuple<const char *, std::string, const Secret_spec *>> out;
  for (const auto &[kind, prefix] : k_prefixes) {
    const size_t n = std::strlen(prefix);
    for (const auto &s : specs) {
      if (s.type != "generic" || s.id.compare(0, n, prefix) != 0) continue;
      std::string uri = s.id.substr(n);
      // Older keys carried a folder: MCP:CONN:/Sandboxes:<uri>.
      if (!uri.empty() && uri[0] == '/') {
        const size_t colon = uri.find(':');
        if (colon != std::string::npos) uri.erase(0, colon + 1);
      }
      out.emplace_back(kind, std::move(uri), &s);
    }
  }
  return out;
}

}  // namespace

std::optional<std::string> Secret_store::get(const Secret_spec &spec, std::string *error) const {
  const auto d = ask(m_path, m_error, "get", spec_json(spec.type, &spec.id), error);
  if (!d) return std::nullopt;
  const auto it = d->IsObject() ? d->FindMember("Secret") : d->MemberEnd();
  if (!d->IsObject() || it == d->MemberEnd() || !it->value.IsString()) {
    if (error != nullptr) *error = "the secret store helper's answer has no secret";
    return std::nullopt;
  }
  return std::string(it->value.GetString(), it->value.GetStringLength());
}

std::optional<std::vector<Secret_spec>> Secret_store::list(const std::string &type,
                                                           std::string *error) const {
  const auto d = ask(m_path, m_error, "list", type.empty() ? std::string() : spec_json(type, nullptr),
                     error);
  if (!d) return std::nullopt;
  if (!d->IsArray()) {
    if (error != nullptr) *error = "the secret store helper's list is not a JSON array";
    return std::nullopt;
  }
  std::vector<Secret_spec> specs;
  for (const auto &e : d->GetArray()) {
    if (!e.IsObject()) continue;
    const auto t = e.FindMember("SecretType");
    const auto i = e.FindMember("SecretID");
    if (t == e.MemberEnd() || i == e.MemberEnd() || !t->value.IsString() || !i->value.IsString()) {
      continue;
    }
    specs.push_back({t->value.GetString(), i->value.GetString()});
  }
  return specs;
}

std::vector<Stored_connection> Secret_store::connections(std::string *error) const {
  std::vector<Stored_connection> out;
  const auto specs = list("generic", error);
  if (!specs) return out;
  for (auto &[kind, uri, spec] : stored_connections(*specs)) out.push_back({kind, std::move(uri)});
  return out;
}

std::optional<std::string> Secret_store::password_for(std::string_view uri, std::string *source,
                                                      std::string *error) const {
  const auto want = normalize_connection_uri(uri);
  if (!want) {
    if (error != nullptr) *error = "not a connection URI";
    return std::nullopt;
  }
  std::string list_error;
  const auto specs = list("generic", &list_error);
  if (specs) {
    for (const auto &[kind, stored, spec] : stored_connections(*specs)) {
      if (normalize_connection_uri(stored) != want) continue;
      if (auto secret = get(*spec, error)) {
        if (source != nullptr) *source = std::string(kind) + " connection " + stored;
        return decode_shell_secret(*secret);
      }
    }
  } else if (m_path.empty()) {
    if (error != nullptr) *error = list_error;
    return std::nullopt;
  }
  // A connection the shell itself stored the password of.
  if (const auto u = parse_connection_uri(uri)) {
    for (const auto &id : shell_password_secret_ids(*u)) {
      std::string ignored;
      if (auto secret = get({"password", id}, &ignored)) {
        if (source != nullptr) *source = "shell credential " + id;
        return secret;
      }
    }
  }
  // A helper that could not list says why, not "no password stored".
  if (error != nullptr) *error = specs ? "no password stored for " + *want : list_error;
  return std::nullopt;
}

}  // namespace mdbls
