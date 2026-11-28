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

#include "mdbls/describe.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include "catalog_symbols.h"
#include "mdbls/catalog.h"
#include "mdbls/lsp/json_position.h"
#include "mdbls/syntax_check.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls {

namespace {

using Grammar = Catalog_symbols;

constexpr uint32_t npos = Syntax_tree::npos;

struct Parsed {
  Statement_check check;
  // A statement starting CREATE, ALTER, DROP, RENAME or USE (REST SQL too):
  // the only ones with more to describe than `other`, so the only ones
  // parsed again for a tree.  A dump's INSERTs keep none.
  bool read = false;
  bool accepted = false;
  Syntax_tree tree;
};

// Reads one accepted statement's tree.  Only the statement node's own
// children are looked at, and those of the rules that merely wrap its head
// (`wraps`), so nothing inside a routine body or a query is ever taken for
// the statement's kind, flags or names.
class Reader {
 public:
  Reader(const Syntax_tree &t, std::string_view text) : t_(t), text_(text), g_(catalog_symbols()) {}

  // `catalog`: the statements before this one, folded.
  void describe(Statement_description &d, const Catalog &catalog) {
    d.verb = "other";
    d.kind = "other";
    if (!catalog.current_schema().empty()) d.current_schema = catalog.current_schema();
    if (t_.roots().empty()) return;
    const uint32_t vc = t_.find(t_.roots()[0], g_.verb_clause);
    if (vc == npos || t_.children(vc).empty()) return;
    const uint32_t st = t_.children(vc)[0];
    const int sym = t_.node(st).symbol;
    if (sym == g_.mrs_statement) {
      if (!t_.children(st).empty()) rest_statement(d, t_.children(st)[0], catalog.rest_context());
      return;
    }
    if (sym == g_.create) d.verb = "create";
    else if (sym == g_.alter) d.verb = "alter";
    else if (sym == g_.drop) d.verb = "drop";
    else if (sym == g_.rename) d.verb = "rename";
    else if (sym == g_.use) d.verb = "use";
    else return;
    sql_statement(d, st);
  }

 private:
  const Syntax_tree &t_;
  std::string_view text_;
  const Grammar &g_;

  std::string_view raw(uint32_t n) const { return n == npos ? std::string_view() : t_.text(text_, n); }
  std::string text(uint32_t n) const { return collapse(raw(n)); }
  std::string name_of(uint32_t n) const { return unquote_identifier(collapse(raw(n))); }
  uint32_t child(uint32_t n, int sym) const { return n == npos ? npos : t_.child(n, sym); }
  bool non_empty(uint32_t n) const { return n != npos && t_.node(n).token_count > 0; }
  std::vector<uint32_t> all(uint32_t n, int sym) const { return n == npos ? std::vector<uint32_t>{} : t_.find_all(n, sym); }

  bool wraps(uint32_t n) const {
    const int s = t_.node(n).symbol;
    if (s == g_.create_routine || s == g_.drop_routine || s == g_.sp_handler || s == g_.table_or_tables ||
        s == g_.trigger_tail || s == g_.event_tail || s == g_.sp_tail_standalone)
      return true;
    const std::string_view name = t_.name(n);
    return name.starts_with("mrs_") && name.ends_with("_prefix");
  }
  // The children of `n` and of the head rules under it, in order.
  void heads(uint32_t n, std::vector<uint32_t> &out) const {
    for (const uint32_t c : t_.children(n)) {
      out.push_back(c);
      if (!t_.is_token(c) && wraps(c)) heads(c, out);
    }
  }
  std::vector<uint32_t> heads(uint32_t n) const {
    std::vector<uint32_t> out;
    heads(n, out);
    return out;
  }
  uint32_t head(const std::vector<uint32_t> &hs, int sym) const {
    for (const uint32_t h : hs) {
      if (t_.node(h).symbol == sym) return h;
    }
    return npos;
  }

