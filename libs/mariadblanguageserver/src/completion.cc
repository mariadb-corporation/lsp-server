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

#include "mdbls/completion.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <set>
#include <unordered_map>

#include "mdbls/keywords.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls {

namespace {

constexpr std::string_view k_sentinel = "mdb_cursor_";

bool is_ident_byte(char ch) {
  const auto c = static_cast<unsigned char>(ch);
  return std::isalnum(c) || c == '_' || c == '$' || c >= 0x80;
}

// What parsing a statement prefix says about the position after it.
struct Prefix_parse {
  enum class State { ok, error_before_end, in_literal, refused } state = State::refused;
  std::vector<std::string> tokens;  // terminal names, without END_OF_INPUT
  std::vector<int> expected;        // grammar symbols, when state is ok
};

std::optional<std::vector<int>> expected_of_tokens(mdbp_parser *p,
                                                   const std::vector<std::string> &names) {
  std::vector<mdbp_token> tokens;
  tokens.reserve(names.size() + 2);
  for (const auto &n : names) tokens.push_back({n.c_str(), n.c_str(), 0, -1, -1});
  tokens.push_back({"ABORT_SYM", "", 0, -1, -1});
  tokens.push_back({"END_OF_INPUT", "", 0, -1, -1});
  mdbp_result r;
  if (mdbp_parse_tokens(p, tokens.data(), tokens.size(), &r) != MDBP_OK || r.accepted ||
      r.error_token_index != names.size() || r.expected == nullptr) {
    return std::nullopt;
  }
  std::vector<int> out;
  out.reserve(r.nexpected);
  for (size_t i = 0; i < r.nexpected; ++i) out.push_back(grammar::symbol(r.expected[i]));
  return out;
}

// What the parser can take after a statement prefix and one more token,
// for many such tokens: the prefix's tokens are set up once.
class Follow_probe {
 public:
  Follow_probe(mdbp_parser *p, const std::vector<std::string> &names) : p_(p) {
    tokens_.reserve(names.size() + 3);
    for (const auto &n : names) tokens_.push_back({n.c_str(), n.c_str(), 0, -1, -1});
    slot_ = tokens_.size();
    tokens_.push_back({"IDENT", "", 0, -1, -1});
    tokens_.push_back({"ABORT_SYM", "", 0, -1, -1});
    tokens_.push_back({"END_OF_INPUT", "", 0, -1, -1});
  }

  // The terminal names the parser can take after the prefix and `terminal`,
  // sorted; nullopt when it cannot take `terminal` there.
  std::optional<std::vector<std::string_view>> after(const char *terminal) {
    tokens_[slot_].name = terminal;
    tokens_[slot_].text = terminal;
    mdbp_result r;
    if (mdbp_parse_tokens(p_, tokens_.data(), tokens_.size(), &r) != MDBP_OK || r.accepted ||
        r.error_token_index != slot_ + 1 || r.expected == nullptr) {
      return std::nullopt;
    }
    // The names are the grammar's own static strings.
    return std::vector<std::string_view>(r.expected, r.expected + r.nexpected);
  }

