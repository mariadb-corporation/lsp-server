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

#include "mdbls/connection_uri.h"

#include <algorithm>
#include <cctype>

#include "text_util.h"

namespace mdbls {

namespace {

bool valid_scheme(std::string_view s) {
  if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0]))) return false;
  return std::all_of(s.begin(), s.end(), [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' || c == '.';
  });
}

}  // namespace

const std::string *Connection_uri::option(std::string_view key) const {
  for (const auto &[k, v] : options) {
    if (iequals(k, key)) return &v;
  }
  return nullptr;
}

bool is_protocol_scheme(std::string_view s) {
  return s == "mariadb" || s == "mariadb+ssh" || s == "mysql" || s == "mysql+ssh";
}

bool is_ssh_scheme(std::string_view s) { return s == "mariadb+ssh" || s == "mysql+ssh"; }

std::optional<Connection_uri> parse_connection_uri(std::string_view text) {
  text = trim(text);
  if (text.empty()) return std::nullopt;
  for (const char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) < 0x20) {
      return std::nullopt;
    }
  }
  Connection_uri u;
  if (const size_t sep = text.find("://"); sep != std::string_view::npos) {
    const auto scheme = text.substr(0, sep);
    if (!valid_scheme(scheme)) return std::nullopt;
    u.scheme = lower(scheme);
    text.remove_prefix(sep + 3);
  }
  // user[:password]@ -- an '@' before any '/', '?' or '('.
  const size_t at = text.find('@');
  const size_t stop = text.find_first_of("/?(");
  if (at != std::string_view::npos && (stop == std::string_view::npos || at < stop)) {
    const auto info = text.substr(0, at);
    const size_t colon = info.find(':');
    const auto user = percent_decode(info.substr(0, colon));
    if (!user) return std::nullopt;
    u.user = *user;
    if (colon != std::string_view::npos) {
      const auto pw = percent_decode(info.substr(colon + 1));
      if (!pw) return std::nullopt;
      u.password = *pw;
    }
    text.remove_prefix(at + 1);
  }
  // The target.
  if (!text.empty() && text.front() == '(') {
    const size_t close = text.find(')');
    if (close == std::string_view::npos) return std::nullopt;
    const auto path = percent_decode(text.substr(1, close - 1));
    if (!path || path->empty()) return std::nullopt;
    u.socket = *path;
    text.remove_prefix(close + 1);
  } else if (!text.empty() && text.front() == '[') {
    const size_t close = text.find(']');
    if (close == std::string_view::npos || close == 1) return std::nullopt;
    u.host = std::string(text.substr(1, close - 1));
    u.ipv6 = true;
    text.remove_prefix(close + 1);
  } else {
    const size_t end = std::min(text.size(), text.find_first_of(":/?"));
    const auto host = percent_decode(text.substr(0, end));
    if (!host || host->empty()) return std::nullopt;
    if (host->find('/') != std::string::npos || host->find('\\') != std::string::npos) {
      u.socket = *host;  // %2Ftmp%2Fmysql.sock
    } else {
      u.host = *host;
    }
    text.remove_prefix(end);
  }
  if (!text.empty() && text.front() == ':') {
    text.remove_prefix(1);
    size_t n = 0;
    unsigned port = 0;
    while (n < text.size() && std::isdigit(static_cast<unsigned char>(text[n]))) {
      port = port * 10 + static_cast<unsigned>(text[n] - '0');
      if (port > 65535) return std::nullopt;
      ++n;
    }
    if (n == 0 || !u.socket.empty()) return std::nullopt;
    u.port = port;
    text.remove_prefix(n);
  }
  if (!text.empty() && text.front() == '/') {
    text.remove_prefix(1);
    const size_t q = std::min(text.size(), text.find('?'));
    const auto schema = percent_decode(text.substr(0, q));
    if (!schema) return std::nullopt;
    u.schema = *schema;
    text.remove_prefix(q);
  }
  if (!text.empty() && text.front() == '?') {
    text.remove_prefix(1);
    while (!text.empty()) {
      const size_t amp = std::min(text.size(), text.find('&'));
      const auto pair = text.substr(0, amp);
      const size_t eq = pair.find('=');
      const auto key = percent_decode(pair.substr(0, eq));
      const auto value = eq == std::string_view::npos ? std::optional<std::string>("")
                                                      : percent_decode(pair.substr(eq + 1));
      if (!key || !value || key->empty()) return std::nullopt;
      u.options.emplace_back(lower(*key), *value);
      text.remove_prefix(std::min(text.size(), amp + 1));
    }
  }
  if (!text.empty()) return std::nullopt;
  return u;
}

std::string format_connection_uri(const Connection_uri &u) {
  std::string out = u.scheme + "://";
  if (!u.user.empty()) out += percent_encode(u.user) + "@";
  if (!u.socket.empty()) {
    out += percent_encode(u.socket);
  } else if (u.ipv6) {
    out += "[" + u.host + "]";
  } else {
    out += percent_encode(u.host);
  }
  if (u.port) out += ":" + std::to_string(*u.port);
  if (!u.schema.empty()) out += "/" + percent_encode(u.schema);
  for (size_t k = 0; k < u.options.size(); ++k) {
    out += (k == 0 ? "?" : "&") + percent_encode(u.options[k].first) + "=" +
           percent_encode(u.options[k].second, "/:");
  }
  return out;
}

std::optional<std::string> normalize_connection_uri(std::string_view text) {
  auto u = parse_connection_uri(text);
  if (!u) return std::nullopt;
  u->password.reset();
  u->host = lower(u->host);
  if (!u->host.empty() && !u->port && is_protocol_scheme(u->scheme)) u->port = 3306;
  std::stable_sort(u->options.begin(), u->options.end(),
                   [](const auto &a, const auto &b) { return a.first < b.first; });
  return format_connection_uri(*u);
}

std::vector<std::string> shell_password_secret_ids(const Connection_uri &u) {
  std::vector<std::string> out;
  const std::string user = percent_encode(u.user);
  if (!u.socket.empty()) {
    out.push_back(user + "@" + percent_encode(u.socket, "/"));
    out.push_back(user + "@(" + u.socket + ")");
    return out;
  }
  const std::string host = u.ipv6 ? "[" + u.host + "]" : u.host;
  if (u.port) out.push_back(user + "@" + host + ":" + std::to_string(*u.port));
  if (!u.port || *u.port == 3306) out.push_back(user + "@" + host);
  if (!u.port) out.push_back(user + "@" + host + ":3306");
  return out;
}

}  // namespace mdbls
