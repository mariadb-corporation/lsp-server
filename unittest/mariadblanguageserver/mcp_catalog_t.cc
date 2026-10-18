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

// The catalog read through MariaDB Shell's MCP server: the db.* tool
// results, as the MCP plugin shapes them, mapped onto the catalog model.

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "mdbls/mcp_catalog.h"

using mdbls::fetch_mcp_catalog;
using mdbls::Mcp_catalog_options;
using mdbls::Tool_caller;
using mdbls::Tool_failure;

namespace {

// The answers of a server with one user schema and one system schema, by
// tool and the arguments that matter.
const std::map<std::string, std::string> k_answers = {
    {"db.execute_sql",
     R"json({"result_sets":[{"columns":["sql_mode","version","schema_name"],"rows":[{"sql_mode":"ANSI_QUOTES,STRICT_TRANS_TABLES","version":"11.8.3-MariaDB","schema_name":"store"}]}]})json"},
    {"db.list_schemas",
     R"json([{"schema_name":"information_schema","schema_type":"System Information Schema","schema_comment":""},
         {"schema_name":"performance_schema","schema_type":"User Schema","schema_comment":""},
         {"schema_name":"store","schema_type":"User Schema","schema_comment":"the shop"}])json"},
    {"db.list_objects information_schema table", R"json([{"name":"TABLES","comment":""}])json"},
    {"db.list_objects information_schema view", R"json([])json"},
    {"db.list_objects store table", R"json([{"name":"item","comment":"for sale"},{"name":"sale","comment":""}])json"},
    {"db.list_objects store view", R"json([{"name":"cheap","comment":"VIEW"}])json"},
    {"db.list_objects store procedure", R"json([{"name":"restock"}])json"},
    {"db.list_objects store function", R"json([{"name":"price_of"}])json"},
    {"db.list_objects store trigger", R"json([{"name":"sale_bi"}])json"},
    {"db.list_objects store event", R"json([{"name":"nightly"}])json"},
    {"db.list_objects store sequence", R"json([{"name":"item_seq","datatype":"bigint(20)"}])json"},
    {"db.get_object_details store item table",
     R"json({"basic":{"schema":"store","name":"item","type":"table","comment":"for sale"},
         "columns":[
           {"name":"id","datatype":"int(11)","not_null":1,"is_primary":1,"is_unique":0,"is_generated":0,"id_generation":"auto_inc","comment":"","column_default":null,"charset":null,"collation":null},
           {"name":"label","datatype":"varchar(40)","not_null":1,"is_primary":0,"is_unique":1,"is_generated":0,"id_generation":null,"comment":"shown to buyers","column_default":"'none'","charset":"utf8mb4","collation":"utf8mb4_uca1400_ai_ci"},
           {"name":"price","datatype":"decimal(10,2)","not_null":0,"is_primary":0,"is_unique":0,"is_generated":0,"id_generation":null,"comment":"","column_default":null,"charset":null,"collation":null}],
         "constraints":[
           {"constraint_name":"PRIMARY","constraint_type":"PRIMARY KEY","column_name":"id"},
           {"constraint_name":"label","constraint_type":"UNIQUE","column_name":"label"}],
         "references":[
           {"position":1001,"name":"sale","ref_column_names":"item_id",
            "reference_mapping":{"kind":"1:n","constraint":"store.sale_ibfk_1","to_many":true,"referenced_schema":"store","referenced_table":"sale","column_mapping":[{"base":"id","ref":"item_id"}]},
            "table_schema":"store","table_name":"item"}]})json"},
    {"db.get_object_details store sale table",
     R"json({"basic":{"schema":"store","name":"sale","type":"table","comment":null},
         "columns":[
           {"name":"id","datatype":"int(11)","not_null":true,"is_primary":1,"is_unique":0,"is_generated":0,"id_generation":null,"comment":"","column_default":null},
           {"name":"item_id","datatype":"int(11)","not_null":0,"is_primary":0,"is_unique":0,"is_generated":0,"id_generation":null,"comment":"","column_default":null},
           {"name":"qty","datatype":"int(11)","not_null":0,"is_primary":0,"is_unique":0,"is_generated":0,"id_generation":null,"comment":"","column_default":"1"}],
         "constraints":[
           {"constraint_name":"PRIMARY","constraint_type":"PRIMARY KEY","column_name":"id"},
           {"constraint_name":"sale_ibfk_1","constraint_type":"FOREIGN KEY","column_name":"item_id"}],
         "references":[
           {"position":102,"name":"item","ref_column_names":"item_id",
            "reference_mapping":"{\"kind\":\"n:1\",\"constraint\":\"store.sale_ibfk_1\",\"to_many\":false,\"referenced_schema\":\"store\",\"referenced_table\":\"item\",\"column_mapping\":[{\"base\":\"item_id\",\"ref\":\"id\"}]}",
            "table_schema":"store","table_name":"sale"}]})json"},
    {"db.get_object_details store cheap view",
     R"json({"basic":{"schema":"store","name":"cheap","type":"view"},"columns":[{"name":"id","datatype":"int(11)"},{"name":"label","datatype":"varchar(40)"}]})json"},
    {"db.get_object_details store restock procedure",
     R"json({"basic":{"schema":"store","name":"restock","type":"procedure"},
         "parameters":[{"name":"n","mode":"IN","datatype":"int(11)","parameter_default":null},{"name":"done","mode":"OUT","datatype":"int(11)","parameter_default":null}],
         "returns":null})json"},
    {"db.get_object_details store price_of function",
     R"json({"basic":{"schema":"store","name":"price_of","type":"function"},
         "parameters":[{"name":"item","mode":"IN","datatype":"int(11)","parameter_default":null}],
         "returns":"decimal(10,2)"})json"},
    {"db.get_object_details store sale_bi trigger",
     R"json({"basic":{"schema":"store","name":"sale_bi","type":"trigger"},
         "details":{"event_manipulation":"INSERT","action_timing":"BEFORE","table_schema":"store","table_name":"sale"}})json"},
};