 private:
  mdbp_parser *p_;
  std::vector<mdbp_token> tokens_;
  size_t slot_ = 0;
};

// Whether each non-reserved keyword means something after a statement
// prefix, for the last few prefixes this thread completed after.
std::unordered_map<int, bool> &keyword_verdicts(const std::string &mode, unsigned long version_id,
                                                const std::vector<std::string> &tokens) {
  thread_local std::deque<std::pair<std::string, std::unordered_map<int, bool>>> cache;
  std::string key = mode + '\n' + std::to_string(version_id);
  for (const auto &t : tokens) key += '\n' + t;
  for (auto &e : cache) {
    if (e.first == key) return e.second;
  }
  if (cache.size() >= 8) cache.pop_back();
  cache.emplace_front(std::move(key), std::unordered_map<int, bool>{});
  return cache.front().second;
}

Prefix_parse parse_prefix(mdbp_parser *p, std::string_view sql) {
  Prefix_parse out;
  mdbp_result r;
  const mdbp_status st = mdbp_parse_sql(p, sql.data(), sql.size(), &r);
  if (st == MDBP_INTERNAL || r.tokens == nullptr) return out;
  // In an open string, or a comment (the lexer drops comments, so text
  // after the last token that is not blank is one).
  bool in_literal = r.eof_token != nullptr && std::string_view(r.eof_token) == "ABORT_SYM";
  size_t last_end = 0;
  for (size_t i = 0; i < r.ntokens; ++i) {
    if (r.tokens[i].byte_end > r.tokens[i].byte_start) last_end = static_cast<size_t>(r.tokens[i].byte_end);
  }
  in_literal = in_literal || !trim(sql.substr(std::min(last_end, sql.size()))).empty();
  if (in_literal) {
    out.state = Prefix_parse::State::in_literal;
    return out;
  }
  for (size_t i = 0; i < r.ntokens; ++i) {
    const std::string_view name = r.tokens[i].name != nullptr ? r.tokens[i].name : "";
    if (name == "END_OF_INPUT") break;
    out.tokens.emplace_back(name);
  }
  if (st == MDBP_UNSUPPORTED) return out;
  if (!r.accepted) {
    if (r.error_token_index + 1 < r.ntokens) {
      out.state = Prefix_parse::State::error_before_end;
      return out;
    }
    if (r.expected == nullptr) return out;
    for (size_t i = 0; i < r.nexpected; ++i) out.expected.push_back(grammar::symbol(r.expected[i]));
    out.state = Prefix_parse::State::ok;
    return out;
  }
  // A complete statement: what may follow it, asked with a token that is
  // never valid.
  if (auto e = expected_of_tokens(p, out.tokens)) {
    out.expected = std::move(*e);
    out.state = Prefix_parse::State::ok;
  }
  return out;
}

class Builder {
 public:
  explicit Builder(Completion_list &out) : out_(out) {}

  bool matches(std::string_view label) const { return istarts_with(label, out_.prefix); }

  void add(const std::string &label, Completion_kind kind, int rank, std::string detail = {},
           std::string doc = {}, std::string insert = {}, bool snippet = false) {
    if (!matches(label)) return;
    const std::string key = std::to_string(static_cast<int>(kind)) + lower(label);
    if (!seen_.insert(key).second) return;
    Completion_item it;
    it.label = label;
    it.kind = kind;
    it.detail = std::move(detail);
    it.documentation = std::move(doc);
    it.snippet = snippet;
    if (!insert.empty()) {
      it.insert_text = std::move(insert);
    } else if (kind == Completion_kind::keyword) {
      it.insert_text = label;
    } else if (out_.quoted) {
      it.insert_text = backtick_quote(label);
    } else {
      it.insert_text = quote_identifier(label);
    }
    char r[4] = {static_cast<char>('0' + rank / 10), static_cast<char>('0' + rank % 10), '_', 0};
    it.sort_text = r + lower(label);
    out_.items.push_back(std::move(it));
  }


 private:
  Completion_list &out_;
  std::set<std::string> seen_;
};

std::string layer_name(const Catalog_layer *l) {
  if (l == nullptr) return {};
  switch (l->kind) {
    case Layer_kind::document: return "this document";
    case Layer_kind::workspace: return "workspace";
    case Layer_kind::live: return "connection";
  }
  return {};
}

}  // namespace

const char *completion_kind_name(Completion_kind kind) {
  switch (kind) {
    case Completion_kind::keyword: return "keyword";
    case Completion_kind::schema: return "schema";
    case Completion_kind::table: return "table";
    case Completion_kind::view: return "view";
    case Completion_kind::column: return "column";
    case Completion_kind::alias: return "alias";
    case Completion_kind::function: return "function";
    case Completion_kind::routine: return "routine";
    case Completion_kind::variable: return "variable";
  }
  return "keyword";
}