  // `ident` or `ident . ident` children of `n` (table_ident, sp_name,
  // drop_routine).
  std::optional<Object_name> qualified(uint32_t n) const {
    if (n == npos) return std::nullopt;
    std::vector<uint32_t> ids;
    for (const uint32_t c : t_.children(n)) {
      if (t_.node(c).symbol == g_.ident) ids.push_back(c);
    }
    if (ids.empty()) return std::nullopt;
    Object_name o;
    if (ids.size() >= 2) o.schema = name_of(ids[ids.size() - 2]);
    o.name = name_of(ids.back());
    return o;
  }
  void add(Statement_description &d, std::optional<Object_name> name) {
    if (name) d.objects.push_back({std::move(*name), std::nullopt, std::nullopt});
  }

  void sql_statement(Statement_description &d, uint32_t st) {
    const auto hs = heads(st);
    for (const uint32_t h : hs) {
      if (!t_.is_token(h)) continue;
      const int s = t_.node(h).symbol;
      const char *kind = s == g_.TABLE_SYM || s == g_.TABLES ? "table"
                         : s == g_.VIEW_SYM                  ? "view"
                         : s == g_.SEQUENCE_SYM              ? "sequence"
                         : s == g_.PROCEDURE_SYM             ? "procedure"
                         : s == g_.FUNCTION_SYM              ? "function"
                         : s == g_.TRIGGER_SYM               ? "trigger"
                         : s == g_.EVENT_SYM                 ? "event"
                         : s == g_.INDEX_SYM                 ? "index"
                         : s == g_.DATABASE                  ? "schema"
                                                             : nullptr;
      if (kind != nullptr) {
        d.kind = kind;
        break;
      }
    }
    if (d.verb == "use") d.kind = "schema";

    const uint32_t cor = head(hs, g_.create_or_replace);
    d.or_replace = cor != npos && t_.find(cor, g_.OR_SYM) != npos;
    d.if_not_exists = non_empty(head(hs, g_.opt_if_not_exists));
    d.if_exists = non_empty(head(hs, g_.opt_if_exists)) || non_empty(head(hs, g_.opt_if_exists_table_element));
    d.temporary = non_empty(head(hs, g_.opt_temporary));

    const std::string &k = d.kind;
    if (k == "table" || k == "view" || k == "sequence") {
      if (d.verb == "drop") {
        for (const uint32_t ti : all(head(hs, g_.table_list), g_.table_ident)) add(d, qualified(ti));
      } else if (d.verb == "rename") {
        for (const uint32_t pair : all(head(hs, g_.table_to_table_list), g_.table_to_table)) {
          std::vector<uint32_t> tis;
          for (const uint32_t c : t_.children(pair)) {
            if (t_.node(c).symbol == g_.table_ident) tis.push_back(c);
          }
          if (tis.size() != 2) continue;
          d.objects.push_back({*qualified(tis[0]), std::nullopt, qualified(tis[1])});
        }
      } else {
        add(d, qualified(head(hs, g_.table_ident)));
        if (d.verb == "alter" && k == "table" && !d.objects.empty()) {
          // ALTER TABLE ... RENAME [TO] t2: the item with RENAME and a table.
          for (const uint32_t item : all(head(hs, g_.alter_commands), g_.alter_list_item)) {
            if (child(item, g_.RENAME) == npos) continue;
            if (const uint32_t to = child(item, g_.table_ident); to != npos) d.objects[0].new_name = qualified(to);
          }
        }
      }
    } else if (k == "procedure" || k == "function") {
      const uint32_t sp = head(hs, g_.sp_name);
      if (sp != npos) {
        add(d, qualified(sp));
      } else if (const uint32_t dr = head(hs, g_.drop_routine); dr != npos) {
        add(d, qualified(dr));
      } else if (const uint32_t cr = head(hs, g_.create_routine); cr != npos) {
        add(d, qualified(cr));  // a UDF: CREATE FUNCTION name RETURNS ... SONAME
      }
    } else if (k == "trigger" || k == "event") {
      add(d, qualified(head(hs, g_.sp_name)));
      if (k == "trigger" && !d.objects.empty()) {
        if (const uint32_t ti = head(hs, g_.table_ident); ti != npos) d.objects[0].table = qualified(ti);
      }
      if (k == "event" && d.verb == "alter" && !d.objects.empty()) {
        if (const uint32_t to = child(head(hs, g_.opt_ev_rename_to), g_.sp_name); to != npos)
          d.objects[0].new_name = qualified(to);
      }
    } else if (k == "index") {
      if (const uint32_t id = head(hs, g_.ident); id != npos) {
        Described_object o{{std::nullopt, name_of(id)}, qualified(head(hs, g_.table_ident)), std::nullopt};
        d.objects.push_back(std::move(o));
      }
    } else if (k == "schema") {
      uint32_t id = head(hs, g_.ident);
      if (id == npos) id = child(head(hs, g_.ident_or_empty), g_.ident);
      if (id != npos) d.objects.push_back({{std::nullopt, name_of(id)}, std::nullopt, std::nullopt});
    }
  }

