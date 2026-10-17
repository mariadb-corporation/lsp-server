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

#ifndef MDBLS_CONNECTION_URI_H_
#define MDBLS_CONNECTION_URI_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mdbls {

// A connection URI in MariaDB Shell's syntax:
//
//   [scheme://][user[:password]@]target[/schema][?key=value&...]
//
// where the target is host[:port], [ipv6][:port], a socket path in
// parentheses or percent-encoded (%2Ftmp%2Fmysql.sock).  Components are
// kept decoded.
struct Connection_uri {
  std::string scheme = "mariadb";  // lower case
  std::string user;
  std::optional<std::string> password;
  std::string host;                // "" for a socket
  bool ipv6 = false;
  std::optional<unsigned> port;
  std::string socket;              // a socket path, "" for a host
  std::string schema;
  std::vector<std::pair<std::string, std::string>> options;  // keys lower case

  // The value of option `key` ("ssl-mode"), or nullptr.
  const std::string *option(std::string_view key) const;
};

// The schemes that speak the MariaDB protocol, and so default to port 3306.
bool is_protocol_scheme(std::string_view scheme);
// An SSH tunnel: mariadb+ssh, mysql+ssh.
bool is_ssh_scheme(std::string_view scheme);

// nullopt for text the shell would not take as a URI: empty, blanks inside,
// no host or socket, a bad port.
std::optional<Connection_uri> parse_connection_uri(std::string_view text);

// The URI, components percent-encoded, password left out.
std::string format_connection_uri(const Connection_uri &uri);

// The form connection URIs are compared in, as the MCP plugin's
// normalize_connection_uri() makes it: the scheme written out (mariadb when
// none), no password, the host lower case, port 3306 added for a host on a
// protocol scheme, options sorted.  Anything else a URI says (schema,
// options, scheme) is kept and has to match.  nullopt when not a URI.
std::optional<std::string> normalize_connection_uri(std::string_view text);

// The keys MariaDB Shell stores a password under for a connection it
// opened itself (secret type "password"): user@host:port, user@host,
// user@/socket, as its user_transport URI format writes them.
std::vector<std::string> shell_password_secret_ids(const Connection_uri &uri);

}  // namespace mdbls

#endif  // MDBLS_CONNECTION_URI_H_