std::optional<size_t> statement_at(const Statement_index &index, uint32_t offset) {
  const auto &st = index.statements();
  const size_t i = index.find(offset);
  if (i < st.size() && st[i].begin <= offset) {
    if (offset <= st[i].body_end) return i;
    return std::nullopt;  // in the delimiter
  }
  // After a statement with no delimiter yet, or right at its end.
  if (i > 0) {
    const auto &p = st[i - 1];
    if (p.kind == Statement_kind::sql && p.end == p.body_end && p.body_end <= offset) return i - 1;
  }
  return std::nullopt;
}

std::string schema_in_use(std::string_view text, const Statement_index &index, size_t i) {
  const auto &st = index.statements();
  for (size_t k = std::min(i, st.size()); k-- > 0;) {
    if (st[k].kind != Statement_kind::sql) continue;
    std::string_view s = index.text_of(text, st[k]);
    if (!starts_with_word(s, "USE")) continue;
    s.remove_prefix(3);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    size_t n = 0;
    if (!s.empty() && (s.front() == '`' || s.front() == '"')) {
      const char q = s.front();
      n = 1;
      while (n < s.size()) {
        if (s[n] == q) {
          if (n + 1 < s.size() && s[n + 1] == q) {
            n += 2;
            continue;
          }
          ++n;
          break;
        }
        ++n;
      }
    } else {
      while (n < s.size() && is_ident_byte(s[n])) ++n;
    }
    return unquote_name(s.substr(0, n));
  }
  return {};
}

std::optional<std::vector<int>> expected_after(mdbp_parser *parser, std::string_view sql) {
  auto p = parse_prefix(parser, sql);
  if (p.state != Prefix_parse::State::ok) return std::nullopt;
  return std::move(p.expected);
}