  // The REST statement `st` (an mrs_*_statement), resolved against `rest`,
  // the service and schema in use before it, as the fold resolves them.
  void rest_statement(Statement_description &d, uint32_t st, const Rest_context &rest) {
    const std::string_view name = t_.name(st);
    Described_rest r;
    const uint32_t service_node = t_.find(st, g_.mrs_service_request_path);
    const uint32_t schema_node = t_.find(st, g_.mrs_schema_request_path);
    const Rest_service_name named_service =
        service_node == npos ? Rest_service_name{} : rest_service_name(t_, text_, service_node);
    const std::string named_schema = text(schema_node);  // "" for none
    // What the statement names, else what is in use: the fold's rule.
    auto service = [&]() -> std::optional<Described_rest_service> {
      const Rest_service_name *sv = rest.service_for(named_service);
      if (sv == nullptr) return std::nullopt;
      return Described_rest_service{sv->written_request_path, sv->developers, sv == &named_service};
    };
    auto schema = [&]() -> std::optional<Described_rest_schema> {
      const std::string *path = rest.schema_for(named_schema);
      if (path == nullptr) return std::nullopt;
      return Described_rest_schema{*path, path == &named_schema};
    };
    // Only what the statement names.
    auto written_service = [&]() { return service_node == npos ? std::nullopt : service(); };
    auto written_schema = [&]() { return schema_node == npos ? std::nullopt : schema(); };
    auto database = [&](uint32_t n) {
      if (n != npos) r.database_schema = name_of(n);
    };
    auto new_request_path = [&]() {
      const uint32_t to = child(child(st, g_.mrs_opt_new_request_path), g_.mrs_request_path_identifier);
      if (to != npos) r.new_path = text(to);
    };

    if (t_.node(st).symbol == g_.mrs_use_statement) {
      d.verb = "use";
      if (service_node == npos && schema_node == npos) {
        d.kind = "other";  // USE REST METADATA
      } else {
        d.kind = schema_node != npos ? "rest_schema" : "rest_service";
        r.service = service();
        r.schema = written_schema();
      }
      d.rest = std::move(r);
      return;
    }
    // mrs_<verb>_rest_<kind>_statement
    std::string verb, kind;
    for (const char *v : {"create", "alter", "drop"}) {
      const std::string prefix = std::string("mrs_") + v + "_rest_";
      if (name.starts_with(prefix) && name.ends_with("_statement")) {
        verb = v;
        kind = std::string(name.substr(prefix.size(), name.size() - prefix.size() - std::string_view("_statement").size()));
      }
    }
    static const char *const kinds[] = {"service", "schema",      "view",     "procedure", "function",
                                        "content_set", "content_file", "auth_app", "user",  "role"};
    d.rest = Described_rest{};
    if (verb.empty() || std::find(std::begin(kinds), std::end(kinds), kind) == std::end(kinds)) return;
    d.verb = verb;
    d.kind = "rest_" + kind;

    const auto hs = heads(st);
    for (const uint32_t h : hs) {
      if (t_.is_token(h) && t_.node(h).symbol == g_.OR_SYM) d.or_replace = true;
    }
    d.if_exists = non_empty(head(hs, g_.mrs_opt_if_exists));
    d.if_not_exists = non_empty(head(hs, g_.mrs_opt_if_not_exists));

    if (kind == "service") {
      r.service = written_service();
      if (const uint32_t to = child(st, g_.mrs_new_service_request_path); to != npos) r.new_path = text(to);
    } else if (kind == "schema") {
      r.service = service();
      if (verb == "alter") {
        r.schema = schema();
        new_request_path();
        database(child(child(st, g_.mrs_opt_from_schema_name), g_.mrs_identifier));
      } else if (verb == "create") {
        database(child(st, g_.mrs_identifier));
        r.schema = written_schema();
        if (!r.schema && r.database_schema)
          r.schema = Described_rest_schema{default_rest_schema_path(*r.database_schema), true};
      } else {
        r.schema = written_schema();
        database(child(child(st, g_.mrs_opt_for_database), g_.mrs_identifier));
      }
    } else if (kind == "view" || kind == "procedure" || kind == "function") {
      r.service = service();
      r.schema = schema();
      const int path_sym = kind == "view"        ? g_.mrs_view_request_path
                           : kind == "procedure" ? g_.mrs_procedure_request_path
                                                 : g_.mrs_function_request_path;
      if (const uint32_t p = t_.find(st, path_sym); p != npos) r.path = text(p);
      new_request_path();
    } else if (kind == "content_set" || kind == "content_file") {
      r.service = service();
      const int path_sym = kind == "content_set" ? g_.mrs_content_set_request_path : g_.mrs_content_file_request_path;
      if (const uint32_t p = t_.find(st, path_sym); p != npos) r.path = text(p);
    } else if (kind == "auth_app" || kind == "role") {
      const uint32_t p = t_.find(st, kind == "auth_app" ? g_.mrs_auth_app_name : g_.mrs_role_name);
      if (p != npos) d.objects.push_back({{std::nullopt, rest_name(text(p))}, std::nullopt, std::nullopt});
    } else if (kind == "user") {
      const uint32_t u = t_.find(st, g_.mrs_user_name);
      const uint32_t a = t_.find(st, g_.mrs_auth_app_name);
      if (u != npos && a != npos)
        d.objects.push_back({{std::nullopt, rest_name(text(u)) + "@" + rest_name(text(a))}, std::nullopt, std::nullopt});
    }
    d.rest = std::move(r);
  }
};

const char *status_name(Describe_status s) {
  switch (s) {
    case Describe_status::ok: return "ok";
    case Describe_status::syntax_error: return "syntax-error";
    case Describe_status::unsupported: return "unsupported";
    case Describe_status::source_directive: return "source-directive";
    case Describe_status::several_statements: return "several-statements";
    case Describe_status::empty: return "empty";
  }
  return "ok";
}

void write_optional(lsp::Json_writer &w, const std::optional<std::string> &s) {
  if (s) w.String(s->c_str(), static_cast<rapidjson::SizeType>(s->size()));
  else w.Null();
}

void write_name(lsp::Json_writer &w, const Object_name &n) {
  w.StartObject();
  w.Key("schema");
  write_optional(w, n.schema);
  w.Key("name");
  w.String(n.name.c_str(), static_cast<rapidjson::SizeType>(n.name.size()));
  w.EndObject();
}

}  // namespace

