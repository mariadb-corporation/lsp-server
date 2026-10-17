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

#ifndef MDBLS_SECRET_STORE_H_
#define MDBLS_SECRET_STORE_H_

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mdbls {

// Runs `program` with `args`, writes `input` to its stdin and returns what
// it writes to stdout; stderr is discarded.  `exit_code` gets its exit
// code.  nullopt when it cannot be started, or when it has not finished
// after `timeout` (it is killed then).  One program runs at a time, so no
// child inherits another's pipes.
std::optional<std::string> run_program(const std::string &program,
                                       const std::vector<std::string> &args,
                                       std::string_view input, int *exit_code,
                                       std::chrono::milliseconds timeout = std::chrono::seconds(60));

struct Secret_store_options {
  // The directory of MariaDB Shell's binaries, which holds the
  // mariadb-secret-store-<helper> programs; "" to look for mariadb-shell on
  // PATH.
  std::string shell_bin_dir;
  // The helper's name; "" for the shell's choice: the environment variable
  // MARIADB_SHELL_CREDENTIAL_STORE_HELPER, else credentialStore.helper in
  // the shell's options.json, else the platform's default (keychain,
  // windows-credential, login-path).
  std::string helper;
};

struct Secret_spec {
  std::string type;  // "generic", "password", or a tenant group
  std::string id;
};

// A connection stored by the MCP server or the GUI, as `connections` lists.
struct Stored_connection {
  std::string kind;  // "gui" or "mcp"
  std::string uri;   // as stored
};

// MariaDB Shell's secret store, spoken to directly through its helper
// programs, as the shell's own credential manager does
// (mysqlshdk/libs/secret-store-api): `<helper> get|list` with JSON on stdin
// and stdout.  Passwords are returned to the caller and logged nowhere.
class Secret_store {
 public:
  explicit Secret_store(Secret_store_options options = {});

  // The helper program, "" when there is none: `error()` says why.
  const std::string &helper_path() const { return m_path; }
  const std::string &helper_name() const { return m_name; }
  const std::string &error() const { return m_error; }

  std::optional<std::string> get(const Secret_spec &spec, std::string *error) const;
  std::optional<std::vector<Secret_spec>> list(const std::string &type, std::string *error) const;

  // The connections the MCP server and the GUI keep (GUI:CONN:<uri>,
  // MCP:CONN:<uri>, and the older GUI:Connection:, MCP:Connection:).
  std::vector<Stored_connection> connections(std::string *error) const;

  // The password of the connection `uri` names: the GUI's entry, then the
  // MCP server's, then one the shell stored when it connected itself.
  // Entries are matched by their normalized URI.  nullopt when none.
  // `source` (may be null) gets a description of where it was found.
  std::optional<std::string> password_for(std::string_view uri, std::string *source,
                                          std::string *error) const;

 private:
  std::string m_name;
  std::string m_path;
  std::string m_error;
};

// The shell's user configuration directory: MARIADB_SHELL_USER_CONFIG_HOME,
// else ~/.mariadb-shell (%APPDATA%\MariaDB\mariadb-shell on Windows).
std::string shell_user_config_dir();

// The directory a program named `name` is in, found on PATH with links
// resolved; "" when it is not.
std::string find_program_dir(const std::string &name);

// A secret the shell stored with store_secret(): 'r' + text, or 'b' +
// base64.  Anything else is returned as it is.
std::string decode_shell_secret(std::string_view stored);

}  // namespace mdbls

#endif  // MDBLS_SECRET_STORE_H_