Completion_list complete(std::string_view text, const Statement_index &index, uint32_t offset,
                         const Catalog_view &catalogs, Parser_cache &cache,
                         const Completion_options &options) {
  Completion_list out;
  offset = std::min<uint32_t>(offset, static_cast<uint32_t>(text.size()));
  out.replace_begin = out.replace_end = offset;

  const auto &stmts = index.statements();
  const auto at = statement_at(index, offset);
  uint32_t begin = offset;  // the statement's first byte
  uint32_t body_end = offset;
  std::string mode;
  if (at) {
    const Statement &st = stmts[*at];
    if (st.kind == Statement_kind::comment) return out;
    begin = st.begin;
    body_end = std::max(st.body_end, offset);
    mode = index.modes()[st.mode].mode;
  } else {
    // Between statements: a new one starts here, in the mode in force.
    const size_t next = index.find(offset);
    if (next > 0) {
      mode = index.modes()[stmts[next - 1].mode].mode;
    } else if (next < stmts.size()) {
      mode = index.modes()[stmts[next].mode].mode;
    }
    // Text after the cursor up to the next statement is not ours.
  }

  // The word being typed: [word_begin, offset), and what follows it.
  uint32_t word_begin = offset;
  while (word_begin > begin && is_ident_byte(text[word_begin - 1])) --word_begin;
  bool quoted = false;
  if (word_begin > begin && text[word_begin - 1] == '`') {
    // An opening backtick: an even number of them before it.
    const auto before = text.substr(begin, word_begin - 1 - begin);
    if (std::count(before.begin(), before.end(), '`') % 2 == 0) {
      quoted = true;
      out.quoted = true;
      --word_begin;
    }
  }
  uint32_t word_end = offset;
  while (word_end < body_end && is_ident_byte(text[word_end])) ++word_end;
  if (quoted && word_end < body_end && text[word_end] == '`') ++word_end;
  out.replace_begin = word_begin;
  // Inside `...` with the closing backtick already there (editors close
  // them): the item replaces up to and with it, as it brings its own.
  if (quoted && word_end > offset && text[word_end - 1] == '`') out.replace_end = word_end;
  out.prefix = std::string(text.substr(word_begin + (quoted ? 1 : 0),
                                       offset - word_begin - (quoted ? 1 : 0)));
  // A word that starts with a digit is a number being typed.
  if (!quoted && !out.prefix.empty() && std::isdigit(static_cast<unsigned char>(out.prefix[0]))) {
    return out;
  }

  mdbp_parser *plain = cache.get(mode, options.version_id, false);
  if (plain == nullptr) return out;
  const std::string_view sql_prefix = text.substr(begin, word_begin - begin);
  const Prefix_parse pp = parse_prefix(plain, sql_prefix);
  if (pp.state == Prefix_parse::State::in_literal) return out;
  const bool keywords_known = pp.state == Prefix_parse::State::ok;

  static const int k_ident = grammar::symbol("IDENT");
  static const int k_ident_quoted = grammar::symbol("IDENT_QUOTED");
  const bool after_dot = word_begin > begin && text[word_begin - 1] == '.';
  const bool names_fit =
      after_dot || !keywords_known ||
      std::any_of(pp.expected.begin(), pp.expected.end(),
                  [](int s) { return s == k_ident || s == k_ident_quoted; });

  Builder b(out);

  // ---- names
  if (names_fit && at) {
    mdbp_parser *tp = cache.get(mode, options.version_id, true);
    // Offsets in `full` and `truncated` equal the document's up to the
    // sentinel, which sits at word_begin.
    std::string truncated(text.substr(begin, word_begin - begin));
    truncated += k_sentinel;
    const std::string full = truncated + std::string(text.substr(word_end, body_end - word_end));
    const Statement_names a(tp, full, begin);
    Name_ref ref;
    if (a.accepted()) {
      ref = a.name_at(word_begin);
    } else {
      const Statement_names bt(tp, truncated, begin);
      ref = bt.accepted() ? bt.name_at(word_begin) : a.name_at(word_begin);
    }
    out.context = ref.kind;
    if (ref.defining) out.context = Name_kind::none;

    // The tables a column can come from, found when needed.  Their ranges
    // past the sentinel are off by the sentinel's length; only the names
    // are used.
    std::optional<std::vector<Table_ref>> tables_found;
    auto tables = [&]() -> const std::vector<Table_ref> & {
      if (!tables_found) tables_found = a.parsed() ? a.tables_at(word_begin) : std::vector<Table_ref>{};
      return *tables_found;
    };

    struct Col {
      std::string name;
      std::string detail;
      std::string doc;
    };
    auto columns_of_table = [&](const Catalog_view::Table_hit &hit, const std::string &table,
                                std::vector<Col> *cols) {
      if (hit.table != nullptr) {
        for (const auto &c : hit.table->columns) {
          cols->push_back({c.name, c.attributes(),
                           "Column of `" + table + "`" +
                               (c.comment ? ": " + *c.comment : std::string())});
        }
      } else if (hit.view != nullptr) {
        for (const auto &c : hit.view->columns) cols->push_back({c, "", "Column of view `" + table + "`"});
      }
    };
    auto columns_of_ref = [&](const Table_ref &t, std::vector<Col> *cols) {
      if (t.derived) {
        for (const auto &c : t.columns) {
          cols->push_back({c, "", "Column of `" + t.visible_name() + "`"});
        }
        return;
      }
      columns_of_table(catalogs.find_table(t.schema, t.name), t.name, cols);
    };
    // One item per name (the builder drops repeats): the first table's.
    auto add_columns = [&](const std::vector<Col> &cols, int rank) {
      for (const auto &c : cols) b.add(c.name, Completion_kind::column, rank, c.detail, c.doc);
    };
    // The tables in scope by the name the query uses for them.
    auto add_visible_tables = [&](int rank) {
      for (const auto &t : tables()) {
        if (!t.visible_name().empty()) {
          b.add(t.visible_name(), Completion_kind::alias, rank,
                t.alias.empty() ? "table" : "alias of " + (t.name.empty() ? "a subquery" : t.name));
        }
      }
    };
    auto add_tables = [&](std::string_view schema, int rank) {
      catalogs.for_each_table(schema, [&](const Catalog_view::Table_hit &h) {
        if (!b.matches(h.table != nullptr ? h.table->name : h.view->name)) return;
        if (h.table != nullptr) {
          b.add(h.table->name, Completion_kind::table, rank,
                std::string(h.table->temporary ? "temporary table" : "table") +
                    (h.schema->name.empty() ? "" : " in " + h.schema->name),
                std::to_string(h.table->columns.size()) + " columns (" + layer_name(h.layer) + ")");
        } else {
          b.add(h.view->name, Completion_kind::view, rank,
                "view" + (h.schema->name.empty() ? std::string() : " in " + h.schema->name));
        }
      });
    };
    auto add_schemas = [&](int rank) {
      for (const auto &s : catalogs.schema_names()) b.add(s, Completion_kind::schema, rank, "schema");
    };
    auto add_routines = [&](std::string_view schema, const char *kind, int rank) {
      catalogs.for_each_routine(schema, [&](const Catalog_view::Routine_hit &h) {
        if (h.routine->kind != kind || !b.matches(h.routine->name)) return;
        std::string detail =
            std::string(kind) + " " + h.routine->name + "(" + h.routine->parameter_list() + ")";
        if (!h.routine->returns.empty()) detail += " RETURNS " + h.routine->returns;
        if (std::string_view(kind) == "function" && !quoted) {
          const std::string name = quote_identifier(h.routine->name);
          b.add(h.routine->name, Completion_kind::routine, rank, detail, {},
                options.snippets ? name + "($0)" : name, options.snippets);
        } else {
          b.add(h.routine->name, Completion_kind::routine, rank, detail);
        }
      });
    };
    auto add_builtins = [&](int rank) {
      if (quoted) return;
      for (const auto &f : builtin_functions()) {
        if (!b.matches(f.name)) continue;
        const std::string label = options.upper_case_keywords ? std::string(f.name) : lower(f.name);
        const auto nl = f.forms.find('\n');
        b.add(label, Completion_kind::function, rank,
              std::string(f.forms.substr(0, nl == std::string_view::npos ? f.forms.size() : nl)),
              builtin_markdown(f), options.snippets ? label + "($0)" : label, options.snippets);
      }
    };

    switch (ref.defining ? Name_kind::none : ref.kind) {
      case Name_kind::table:
        if (ref.qualifiers.empty()) {
          for (const auto &c : a.ctes()) {
            b.add(c.name, Completion_kind::table, 1, "common table expression");
          }
          add_tables("", 1);
          add_schemas(3);
        } else {
          add_tables(ref.qualifiers.back(), 1);
        }
        break;
      case Name_kind::schema:
        add_schemas(1);
        break;
      case Name_kind::routine:
        if (ref.qualifiers.empty()) {
          add_routines("", "procedure", 1);
          add_schemas(3);
        } else {
          add_routines(ref.qualifiers.back(), "procedure", 1);
        }
        break;
      case Name_kind::function:
        if (ref.qualifiers.empty()) {
          add_routines("", "function", 1);
          add_builtins(2);
        } else {
          add_routines(ref.qualifiers.back(), "function", 1);
        }
        break;
      case Name_kind::qualifier:
        add_visible_tables(1);
        break;
      case Name_kind::column: {
        std::vector<Col> cols;
        if (ref.has_owner) {
          const auto hit = catalogs.find_table(ref.owner_schema, ref.owner);
          columns_of_table(hit, ref.owner, &cols);
          if (cols.empty()) {
            // The table this statement creates: its own column definitions.
            for (const auto &n : a.names()) {
              if (n.kind == Name_kind::column && n.defining) cols.push_back({n.name, "", ""});
            }
            for (const auto &c : a.ctes()) {
              if (iequals(c.name, ref.owner)) {
                for (const auto &cn : c.columns) cols.push_back({cn, "", ""});
              }
            }
          }
          add_columns(cols, 1);
          break;
        }
        if (ref.qualifiers.size() == 1) {
          const std::string &q = ref.qualifiers[0];
          const Table_ref *t = table_for_qualifier(tables(), a.ctes(), q);
          bool found = t != nullptr;
          if (t != nullptr) {
            columns_of_ref(*t, &cols);
          } else {
            const auto hit = catalogs.find_table("", q);
            columns_of_table(hit, q, &cols);
            found = hit.table != nullptr || hit.view != nullptr;
          }
          add_columns(cols, 1);
          if (!found && catalogs.has_schema(q)) {
            // schema.table in an expression
            add_tables(q, 2);
          }
          break;
        }
        if (ref.qualifiers.size() == 2) {
          columns_of_table(catalogs.find_table(ref.qualifiers[0], ref.qualifiers[1]),
                           ref.qualifiers[1], &cols);
          add_columns(cols, 1);
          break;
        }
        for (const auto &t : tables()) columns_of_ref(t, &cols);
        add_columns(cols, 1);
        for (const auto &al : a.select_aliases_at(word_begin)) {
          b.add(al, Completion_kind::alias, 1, "select alias");
        }
        for (const auto &v : a.variables()) b.add(v.name, Completion_kind::variable, 2, v.type);
        add_visible_tables(3);
        add_routines("", "function", 4);
        add_builtins(5);
        break;
      }
      case Name_kind::none:
      case Name_kind::alias:
      case Name_kind::variable:
      case Name_kind::index:
      case Name_kind::other:
        break;
    }
  }

  // ---- keywords
  if (keywords_known && !after_dot && !quoted) {
    // A non-reserved keyword where a name may stand is offered only when
    // the parser goes on differently after it than after a name.
    // Each probe parses the prefix again, so only the keywords that match
    // what is typed are probed, and the verdicts are kept for the next
    // keystroke in the same word.  With nothing typed yet they are left
    // out and the list is marked incomplete: the client asks again after
    // the first letter.
    Follow_probe probe(plain, pp.tokens);
    std::optional<std::vector<std::string_view>> as_name;
    auto &verdicts = keyword_verdicts(mode, options.version_id, pp.tokens);
    auto meaningful = [&](int sym) {
      if (!names_fit || !is_nonreserved_keyword(sym)) return true;
      if (out.prefix.empty()) {
        out.incomplete = true;
        return false;
      }
      if (const auto it = verdicts.find(sym); it != verdicts.end()) return it->second;
      if (!as_name) as_name = probe.after("IDENT").value_or(std::vector<std::string_view>{});
      const auto after = probe.after(grammar::name(sym));
      const bool yes = after && *after != *as_name;
      verdicts.emplace(sym, yes);
      return yes;
    };
    for (const int sym : pp.expected) {
      const auto &spellings = keyword_spellings(sym);
      if (spellings.empty()) continue;
      bool any = false;
      for (const auto sp : spellings) any = any || b.matches(sp);
      if (!any || !meaningful(sym)) continue;
      for (const auto sp : spellings) {
        b.add(options.upper_case_keywords ? std::string(sp) : lower(sp), Completion_kind::keyword, 9);
      }
    }
  }

  std::sort(out.items.begin(), out.items.end(),
            [](const Completion_item &x, const Completion_item &y) { return x.sort_text < y.sort_text; });
  if (out.items.size() > options.max_items) {
    out.items.resize(options.max_items);
    out.incomplete = true;
  }
  return out;
}

}  // namespace mdbls