std::vector<Statement_description> describe_statements(std::string_view text, const Statement_index &index,
                                                       Parser_pool &pool, const Describe_options &options) {
  const auto &stmts = index.statements();
  std::vector<size_t> sql;
  for (size_t i = 0; i < stmts.size(); ++i) {
    if (stmts[i].kind == Statement_kind::sql) sql.push_back(i);
  }

  // Check and parse in parallel; read and fold in file order afterwards.
  std::vector<Parsed> parsed(sql.size());
  constexpr size_t k_per_job = 64;
  const size_t n_jobs = (sql.size() + k_per_job - 1) / k_per_job;
  pool.run_all(options.priority, n_jobs, [&](size_t job, Parser_cache &cache) {
    const size_t from = job * k_per_job;
    const size_t to = std::min(sql.size(), from + k_per_job);
    for (size_t k = from; k < to; ++k) {
      parsed[k].check = check_statement(text, index, sql[k], cache, options.version_id);
      if (parsed[k].check.status != Check_status::ok || parsed[k].check.has_source_directives) continue;
      const Statement &s = stmts[sql[k]];
      const auto stmt_text = index.text_of(text, s);
      parsed[k].read = may_change_catalog(stmt_text);
      if (!parsed[k].read) continue;
      mdbp_parser *p = cache.get(index.modes()[s.mode].mode, options.version_id, true);
      if (p == nullptr) continue;
      mdbp_result r;
      if (mdbp_parse_sql(p, stmt_text.data(), stmt_text.size(), &r) == MDBP_OK && r.accepted) {
        parsed[k].accepted = true;
        parsed[k].tree = Syntax_tree::build(p, r, s.begin);
      }
    }
  });

  std::vector<Statement_description> out;
  out.reserve(sql.size());
  Catalog catalog;
  for (size_t k = 0; k < sql.size(); ++k) {
    Statement_description d;
    d.statement = sql[k];
    const Statement &s = stmts[sql[k]];
    const Statement_check &c = parsed[k].check;
    switch (c.status) {
      case Check_status::ok:
        if (c.has_source_directives) {
          d.status = Describe_status::source_directive;
          d.message = "the statement holds an MSM SOURCE directive";
        } else if (parsed[k].read && !parsed[k].accepted) {
          d.status = Describe_status::unsupported;
          d.message = "the statement could not be parsed again";
        }
        break;
      case Check_status::syntax_error:
        d.status = Describe_status::syntax_error;
        d.message = syntax_error_message(text, c);
        d.error_begin = c.error_offset;
        d.error_end = c.error_offset + c.error_length;
        break;
      case Check_status::source_directive:
        d.status = Describe_status::source_directive;
        d.message = "an MSM SOURCE directive";
        break;
      case Check_status::invalid_utf8:
        d.status = Describe_status::unsupported;
        d.message = "the statement is not valid UTF-8";
        break;
      case Check_status::unsupported_mode:
        d.status = Describe_status::unsupported;
        d.message = "sql_mode " + index.modes()[s.mode].mode + " is not supported";
        break;
      case Check_status::internal_error:
      case Check_status::unchecked:
        d.status = Describe_status::unsupported;
        d.message = "the parser could not finish";
        break;
    }
    if (d.status == Describe_status::ok) {
      // A statement not read has no tree: `other`.
      Reader(parsed[k].tree, text).describe(d, catalog);
      if (parsed[k].accepted) catalog.apply(parsed[k].tree, text, index.modes()[s.mode].bits);
    }
    out.push_back(std::move(d));
  }
  return out;
}