// A caller answering from the table, noting each call.
struct Fake_server {
  std::vector<std::string> calls;
  std::string fail_on;  // a call key that fails
  bool permanent = false;

  Tool_caller caller() {
    return [this](std::string_view tool, const std::string &arguments,
                  rapidjson::Document *out) -> std::optional<Tool_failure> {
      rapidjson::Document args;
      args.Parse(arguments.c_str());
      EXPECT_FALSE(args.HasParseError()) << arguments;
      std::string key(tool);
      for (const char *name : {"schema_name", "object_name", "object_type"}) {
        if (args.HasMember(name)) key += std::string(" ") + args[name].GetString();
      }
      calls.push_back(key);
      if (key == fail_on) return Tool_failure{"no such thing", permanent};
      const auto it = k_answers.find(key);
      if (it == k_answers.end()) return Tool_failure{"unexpected call " + key, true};
      out->Parse(it->second.c_str());
      EXPECT_FALSE(out->HasParseError()) << key;
      return std::nullopt;
    };
  }
};

}  // namespace

TEST(Mcp_catalog, maps_the_tool_results_onto_the_catalog) {
  Fake_server server;
  std::string error;
  bool permanent = true;
  const auto snap = fetch_mcp_catalog(server.caller(), &error, &permanent);
  ASSERT_TRUE(snap) << error;
  EXPECT_EQ(snap->server.sql_mode, "ANSI_QUOTES,STRICT_TRANS_TABLES");
  EXPECT_EQ(snap->server.version, "11.8.3-MariaDB");
  EXPECT_EQ(snap->server.version_id, 110803u);
  EXPECT_EQ(snap->server.current_schema, "store");
  EXPECT_EQ(snap->tables, 3u);
  EXPECT_EQ(snap->columns, 6u);

  // performance_schema is left out; the system schema is listed without
  // details, so none of its objects was asked about.
  ASSERT_EQ(snap->catalog->schemas().size(), 2u);
  EXPECT_EQ(snap->catalog->find("performance_schema"), nullptr);
  const auto *is = snap->catalog->find("information_schema");
  ASSERT_NE(is, nullptr);
  ASSERT_EQ(is->tables.size(), 1u);
  EXPECT_EQ(is->tables[0].name, "TABLES");
  EXPECT_TRUE(is->tables[0].columns.empty());
  for (const auto &call : server.calls) {
    EXPECT_EQ(call.find("db.get_object_details information_schema"), std::string::npos) << call;
    EXPECT_EQ(call.find("db.list_objects information_schema procedure"), std::string::npos) << call;
  }

  const auto *store = snap->catalog->find("store");
  ASSERT_NE(store, nullptr);
  EXPECT_EQ(store->options, (std::vector<std::pair<std::string, std::string>>{{"comment", "the shop"}}));
  const auto *item = store->table("item");
  ASSERT_NE(item, nullptr);
  EXPECT_EQ(item->options, (std::vector<std::pair<std::string, std::string>>{{"comment", "for sale"}}));
  ASSERT_EQ(item->columns.size(), 3u);
  EXPECT_EQ(item->columns[0].type, "int(11)");
  EXPECT_FALSE(item->columns[0].nullable);
  EXPECT_TRUE(item->columns[0].auto_increment);
  EXPECT_FALSE(item->columns[0].default_value);
  EXPECT_FALSE(item->columns[0].comment);
  EXPECT_EQ(item->columns[1].comment, "shown to buyers");
  EXPECT_EQ(item->columns[1].default_value, "'none'");
  EXPECT_EQ(item->columns[1].collation, "utf8mb4_uca1400_ai_ci");
  EXPECT_TRUE(item->columns[2].nullable);
  ASSERT_EQ(item->indexes.size(), 2u);
  EXPECT_EQ(item->indexes[0].kind, "primary");
  EXPECT_EQ(item->indexes[0].columns, std::vector<std::string>{"id"});
  EXPECT_EQ(item->indexes[1].kind, "unique");
  EXPECT_EQ(item->indexes[1].name, "label");
  // The 1:n reference is sale's key, not item's.
  EXPECT_TRUE(item->foreign_keys.empty());

  const auto *sale = store->table("sale");
  ASSERT_NE(sale, nullptr);
  EXPECT_FALSE(sale->columns[0].nullable);  // not_null as a boolean
  EXPECT_EQ(sale->columns[2].default_value, "1");
  ASSERT_EQ(sale->indexes.size(), 1u);  // the FOREIGN KEY constraint is no index
  ASSERT_EQ(sale->foreign_keys.size(), 1u);
  EXPECT_EQ(sale->foreign_keys[0].name, "sale_ibfk_1");
  EXPECT_EQ(sale->foreign_keys[0].columns, std::vector<std::string>{"item_id"});
  EXPECT_EQ(sale->foreign_keys[0].ref_schema, "store");
  EXPECT_EQ(sale->foreign_keys[0].ref_table, "item");
  EXPECT_EQ(sale->foreign_keys[0].ref_columns, std::vector<std::string>{"id"});

  const auto *cheap = store->view("cheap");
  ASSERT_NE(cheap, nullptr);
  EXPECT_EQ(cheap->columns, (std::vector<std::string>{"id", "label"}));

  ASSERT_EQ(store->routines.size(), 2u);
  EXPECT_EQ(store->routines[0].name, "restock");
  EXPECT_EQ(store->routines[0].kind, "procedure");
  EXPECT_EQ(store->routines[0].parameter_list(), "IN n int(11), OUT done int(11)");
  EXPECT_EQ(store->routines[0].returns, "");
  EXPECT_EQ(store->routines[1].kind, "function");
  EXPECT_EQ(store->routines[1].returns, "decimal(10,2)");

  ASSERT_EQ(store->triggers.size(), 1u);
  EXPECT_EQ(store->triggers[0].timing, "BEFORE");
  EXPECT_EQ(store->triggers[0].events, "INSERT");
  EXPECT_EQ(store->triggers[0].table, "sale");
  ASSERT_EQ(store->events.size(), 1u);
  EXPECT_EQ(store->events[0].name, "nightly");
  ASSERT_EQ(store->sequences.size(), 1u);
  EXPECT_EQ(store->sequences[0].name, "item_seq");
}

TEST(Mcp_catalog, a_failed_call_ends_the_reading_with_its_reason) {
  Fake_server server;
  server.fail_on = "db.get_object_details store sale table";
  server.permanent = true;
  std::string error;
  bool permanent = false;
  EXPECT_FALSE(fetch_mcp_catalog(server.caller(), &error, &permanent));
  EXPECT_EQ(error, "db.get_object_details: no such thing");
  EXPECT_TRUE(permanent);
  // Nothing after the failure was asked.
  EXPECT_EQ(server.calls.back(), "db.get_object_details store sale table");
}

TEST(Mcp_catalog, cancelled_between_calls) {
  Fake_server server;
  Mcp_catalog_options options;
  int calls = 0;
  options.cancelled = [&] { return ++calls > 3; };
  std::string error;
  bool permanent = true;
  EXPECT_FALSE(fetch_mcp_catalog(server.caller(), &error, &permanent, options));
  EXPECT_EQ(error, "cancelled");
  EXPECT_FALSE(permanent);
  EXPECT_EQ(server.calls.size(), 3u);
}