namespace {

// A delimiter no item contains, so that each item ends where the text
// joining them says.
std::string unused_delimiter(const std::vector<std::string> &items) {
  for (unsigned n = 0;; ++n) {
    std::string d = "$$" + (n == 0 ? std::string() : std::to_string(n));
    if (std::none_of(items.begin(), items.end(), [&](const std::string &i) { return i.find(d) != std::string::npos; }))
      return d;
  }
}

// Item `it` holds `n` statements: no description, positions over the whole
// item.
void holds_several(Described_item &it, size_t n) {
  it.description = {};
  it.description.status = Describe_status::several_statements;
  it.description.message = "the item holds " + std::to_string(n) + " statements";
  it.begin = 0;
  it.body_end = it.end = static_cast<uint32_t>(it.text.size());
}

// An item as several `;`-separated statements, when each of them parses.
size_t separate_statements(const std::string &item, Parser_pool &pool, const Describe_options &options) {
  Index_options io;
  io.version_id = options.version_id;
  const auto index = Statement_index::build(item, io);
  const auto ds = describe_statements(item, index, pool, options);
  if (ds.size() < 2) return 0;
  for (const auto &d : ds) {
    if (d.status != Describe_status::ok) return 0;
  }
  return ds.size();
}

}  // namespace

std::vector<Described_item> describe_list(const std::vector<std::string> &items, Parser_pool &pool,
                                          const Describe_options &options) {
  std::vector<Described_item> out(items.size());
  const std::string delimiter = unused_delimiter(items);
  Index_options io;
  io.version_id = options.version_id;
  io.sql_mode = options.sql_mode;
  io.delimiter = delimiter;

  // The items joined, each followed by the delimiter on a line of its own.
  // One that would not end there (it changes the delimiter, or leaves a
  // string or comment open) is described alone and left out.
  std::string text;
  std::vector<std::pair<uint32_t, uint32_t>> range(items.size(), {0, 0});  // in `text`
  std::vector<bool> joined(items.size(), false);
  for (size_t i = 0; i < items.size(); ++i) {
    Described_item &it = out[i];
    it.text = items[i];
    it.end = it.body_end = static_cast<uint32_t>(it.text.size());
    const auto alone = Statement_index::build(it.text, io);
    Statement_description &d = it.description;
    if (alone.final_delimiter() != delimiter) {
      d.status = Describe_status::unsupported;
      d.message = "an item needs no DELIMITER: it is one statement";
      continue;
    }
    if (const char *open = open_context_name(alone.open_at_end())) {
      d.status = Describe_status::syntax_error;
      d.message = std::string("unterminated ") + open;
      d.error_end = it.end;
      continue;
    }
    joined[i] = true;
    range[i].first = static_cast<uint32_t>(text.size());
    text += it.text;
    range[i].second = static_cast<uint32_t>(text.size());
    text += "\n" + delimiter + "\n";
  }

  const auto index = Statement_index::build(text, io);
  const auto ds = describe_statements(text, index, pool, options);
  std::vector<std::vector<size_t>> of_item(items.size());
  size_t i = 0;
  for (size_t k = 0; k < ds.size(); ++k) {
    const uint32_t at = index.statements()[ds[k].statement].begin;
    while (i < items.size() && (!joined[i] || at >= range[i].second)) ++i;
    if (i < items.size() && at >= range[i].first) of_item[i].push_back(k);
  }
  for (i = 0; i < items.size(); ++i) {
    if (!joined[i]) continue;
    Described_item &it = out[i];
    if (of_item[i].empty()) {
      it.description.status = Describe_status::empty;
      it.description.message = "the item holds no statement";
    } else if (of_item[i].size() > 1) {
      holds_several(it, of_item[i].size());
    } else {
      const Statement_description &d = ds[of_item[i][0]];
      const Statement &s = index.statements()[d.statement];
      const uint32_t base = range[i].first;
      const uint32_t size = static_cast<uint32_t>(it.text.size());
      it.description = d;
      it.begin = s.begin - base;
      it.body_end = std::min(s.body_end - base, size);
      it.end = std::min(s.end - base, size);
      // body_end before a trailing `;`, as in a text, where it is the
      // delimiter.
      const auto blank_before = [&](uint32_t e) {
        while (e > it.begin && std::isspace(static_cast<unsigned char>(it.text[e - 1]))) --e;
        return e;
      };
      if (const uint32_t e = blank_before(it.body_end); e > it.begin && it.text[e - 1] == ';')
        it.body_end = blank_before(e - 1);
      if (d.status == Describe_status::syntax_error) {
        it.description.error_begin = std::min(d.error_begin - base, size);
        it.description.error_end = std::min(d.error_end - base, size);
        // `DROP VIEW a; DROP VIEW b` reads as one statement here.
        if (const size_t n = separate_statements(it.text, pool, options); n > 1) holds_several(it, n);
      }
    }
  }
  return out;
}

namespace {

// The members of one entry after its positions.
void write_entry_rest(lsp::Json_writer &w, const Statement_description &d, std::string_view text,
                      const Line_map &lines) {
  auto str = [&](const std::string &s) { w.String(s.c_str(), static_cast<rapidjson::SizeType>(s.size())); };
  const bool ok = d.status == Describe_status::ok;
  w.Key("status");
  w.String(status_name(d.status));
  if (!ok) {
    w.Key("message");
    str(d.message);
  }
  if (d.status == Describe_status::syntax_error) {
    w.Key("error_range");
    lsp::write_range(w, text, lines, d.error_begin, d.error_end);
  }
  w.Key("verb");
  if (ok) str(d.verb); else w.Null();
  w.Key("kind");
  if (ok) str(d.kind); else w.Null();
  w.Key("or_replace");
  w.Bool(d.or_replace);
  w.Key("if_exists");
  w.Bool(d.if_exists);
  w.Key("if_not_exists");
  w.Bool(d.if_not_exists);
  w.Key("temporary");
  w.Bool(d.temporary);
  w.Key("objects");
  w.StartArray();
  for (const auto &o : d.objects) {
    w.StartObject();
    w.Key("schema");
    write_optional(w, o.name.schema);
    w.Key("name");
    str(o.name.name);
    if (o.table) {
      w.Key("table");
      write_name(w, *o.table);
    }
    if (o.new_name) {
      w.Key("new_schema");
      write_optional(w, o.new_name->schema);
      w.Key("new_name");
      str(o.new_name->name);
    }
    w.EndObject();
  }
  w.EndArray();
  w.Key("current_schema");
  write_optional(w, d.current_schema);
  w.Key("rest");
  if (!d.rest) {
    w.Null();
  } else {
    const Described_rest &r = *d.rest;
    w.StartObject();
    w.Key("service");
    if (r.service) {
      w.StartObject();
      w.Key("path");
      str(r.service->path);
      w.Key("developers");
      w.StartArray();
      for (const auto &dev : r.service->developers) str(dev);
      w.EndArray();
      w.Key("written");
      w.Bool(r.service->written);
      w.EndObject();
    } else {
      w.Null();
    }
    w.Key("schema");
    if (r.schema) {
      w.StartObject();
      w.Key("path");
      str(r.schema->path);
      w.Key("written");
      w.Bool(r.schema->written);
      w.EndObject();
    } else {
      w.Null();
    }
    w.Key("path");
    write_optional(w, r.path);
    w.Key("new_path");
    write_optional(w, r.new_path);
    w.Key("database_schema");
    write_optional(w, r.database_schema);
    w.EndObject();
  }
}

}  // namespace

std::string describe_json(const std::vector<Statement_description> &descriptions, std::string_view text,
                          const Statement_index &index, const Line_map &lines) {
  rapidjson::StringBuffer buf;
  lsp::Json_writer w(buf);
  w.StartObject();
  w.Key("format");
  w.Uint(1);
  w.Key("statements");
  w.StartArray();
  for (const auto &d : descriptions) {
    const Statement &s = index.statements()[d.statement];
    w.StartObject();
    lsp::write_statement_bounds(w, text, lines, s.begin, s.body_end, s.end);
    write_entry_rest(w, d, text, lines);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

std::string describe_list_json(const std::vector<Described_item> &items) {
  rapidjson::StringBuffer buf;
  lsp::Json_writer w(buf);
  w.StartObject();
  w.Key("format");
  w.Uint(1);
  w.Key("statements");
  w.StartArray();
  for (size_t i = 0; i < items.size(); ++i) {
    const Described_item &it = items[i];
    const Line_map lines(it.text);
    w.StartObject();
    w.Key("index");
    w.Uint64(i);
    lsp::write_statement_bounds(w, it.text, lines, it.begin, it.body_end, it.end);
    write_entry_rest(w, it.description, it.text, lines);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

}  // namespace mdbls
