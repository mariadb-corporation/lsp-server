/*
   Copyright (c) 2000, 2015, Oracle and/or its affiliates.
   Copyright (c) 2010, 2023, MariaDB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

/* DON'T EDIT THIS FILE. IT'S GENERATED.  EDIT sql_yacc.yy INSTEAD */

/**
  @defgroup Parser Parser
  @{
*/

%{ #include "stub.h"
/* stub.py rule preserve_parsing_place: the five spellings sql_yacc.yy
   uses to reach the SELECT_LEX whose parsing_place MariaDB's lexer
   reads, on src/stub.h's mdbp_ctx.  Lex and Select keep sql_yacc.yy:33
   and :35's shape; thd and lex are ours (the %parse-param is
   `mdbp_ctx *ctx`, and mdbp_ctx carries a self-pointer named ctx). */
#define thd ctx
#define lex ctx
#define Lex (thd->lex)
#define Select Lex->current_select
#define sel Select
%}






































































































































































%define api.value.type {int}



















































































































































































%define api.pure                                    /* We have threads */
%parse-param { mdbp_ctx *ctx }
%lex-param { mdbp_ctx *ctx }
/*
  We should not introduce any further shift/reduce conflicts.
*/


%expect 72




/*
   Comments for TOKENS.
   For each token, please include in the same line a comment that contains
   the following tags:
   SQL-2011-R : Reserved keyword as per SQL-2011
   SQL-2011-N : Non Reserved keyword as per SQL-2011
   SQL-2003-R : Reserved keyword as per SQL-2003
   SQL-2003-N : Non Reserved keyword as per SQL-2003
   SQL-1999-R : Reserved keyword as per SQL-1999
   SQL-1999-N : Non Reserved keyword as per SQL-1999
   MYSQL      : MySQL extention (unspecified)
   MYSQL-FUNC : MySQL extention, function
   INTERNAL   : Not a real token, lex optimization
   OPERATOR   : SQL operator
   FUTURE-USE : Reserved for future use

   This makes the code grep-able, and helps maintenance.
*/


%token  '@'

%token HINT_COMMENT

/*
  Special purpose tokens
*/
%token   ABORT_SYM              /* INTERNAL (used in lex) */
%token   IMPOSSIBLE_ACTION      /* To avoid warning for yyerrlab1 */
%token   FORCE_LOOKAHEAD        /* INTERNAL never returned by the lexer */
%token   END_OF_INPUT           /* INTERNAL */
%token    COLON_ORACLE_SYM       /* INTERNAL */
%token    PARAM_MARKER           /* INTERNAL */
%token   FOR_SYSTEM_TIME_SYM    /* INTERNAL */
%token   LEFT_PAREN_ALT         /* INTERNAL */
%token   LEFT_PAREN_WITH        /* INTERNAL */
%token   LEFT_PAREN_LIKE        /* INTERNAL */
%token   ORACLE_CONCAT_SYM      /* INTERNAL */
%token   PERCENT_ORACLE_SYM     /* INTERNAL */
%token   WITH_CUBE_SYM          /* INTERNAL */
%token   WITH_ROLLUP_SYM        /* INTERNAL */
%token   WITH_SYSTEM_SYM        /* INTERNAL */

/*
  Identifiers
*/
%token  IDENT
%token  IDENT_QUOTED
%token  LEX_HOSTNAME
%token  UNDERSCORE_CHARSET            /* _latin1 */


/*
  Literals
*/
%token  BIN_NUM                       /* LITERAL */
%token  DECIMAL_NUM                   /* LITERAL */
%token  FLOAT_NUM                     /* LITERAL */
%token  HEX_NUM                       /* LITERAL */
%token  HEX_STRING                    /* LITERAL */
%token  LONG_NUM                      /* LITERAL */
%token  NCHAR_STRING                  /* LITERAL */
%token  NUM                           /* LITERAL */
%token  TEXT_STRING                   /* LITERAL */
%token  ULONGLONG_NUM                 /* LITERAL */


/*
  Operators
*/
%token   AND_AND_SYM            /* OPERATOR */
%token   DOT_DOT_SYM            /* OPERATOR */
%token   EQUAL_SYM              /* OPERATOR */
%token   GE                     /* OPERATOR */
%token   LE                     /* OPERATOR */
%token   MYSQL_CONCAT_SYM       /* OPERATOR */
%token   NE                     /* OPERATOR */
%token   NOT2_SYM               /* OPERATOR */
%token   OR2_SYM                /* OPERATOR */
%token   SET_VAR                /* OPERATOR */
%token   SHIFT_LEFT             /* OPERATOR */
%token   SHIFT_RIGHT            /* OPERATOR */
%token   ARROW_SYM              /* OPERATOR */
%token   JSON_SEPARATOR_SYM            /* OPERATOR */
%token   JSON_UNQUOTED_SEPARATOR_SYM   /* OPERATOR */


/*
  Reserved keywords
*/
%token   ADD                           /* SQL-2003-R */
%token   ALL                           /* SQL-2003-R */
%token   ALTER                         /* SQL-2003-R */
%token   ANALYZE_SYM
%token   AND_SYM                       /* SQL-2003-R */
%token   ASC                           /* SQL-2003-N */
%token   ASENSITIVE_SYM                /* FUTURE-USE */
%token   AS                            /* SQL-2003-R */
%token   BEFORE_SYM                    /* SQL-2003-N */
%token   BETWEEN_SYM                   /* SQL-2003-R */
%token   BIGINT                        /* SQL-2003-R */
%token   BINARY                        /* SQL-2003-R */
%token   BIT_AND                       /* MYSQL-FUNC */
%token   BIT_OR                        /* MYSQL-FUNC */
%token   BIT_XOR                       /* MYSQL-FUNC */
%token   BLOB_MARIADB_SYM              /* SQL-2003-R */
%token   BLOB_ORACLE_SYM               /* Oracle-R   */
%token   BODY_ORACLE_SYM               /* Oracle-R   */
%token   BOTH                          /* SQL-2003-R */
%token   BY                            /* SQL-2003-R */
%token   CALL_SYM                      /* SQL-2003-R */
%token   CASCADE                       /* SQL-2003-N */
%token   CASE_SYM                      /* SQL-2003-R */
%token   CAST_SYM                      /* SQL-2003-R */
%token   CHANGE
%token   CHAR_SYM                      /* SQL-2003-R */
%token   CHECK_SYM                     /* SQL-2003-R */
%token   COLLATE_SYM                   /* SQL-2003-R */
%token   CONDITION_SYM                 /* SQL-2003-R, SQL-2008-R */
%token   CONSTRAINT                    /* SQL-2003-R */
%token   CONTINUE_MARIADB_SYM          /* SQL-2003-R, Oracle-R */
%token   CONTINUE_ORACLE_SYM           /* SQL-2003-R, Oracle-R */
%token   CONVERT_SYM                   /* SQL-2003-N */
%token   COUNT_SYM                     /* SQL-2003-N */
%token   CREATE                        /* SQL-2003-R */
%token   CROSS                         /* SQL-2003-R */
%token   CUME_DIST_SYM
%token   CURDATE                       /* MYSQL-FUNC */
%token   CURRENT_PATH                  /* SQL-2003-R */
%token   CURRENT_ROLE                  /* SQL-2003-R */
%token   CURRENT_USER                  /* SQL-2003-R */
%token   CURSOR_SYM                    /* SQL-2003-R */
%token   CURTIME                       /* MYSQL-FUNC */
%token   DATABASE
%token   DATABASES
%token   DATE_ADD_INTERVAL             /* MYSQL-FUNC */
%token   DATE_SUB_INTERVAL             /* MYSQL-FUNC */
%token   DAY_HOUR_SYM
%token   DAY_MICROSECOND_SYM
%token   DAY_MINUTE_SYM
%token   DAY_SECOND_SYM
%token   DECIMAL_SYM                   /* SQL-2003-R */
%token   DECLARE_MARIADB_SYM           /* SQL-2003-R */
%token   DECLARE_ORACLE_SYM            /* Oracle-R   */
%token   DEFAULT                       /* SQL-2003-R */
%token   DELETE_DOMAIN_ID_SYM
%token   DELETE_SYM                    /* SQL-2003-R */
%token   DENSE_RANK_SYM
%token   DENY
%token   DESCRIBE                      /* SQL-2003-R */
%token   DESC                          /* SQL-2003-N */
%token   DETERMINISTIC_SYM             /* SQL-2003-R */
%token   DISTINCT                      /* SQL-2003-R */
%token   DIV_SYM
%token   DO_DOMAIN_IDS_SYM
%token   DOUBLE_SYM                    /* SQL-2003-R */
%token   DROP                          /* SQL-2003-R */
%token   DUAL_SYM
%token   EACH_SYM                      /* SQL-2003-R */
%token   ELSEIF_MARIADB_SYM
%token   ELSE                          /* SQL-2003-R */
%token   ELSIF_ORACLE_SYM              /* PLSQL-R    */
%token   EMPTY_SYM                     /* SQL-2016-R */
%token   ENCLOSED
%token   ESCAPED
%token   EXCEPT_SYM                    /* SQL-2003-R */
%token   EXISTS                        /* SQL-2003-R */
%token   EXTRACT_SYM                   /* SQL-2003-N */
%token   FALSE_SYM                     /* SQL-2003-R */
%token   FETCH_SYM                     /* SQL-2003-R */
%token   FIRST_VALUE_SYM               /* SQL-2011 */
%token   FLOAT_SYM                     /* SQL-2003-R */
%token   FOREIGN                       /* SQL-2003-R */
%token   FOR_SYM                       /* SQL-2003-R */
%token   FROM
%token   FULLTEXT_SYM
%token   GOTO_ORACLE_SYM               /* Oracle-R   */
%token   GRANT                         /* SQL-2003-R */
%token   GROUP_CONCAT_SYM
%token   JSON_ARRAYAGG_SYM
%token   JSON_OBJECTAGG_SYM
%token   JSON_TABLE_SYM
%token   GROUP_SYM                     /* SQL-2003-R */
%token   HAVING                        /* SQL-2003-R */
%token   HOUR_MICROSECOND_SYM
%token   HOUR_MINUTE_SYM
%token   HOUR_SECOND_SYM
%token   IF_SYM
%token   IGNORE_DOMAIN_IDS_SYM
%token   IGNORE_SYM
%token   IGNORED_SYM
%token   INDEX_SYM
%token   INFILE
%token   INNER_SYM                     /* SQL-2003-R */
%token   INOUT_SYM                     /* SQL-2003-R */
%token   INSENSITIVE_SYM               /* SQL-2003-R */
%token   INSERT                        /* SQL-2003-R */
%token   IN_SYM                        /* SQL-2003-R */
%token   INTERSECT_SYM                 /* SQL-2003-R */
%token   INTERVAL_SYM                  /* SQL-2003-R */
%token   INTO                          /* SQL-2003-R */
%token   INT_SYM                       /* SQL-2003-R */
%token   IS                            /* SQL-2003-R */
%token   ITERATE_SYM
%token   JOIN_SYM                      /* SQL-2003-R */
%token   KEYS
%token   KEY_SYM                       /* SQL-2003-N */
%token   KILL_SYM
%token   LAG_SYM                       /* SQL-2011 */
%token   LEADING                       /* SQL-2003-R */
%token   LEAD_SYM                      /* SQL-2011 */
%token   LEAVE_SYM
%token   LEFT                          /* SQL-2003-R */
%token   LIKE                          /* SQL-2003-R */
%token   LIMIT
%token   LINEAR_SYM
%token   LINES
%token   LOAD
%token   LOCALTIMESTAMP                /* SQL-2003-R */
%token   LOCATOR_SYM                   /* SQL-2003-N */
%token   LOCK_SYM
%token   LONGBLOB
%token   LONG_SYM
%token   LONGTEXT
%token   LOOP_SYM
%token   LOW_PRIORITY
%token   MASTER_SSL_VERIFY_SERVER_CERT_SYM
%token   MATCH                         /* SQL-2003-R */
%token   MAX_SYM                       /* SQL-2003-N */
%token   MAXVALUE_SYM                  /* SQL-2003-N */
%token   MEDIAN_SYM
%token   MEDIUMBLOB
%token   MEDIUMINT
%token   MEDIUMTEXT
%token   MIN_SYM                       /* SQL-2003-N */
%token   MINUS_ORACLE_SYM              /* Oracle-R   */
%token   MINUTE_MICROSECOND_SYM
%token   MINUTE_SECOND_SYM
%token   MODIFIES_SYM                  /* SQL-2003-R */
%token   MOD_SYM                       /* SQL-2003-N */
%token   NATURAL                       /* SQL-2003-R */
%token   NEG
%token   NESTED_SYM                    /* SQL-2003-N */
%token   NOT_SYM                       /* SQL-2003-R */
%token   NO_WRITE_TO_BINLOG
%token   NOW_SYM
%token   NTH_VALUE_SYM                 /* SQL-2011 */
%token   NTILE_SYM
%token   NULL_SYM                      /* SQL-2003-R */
%token   NUMERIC_SYM                   /* SQL-2003-R */
%token   OLD_VALUE_SYM
%token   ON                            /* SQL-2003-R */
%token   OPTIMIZE
%token   OPTIONALLY
%token   ORDER_SYM                     /* SQL-2003-R */
%token   ORDINALITY_SYM                /* SQL-2003-N */
%token   OR_SYM                        /* SQL-2003-R */
%token   OTHERS_ORACLE_SYM             /* SQL-2011-N, PLSQL-R */
%token   OUTER
%token   OUTFILE
%token   OUT_SYM                       /* SQL-2003-R */
%token   OVER_SYM
%token   PACKAGE_ORACLE_SYM            /* Oracle-R   */
%token   PAGE_CHECKSUM_SYM
%token   PARSE_VCOL_EXPR_SYM
%token   PARTITION_SYM                 /* SQL-2003-R */
%token   PATH_SYM                      /* SQL-2003-N */
%token   PERCENTILE_CONT_SYM
%token   PERCENTILE_DISC_SYM
%token   PERCENT_RANK_SYM
%token   PORTION_SYM                   /* SQL-2016-R */
%token   POSITION_SYM                  /* SQL-2003-N */
%token   PRECISION                     /* SQL-2003-R */
%token   PRIMARY_SYM                   /* SQL-2003-R */
%token   PROCEDURE_SYM                 /* SQL-2003-R */
%token   PURGE
%token   RAISE_ORACLE_SYM              /* PLSQL-R    */
%token   RANGE_SYM                     /* SQL-2003-R */
%token   RANK_SYM
%token   READS_SYM                     /* SQL-2003-R */
%token   READ_SYM                      /* SQL-2003-N */
%token   READ_WRITE_SYM
%token   REAL                          /* SQL-2003-R */
%token   RECURSIVE_SYM
%token   REFERENCES                    /* SQL-2003-R */
%token   REF_SYM
%token   REF_SYSTEM_ID_SYM
%token   REGEXP
%token   RELEASE_SYM                   /* SQL-2003-R */
%token   RENAME
%token   REPEAT_SYM                    /* MYSQL-FUNC */
%token   REQUIRE_SYM
%token   RESIGNAL_SYM                  /* SQL-2003-R */
%token   RESTRICT
%token   RETURNING_SYM
%token   RETURN_MARIADB_SYM            /* SQL-2003-R, PLSQL-R */
%token   RETURN_ORACLE_SYM             /* SQL-2003-R, PLSQL-R */
%token   REVOKE                        /* SQL-2003-R */
%token   RIGHT                         /* SQL-2003-R */
%token   ROW_NUMBER_SYM
%token   ROWS_SYM                      /* SQL-2003-R */
%token   ROWTYPE_ORACLE_SYM            /* PLSQL-R    */
%token   SECOND_MICROSECOND_SYM
%token   SELECT_SYM                    /* SQL-2003-R */
%token   SENSITIVE_SYM                 /* FUTURE-USE */
%token   SEPARATOR_SYM
%token   SET                           /* SQL-2003-R */
%token   SHOW
%token   SIGNAL_SYM                    /* SQL-2003-R */
%token   SMALLINT                      /* SQL-2003-R */
%token   SPATIAL_SYM
%token   SPECIFIC_SYM                  /* SQL-2003-R */
%token   SQL_BIG_RESULT
%token   SQLEXCEPTION_SYM              /* SQL-2003-R */
%token   SQL_SMALL_RESULT
%token   SQLSTATE_SYM                  /* SQL-2003-R */
%token   SQL_SYM                       /* SQL-2003-R */
%token   SQLWARNING_SYM                /* SQL-2003-R */
%token   SSL_SYM
%token   STARTING
%token   STATS_AUTO_RECALC_SYM
%token   STATS_PERSISTENT_SYM
%token   STATS_SAMPLE_PAGES_SYM
%token   STDDEV_SAMP_SYM               /* SQL-2003-N */
%token   STD_SYM
%token   STRAIGHT_JOIN
%token   SUM_SYM                       /* SQL-2003-N */
%token   SYSDATE
%token   TABLE_SYM                     /* SQL-2003-R */
%token   TERMINATED
%token   THEN_SYM                      /* SQL-2003-R */
%token   TINYBLOB
%token   TINYINT
%token   TINYTEXT
%token   TO_SYM                        /* SQL-2003-R */
%token   TRAILING                      /* SQL-2003-R */
%token   TRIGGER_SYM                   /* SQL-2003-R */
%token   TRUE_SYM                      /* SQL-2003-R */
%token   UNDO_SYM                      /* FUTURE-USE */
%token   UNION_SYM                     /* SQL-2003-R */
%token   UNIQUE_SYM
%token   UNLOCK_SYM
%token   UNSIGNED
%token   UPDATE_SYM                    /* SQL-2003-R */
%token   USAGE                         /* SQL-2003-N */
%token   USE_SYM
%token   USING                         /* SQL-2003-R */
%token   UTC_DATE_SYM
%token   UTC_TIMESTAMP_SYM
%token   UTC_TIME_SYM
%token   VALUES_IN_SYM
%token   VALUES_LESS_SYM
%token   VALUES                        /* SQL-2003-R */
%token   VARBINARY
%token   VARCHAR                       /* SQL-2003-R */
%token   VARIANCE_SYM
%token   VAR_SAMP_SYM
%token   VARYING                       /* SQL-2003-R */
%token   VECTOR_SYM
%token   WHEN_SYM                      /* SQL-2003-R */
%token   WHERE                         /* SQL-2003-R */
%token   WHILE_SYM
%token   WITH                          /* SQL-2003-R */
%token   XOR
%token   YEAR_MONTH_SYM
%token   ZEROFILL


/*
  Keywords that have different reserved status in std/oracle modes.
*/
%token    BODY_MARIADB_SYM              // Oracle-R
%token    ELSEIF_ORACLE_SYM
%token    ELSIF_MARIADB_SYM             // PLSQL-R
%token    EXCEPTION_ORACLE_SYM          // SQL-2003-N, PLSQL-R
%token    GOTO_MARIADB_SYM              // Oracle-R
%token    NOCOPY_SYM                    // PLSQL-N
%token    OTHERS_MARIADB_SYM            // SQL-2011-N, PLSQL-R
%token    PACKAGE_MARIADB_SYM           // Oracle-R
%token    RAISE_MARIADB_SYM             // PLSQL-R
%token    RECORD_SYM
%token    ROWTYPE_MARIADB_SYM           // PLSQL-R
%token    ROWNUM_SYM                    /* Oracle-R */

/*
  SQL functions with a special syntax
*/
%token   REPLACE                        /* MYSQL-FUNC */
%token   SUBSTRING                      /* SQL-2003-N */
%token   TRIM                           /* SQL-2003-N */


/*
  Non-reserved keywords
*/

%token    ACCOUNT_SYM                   /* MYSQL */
%token    ACTION                        /* SQL-2003-N */
%token    ADMIN_SYM                     /* SQL-2003-N */
%token    ADDDATE_SYM                   /* MYSQL-FUNC */
%token    AFTER_SYM                     /* SQL-2003-N */
%token    AGAINST
%token    AGGREGATE_SYM
%token    ALGORITHM_SYM
%token    ALWAYS_SYM
%token    ANY_SYM                       /* SQL-2003-R */
%token    ARRAY_SYM
%token    ASCII_SYM                     /* MYSQL-FUNC */
%token    AT_SYM                        /* SQL-2003-R */
%token    ATOMIC_SYM                    /* SQL-2003-R */
%token    AUTHORS_SYM
%token    AUTHORIZATION_SYM             /* SQL-2003-R */
%token    AUTO_INC
%token    AUTO_SYM
%token    AVG_ROW_LENGTH
%token    AVG_SYM                       /* SQL-2003-N */
%token    BACKUP_SYM
%token    BEGIN_MARIADB_SYM             /* SQL-2003-R, PLSQL-R */
%token    BEGIN_ORACLE_SYM              /* SQL-2003-R, PLSQL-R */
%token    BINLOG_SYM
%token    BIT_SYM                       /* MYSQL-FUNC */
%token    BLOCK_SYM
%token    BOOL_SYM
%token    BOOLEAN_SYM                   /* SQL-2003-R, PLSQL-R */
%token    BTREE_SYM
%token    BYTE_SYM
%token    CACHE_SYM
%token    CASCADED                      /* SQL-2003-R */
%token    CATALOG_NAME_SYM              /* SQL-2003-N */
%token    CHAIN_SYM                     /* SQL-2003-N */
%token    CHANGED
%token    CHANNEL_SYM
%token    CHARSET
%token    CHECKPOINT_SYM
%token    CHECKSUM_SYM
%token    CIPHER_SYM
%token    CLASS_ORIGIN_SYM              /* SQL-2003-N */
%token    CLIENT_SYM
%token    CLOB_MARIADB_SYM              /* SQL-2003-R */
%token    CLOB_ORACLE_SYM               /* Oracle-R   */
%token    CLOSE_SYM                     /* SQL-2003-R */
%token    COALESCE                      /* SQL-2003-N */
%token    CODE_SYM
%token    COLLATION_SYM                 /* SQL-2003-N */
%token    COLUMNS
%token    COLUMN_ADD_SYM
%token    COLUMN_CHECK_SYM
%token    COLUMN_CREATE_SYM
%token    COLUMN_DELETE_SYM
%token    COLUMN_GET_SYM
%token    COLUMN_SYM                    /* SQL-2003-R */
%token    COLUMN_NAME_SYM               /* SQL-2003-N */
%token    COMMENT_SYM                   /* Oracle-R   */
%token    COMMITTED_SYM                 /* SQL-2003-N */
%token    COMMIT_SYM                    /* SQL-2003-R */
%token    COMPACT_SYM
%token    COMPLETION_SYM
%token    COMPRESSED_SYM
%token    CONCURRENT
%token    CONNECTION_SYM
%token    CONSISTENT_SYM
%token    CONSTRAINT_CATALOG_SYM        /* SQL-2003-N */
%token    CONSTRAINT_NAME_SYM           /* SQL-2003-N */
%token    CONSTRAINT_SCHEMA_SYM         /* SQL-2003-N */
%token    CONTAINS_SYM                  /* SQL-2003-N */
%token    CONTEXT_SYM
%token    CONTRIBUTORS_SYM
%token    CONVERSION_SYM
%token    CPU_SYM
%token    CUBE_SYM                      /* SQL-2003-R */
%token    CURRENT_SYM                   /* SQL-2003-R */
%token    CURRENT_POS_SYM
%token    CURSOR_NAME_SYM               /* SQL-2003-N */
%token    CYCLE_SYM
%token    DATA_SYM                      /* SQL-2003-N */
%token    DATETIME
%token    DATE_SYM                      /* SQL-2003-R, Oracle-R, PLSQL-R */
%token    DAY_SYM                       /* SQL-2003-R */
%token    DEALLOCATE_SYM                /* SQL-2003-R */
%token    DEFINER_SYM
%token    DELAYED_SYM
%token    DELAY_KEY_WRITE_SYM
%token    DIAGNOSTICS_SYM               /* SQL-2003-N */
%token    DIRECTORY_SYM
%token    DISABLE_SYM
%token    DISCARD
%token    DISK_SYM
%token    DO_SYM
%token    DUMPFILE
%token    DUPLICATE_SYM
%token    DYNAMIC_SYM                   /* SQL-2003-R */
%token    ENABLE_SYM
%token    END                           /* SQL-2003-R, PLSQL-R */
%token    ENDS_SYM
%token    ENGINES_SYM
%token    ENGINE_SYM
%token    ENUM
%token    ERROR_SYM
%token    ERRORS
%token    ESCAPE_SYM                    /* SQL-2003-R */
%token    EVENTS_SYM
%token    EVENT_SYM
%token    EVERY_SYM                     /* SQL-2003-N */
%token    EXCHANGE_SYM
%token    EXAMINED_SYM
%token    EXCLUDE_SYM                   /* SQL-2011-N */
%token    EXECUTE_SYM                   /* SQL-2003-R */
%token    EXCEPTION_MARIADB_SYM         /* SQL-2003-N, PLSQL-R */
%token    EXIT_MARIADB_SYM              /* PLSQL-R */
%token    EXIT_ORACLE_SYM               /* PLSQL-R */
%token    EXPANSION_SYM
%token    EXPIRE_SYM                    /* MySQL */
%token    EXPORT_SYM
%token    EXTENDED_SYM
%token    EXTENT_SIZE_SYM
%token    FAST_SYM
%token    FAULTS_SYM
%token    FEDERATED_SYM                 /* MariaDB privilege */
%token    FILE_SYM
%token    FIRST_SYM                     /* SQL-2003-N */
%token    FIXED_SYM
%token    FLUSH_SYM
%token    FOLLOWS_SYM                   /* MYSQL trigger*/
%token    FOLLOWING_SYM                 /* SQL-2011-N */
%token    FORCE_SYM
%token    FORMAT_SYM
%token    FOUND_SYM                     /* SQL-2003-R */
%token    FULL                          /* SQL-2003-R */
%token    FUNCTION_SYM                  /* SQL-2003-R, Oracle-R */
%token    GENERAL
%token    GENERATED_SYM
%token    GET_FORMAT                    /* MYSQL-FUNC */
%token    GET_SYM                       /* SQL-2003-R */
%token    GLOBAL_SYM                    /* SQL-2003-R */
%token    GRANTS
%token    HANDLER_SYM
%token    HARD_SYM
%token    HASH_SYM
%token    HELP_SYM
%token    HIGH_PRIORITY
%token    HISTORY_SYM                   /* MYSQL */
%token    HOST_SYM
%token    HOSTS_SYM
%token    HOUR_SYM                      /* SQL-2003-R */
%token    ID_SYM                        /* MYSQL */
%token    IDENTIFIED_SYM
%token    IGNORE_SERVER_IDS_SYM
%token    IMMEDIATE_SYM                 /* SQL-2003-R */
%token    IMPORT
%token    INCREMENT_SYM
%token    INDEXES
%token    INSERT_METHOD
%token    INSTALL_SYM
%token    INVOKER_SYM
%token    IO_SYM
%token    IPC_SYM
%token    ISOLATION                     /* SQL-2003-R */
%token    ISOPEN_SYM                    /* Oracle-N   */
%token    ISSUER_SYM
%token    INVISIBLE_SYM
%token    JSON_SYM
%token    KEY_BLOCK_SIZE
%token    LANGUAGE_SYM                  /* SQL-2003-R */
%token    LAST_SYM                      /* SQL-2003-N */
%token    LAST_VALUE
%token    LASTVAL_SYM                   /* PostgreSQL sequence function */
%token    LEAVES
%token    LESS_SYM
%token    LEVEL_SYM
%token    LIST_SYM
%token    LOCAL_SYM                     /* SQL-2003-R */
%token    LOCKED_SYM
%token    LOCKS_SYM
%token    LOGS_SYM
%token    LOG_SYM
%token    MASTER_CONNECT_RETRY_SYM
%token    MASTER_DELAY_SYM
%token    MASTER_GTID_POS_SYM
%token    MASTER_HOST_SYM
%token    MASTER_LOG_FILE_SYM
%token    MASTER_LOG_POS_SYM
%token    MASTER_PASSWORD_SYM
%token    MASTER_PORT_SYM
%token    MASTER_RETRY_COUNT_SYM
%token    MASTER_SERVER_ID_SYM
%token    MASTER_SSL_CAPATH_SYM
%token    MASTER_SSL_CA_SYM
%token    MASTER_SSL_CERT_SYM
%token    MASTER_SSL_CIPHER_SYM
%token    MASTER_SSL_CRL_SYM
%token    MASTER_SSL_CRLPATH_SYM
%token    MASTER_SSL_KEY_SYM
%token    MASTER_SSL_SYM
%token    MASTER_SYM
%token    MASTER_USER_SYM
%token    MASTER_USE_GTID_SYM
%token    MASTER_HEARTBEAT_PERIOD_SYM
%token    MASTER_DEMOTE_TO_SLAVE_SYM
%token    MAX_CONNECTIONS_PER_HOUR
%token    MAX_QUERIES_PER_HOUR
%token    MAX_ROWS
%token    MAX_UPDATES_PER_HOUR
%token    MAX_STATEMENT_TIME_SYM
%token    MAX_USER_CONNECTIONS_SYM
%token    MEDIUM_SYM
%token    MEMORY_SYM
%token    MERGE_SYM                     /* SQL-2003-R */
%token    MESSAGE_TEXT_SYM              /* SQL-2003-N */
%token    MICROSECOND_SYM               /* MYSQL-FUNC */
%token    MIGRATE_SYM
%token    MINUTE_SYM                    /* SQL-2003-R */
%token    MINVALUE_SYM
%token    MIN_ROWS
%token    MODE_SYM
%token    MODIFY_SYM
%token    MONITOR_SYM                   /* MariaDB privilege */
%token    MONTH_SYM                     /* SQL-2003-R */
%token    MUTEX_SYM
%token    MYSQL_SYM
%token    MYSQL_ERRNO_SYM
%token    NAMES_SYM                     /* SQL-2003-N */
%token    NAME_SYM                      /* SQL-2003-N */
%token    NATIONAL_SYM                  /* SQL-2003-R */
%token    NCHAR_SYM                     /* SQL-2003-R */
%token    NEVER_SYM                     /* MySQL */
%token    NEXT_SYM                      /* SQL-2003-N */
%token    NEXTVAL_SYM                   /* PostgreSQL sequence function */
%token    NOCACHE_SYM
%token    NOCYCLE_SYM
%token    NODEGROUP_SYM
%token    NONE_SYM                      /* SQL-2003-R */
%token    NOTFOUND_SYM                  /* Oracle-R   */
%token    NO_SYM                        /* SQL-2003-R */
%token    NOMAXVALUE_SYM
%token    NOMINVALUE_SYM
%token    NOWAIT_SYM
%token    NUMBER_MARIADB_SYM            /* SQL-2003-N  */
%token    NUMBER_ORACLE_SYM             /* Oracle-R, PLSQL-R */
%token    NUMTODSINTERVAL_SYM
%token    NUMTOYMINTERVAL_SYM
%token    NVARCHAR_SYM
%token    OBJECT_SYM
%token    OF_SYM                        /* SQL-1992-R, Oracle-R */
%token    OFFSET_SYM
%token    OLD_PASSWORD_SYM
%token    ONE_SYM
%token    ONLY_SYM                      /* SQL-2003-R */
%token    ONLINE_SYM
%token    OPEN_SYM                      /* SQL-2003-R */
%token    OPTIONS_SYM
%token    OPTION                        /* SQL-2003-N */
%token    OVERLAPS_SYM
%token    OWNER_SYM
%token    PACK_KEYS_SYM
%token    PAGE_SYM
%token    PARSER_SYM
%token    PARTIAL                       /* SQL-2003-N */
%token    PARTITIONS_SYM
%token    PARTITIONING_SYM
%token    PASSWORD_SYM
%token    PERIOD_SYM                    /* SQL-2011-R */
%token    PERSISTENT_SYM
%token    PHASE_SYM
%token    PLUGINS_SYM
%token    PLUGIN_SYM
%token    PORT_SYM
%token    PRECEDES_SYM                  /* MYSQL */
%token    PRECEDING_SYM                 /* SQL-2011-N */
%token    PREPARE_SYM                   /* SQL-2003-R */
%token    PRESERVE_SYM
%token    PREV_SYM
%token    PREVIOUS_SYM
%token    PRIVILEGES                    /* SQL-2003-N */
%token    PROCESS
%token    PROCESSLIST_SYM
%token    PROFILE_SYM
%token    PROFILES_SYM
%token    PROXY_SYM
%token    QUARTER_SYM
%token    QUERY_SYM
%token    QUICK
%token    RAW_MARIADB_SYM
%token    RAW_ORACLE_SYM                /* Oracle-R */
%token    READ_ONLY_SYM
%token    REBUILD_SYM
%token    RECOVER_SYM
%token    REDUNDANT_SYM
%token    RELAY
%token    RELAYLOG_SYM
%token    RELAY_LOG_FILE_SYM
%token    RELAY_LOG_POS_SYM
%token    RELAY_THREAD
%token    RELOAD
%token    REMOVE_SYM
%token    REORGANIZE_SYM
%token    REPAIR
%token    REPEATABLE_SYM                /* SQL-2003-N */
%token    REPLAY_SYM                    /* MariaDB privilege */
%token    REPLICATION
%token    RESET_SYM
%token    RESTART_SYM
%token    RESOURCES
%token    RESUME_SYM
%token    RETURNED_SQLSTATE_SYM         /* SQL-2003-N */
%token    RETURNS_SYM                   /* SQL-2003-R */
%token    REUSE_SYM                     /* Oracle-R   */
%token    REVERSE_SYM
%token    ROLE_SYM
%token    ROLLBACK_SYM                  /* SQL-2003-R */
%token    ROLLUP_SYM                    /* SQL-2003-R */
%token    ROUTINE_SYM                   /* SQL-2003-N */
%token    ROWCOUNT_SYM                  /* Oracle-N   */
%token    ROW_SYM                       /* SQL-2003-R */
%token    ROW_COUNT_SYM                 /* SQL-2003-N */
%token    ROW_FORMAT_SYM
%token    RTREE_SYM
%token    SAVEPOINT_SYM                 /* SQL-2003-R */
%token    SCALAR_SYM
%token    SCHEDULE_SYM
%token    SCHEMA_NAME_SYM               /* SQL-2003-N */
%token    SECOND_SYM                    /* SQL-2003-R */
%token    SECURITY_SYM                  /* SQL-2003-N */
%token    SEQUENCE_SYM
%token    SERIALIZABLE_SYM              /* SQL-2003-N */
%token    SERIAL_SYM
%token    SESSION_SYM                   /* SQL-2003-N */
%token    SESSION_USER_SYM              /* SQL-2003-R */
%token    SERVER_SYM
%token    SETVAL_SYM                    /* PostgreSQL sequence function */
%token    SHARE_SYM
%token    SHUTDOWN
%token    SIGNED_SYM
%token    SIMPLE_SYM                    /* SQL-2003-N */
%token    SKIP_SYM
%token    SLAVE
%token    SLAVES
%token    SLAVE_POS_SYM
%token    SLOW
%token    SNAPSHOT_SYM
%token    SOCKET_SYM
%token    SOFT_SYM
%token    SONAME_SYM
%token    SOUNDS_SYM
%token    SOURCE_SYM
%token    SQL_AFTER_GTIDS_SYM
%token    SQL_BEFORE_GTIDS_SYM
%token    SQL_BUFFER_RESULT
%token    SQL_CACHE_SYM
%token    SQL_CALC_FOUND_ROWS
%token    SQL_NO_CACHE_SYM
%token    SQL_THREAD
%token    STAGE_SYM
%token    STARTS_SYM
%token    START_SYM                     /* SQL-2003-R */
%token    STATEMENT_SYM
%token    STATUS_SYM
%token    STOP_SYM
%token    STORAGE_SYM
%token    STORED_SYM
%token    STRING_SYM
%token    SUBCLASS_ORIGIN_SYM           /* SQL-2003-N */
%token    SUBDATE_SYM
%token    SUBJECT_SYM
%token    SUBPARTITIONS_SYM
%token    SUBPARTITION_SYM
%token    SUPER_SYM
%token    SUSPEND_SYM
%token    SWAPS_SYM
%token    SWITCHES_SYM
%token    SYSTEM                        /* SQL-2011-R */
%token    SYSTEM_TIME_SYM               /* SQL-2011-R */
%token    TABLES
%token    TABLESPACE
%token    TABLE_CHECKSUM_SYM
%token    TABLE_NAME_SYM                /* SQL-2003-N */
%token    TEMPORARY                     /* SQL-2003-N */
%token    TEMPTABLE_SYM
%token    TEXT_SYM
%token    THAN_SYM
%token    TIES_SYM                      /* SQL-2011-N */
%token    TIMESTAMP                     /* SQL-2003-R */
%token    TIMESTAMP_ADD
%token    TIMESTAMP_DIFF
%token    TIME_SYM                      /* SQL-2003-R, Oracle-R */
%token    TRANSACTION_SYM
%token    TRANSACTIONAL_SYM
%token    THREADS_SYM
%token    TO_DATE                       /* PLSQL function */
%token    TRIGGERS_SYM
%token    TRIM_ORACLE
%token    TRUNCATE_SYM
%token    TYPE_SYM                      /* SQL-2003-N */
%token    UNBOUNDED_SYM                 /* SQL-2011-N */
%token    UNCOMMITTED_SYM               /* SQL-2003-N */
%token    UNDEFINED_SYM
%token    UNICODE_SYM
%token    UNINSTALL_SYM
%token    UNKNOWN_SYM                   /* SQL-2003-R */
%token    UNTIL_SYM
%token    UPGRADE_SYM
%token    USER_SYM                      /* SQL-2003-R */
%token    USE_FRM
%token    VALIDATION_SYM                /* MYSQL */
%token    VALUE_SYM                     /* SQL-2003-R */
%token    VARCHAR2_MARIADB_SYM
%token    VARCHAR2_ORACLE_SYM           /* Oracle-R, PLSQL-R */
%token    VARIABLES
%token    VERSIONING_SYM                /* SQL-2011-R */
%token    VIA_SYM
%token    VIEW_SYM                      /* SQL-2003-N */
%token    VISIBLE_SYM                   /* MySQL 8.0 */
%token    VIRTUAL_SYM
%token    WAIT_SYM
%token    WARNINGS
%token    WEEK_SYM
%token    WEIGHT_STRING_SYM
%token    WINDOW_SYM                    /* SQL-2003-R */
%token    WITHIN
%token    WITHOUT                       /* SQL-2003-R */
%token    WORK_SYM                      /* SQL-2003-N */
%token    WRAPPER_SYM
%token    WRITE_SYM                     /* SQL-2003-N */
%token    X509_SYM
%token    XA_SYM
%token    XML_SYM
%token    YEAR_SYM                      /* SQL-2003-R */
%token     ST_COLLECT_SYM
%token  ALLOW_SYM  /* REST SQL keyword */
%token  APPS_SYM  /* REST SQL keyword */
%token  APP_SYM  /* REST SQL keyword */
%token  AUTHENTICATION_SYM  /* REST SQL keyword */
%token  AUTH_SYM  /* REST SQL keyword */
%token  AUTODETECT_SYM  /* REST SQL keyword */
%token  AVAILABLE_SYM  /* REST SQL keyword */
%token  CLASS_SYM  /* REST SQL keyword */
%token  CLONE_SYM  /* REST SQL keyword */
%token  CONFIGURE_SYM  /* REST SQL keyword */
%token  CONTENT_SYM  /* REST SQL keyword */
%token  DAEMONS_SYM  /* REST SQL keyword */
%token  DAEMON_SYM  /* REST SQL keyword */
%token  DEVELOPMENT_SYM  /* REST SQL keyword */
%token  DISABLED_SYM  /* REST SQL keyword */
%token  ENABLED_SYM  /* REST SQL keyword */
%token  ENDPOINTS_SYM  /* REST SQL keyword */
%token  EXTENDS_SYM  /* REST SQL keyword */
%token  FEED_SYM  /* REST SQL keyword */
%token  FILES_SYM  /* REST SQL keyword */
%token  FILTER_SYM  /* REST SQL keyword */
%token  FUNCTIONS_SYM  /* REST SQL keyword */
%token  HIERARCHY_SYM  /* REST SQL keyword */
%token  HTTPS_SYM  /* REST SQL keyword */
%token  HTTP_SYM  /* REST SQL keyword */
%token  INCLUDING_SYM  /* REST SQL keyword */
%token  ITEMS_SYM  /* REST SQL keyword */
%token  ITEM_SYM  /* REST SQL keyword */
%token  MANAGEMENT_SYM  /* REST SQL keyword */
%token  MAPPING_SYM  /* REST SQL keyword */
%token  MARIADB_SYM  /* REST SQL keyword */
%token  MEDIA_SYM  /* REST SQL keyword */
%token  METADATA_SYM  /* REST SQL keyword */
%token  MRS_SYM  /* REST SQL keyword */
%token  NEW_SYM  /* REST SQL keyword */
%token  PARAMETERS_SYM  /* REST SQL keyword */
%token  PER_SYM  /* REST SQL keyword */
%token  PRIVATE_SYM  /* REST SQL keyword */
%token  PROCEDURES_SYM  /* REST SQL keyword */
%token  PROTOCOL_SYM  /* REST SQL keyword */
%token  PUBLISHED_SYM  /* REST SQL keyword */
%token  REDIRECTION_SYM  /* REST SQL keyword */
%token  REGISTER_SYM  /* REST SQL keyword */
%token  REQUEST_SYM  /* REST SQL keyword */
%token  REQUIRED_SYM  /* REST SQL keyword */
%token  REST_SYM  /* REST SQL keyword */
%token  RESULT_SYM  /* REST SQL keyword */
%token  ROLES_SYM  /* REST SQL keyword */
%token  SCRIPTS_SYM  /* REST SQL keyword */
%token  SECRET_SYM  /* REST SQL keyword */
%token  SERVICES_SYM  /* REST SQL keyword */
%token  SERVICE_SYM  /* REST SQL keyword */
%token  SETS_SYM  /* REST SQL keyword */
%token  STATIC_SYM  /* REST SQL keyword */
%token  TYPESCRIPT_SYM  /* REST SQL keyword */
%token  UNPUBLISHED_SYM  /* REST SQL keyword */
%token  URL_SYM  /* REST SQL keyword */
%token  USERS_SYM  /* REST SQL keyword */
%token  VENDORS_SYM  /* REST SQL keyword */
%token  VENDOR_SYM  /* REST SQL keyword */
%token  VIEWS_SYM  /* REST SQL keyword */
/* A dummy token to force the priority of table_ref production in a join. */
%left   CONDITIONLESS_JOIN
%left   JOIN_SYM INNER_SYM STRAIGHT_JOIN CROSS LEFT RIGHT ON_SYM USING
 
%left   SET_VAR
%left   OR_SYM OR2_SYM
%left   XOR
%left   AND_SYM AND_AND_SYM

%left   PREC_BELOW_NOT

/* The precedence of boolean NOT is in fact here. See the comment below. */

%left   '=' EQUAL_SYM GE '>' LE '<' NE
%nonassoc IS
%right BETWEEN_SYM
%left   LIKE SOUNDS_SYM REGEXP IN_SYM
%left   '|'
%left   '&'
%left   SHIFT_LEFT SHIFT_RIGHT
%left   '-' '+' ORACLE_CONCAT_SYM
%left   '*' '/' '%' DIV_SYM MOD_SYM
%left   JSON_SEPARATOR_SYM JSON_UNQUOTED_SEPARATOR_SYM
%left   '^'
%left   MYSQL_CONCAT_SYM
/*
  Boolean negation has a special branch in "expr" starting with NOT_SYM.
  The precedence of logical negation is determined by the grammar itself
  (without using Bison terminal symbol precedence) in this order
  - Boolean factor (i.e. logical AND)
  - Boolean NOT
  - Boolean test (such as '=', IS NULL, IS TRUE)

  But we also need a precedence for NOT_SYM in other contexts,
  to shift (without reduce) in these cases:
     predicate <here> NOT IN ...
     predicate <here> NOT BETWEEN ...
     predicate <here> NOT LIKE ...
     predicate <here> NOT REGEXP ...
  If the precedence of NOT_SYM was low, it would reduce immediately
  after scanning "predicate" and then produce a syntax error on "NOT".
*/
%nonassoc NOT_SYM
%nonassoc NEG '~' NOT2_SYM BINARY
%nonassoc COLLATE_SYM
%nonassoc SUBQUERY_AS_EXPR

/*
  Tokens that can change their meaning from identifier to something else
  in certain context.

  - TRANSACTION: identifier, history unit:
      SELECT transaction FROM t1;
      SELECT * FROM t1 FOR SYSTEM_TIME AS OF TRANSACTION @var;

  - TIMESTAMP: identifier, literal, history unit:
      SELECT timestamp FROM t1;
      SELECT TIMESTAMP '2001-01-01 10:20:30';
      SELECT * FROM t1 FOR SYSTEM_TIME AS OF TIMESTAMP CONCAT(@date,' ',@time);

  - PERIOD: identifier, period for system time:
      SELECT period FROM t1;
      ALTER TABLE DROP PERIOD FOR SYSTEM TIME;

  - SYSTEM: identifier, system versioning:
      SELECT system FROM t1;
      ALTER TABLE DROP SYSTEM VERSIONING;

  - USER: identifier, user:
      SELECT user FROM t1;
      KILL USER foo;

   Note, we need here only tokens that cause shift/reduce conflicts
   with keyword identifiers. For example:
      opt_clause1: %empty | KEYWORD ... ;
      clause2: opt_clause1 ident;
   KEYWORD can appear both in opt_clause1 and in "ident" through the "keyword"
   rule. So the parser reports a conflict on how to interpret KEYWORD:
     - as a start of non-empty branch in opt_clause1, or
     - as an identifier which follows the empty branch in opt_clause1.

   Example#1:
     alter_list_item:
       DROP opt_column opt_if_exists_table_element field_ident
     | DROP SYSTEM VERSIONING_SYM
   SYSTEM can be a keyword in field_ident, or can be a start of
   SYSTEM VERSIONING.

   Example#2:
     system_time_expr: AS OF_SYM history_point
     history_point: opt_history_unit bit_expr
     opt_history_unit: | TRANSACTION_SYM
   TRANSACTION can be a non-empty history unit, or can be an identifier
   in bit_expr.

   In the grammar below we use %prec to explicitly tell Bison to go
   through the empty branch in the optional rule only when the lookahead
   token does not belong to a small set of selected tokens.

   Tokens NEXT_SYM and PREVIOUS_SYM also change their meaning from
   identifiers to sequence operations when followed by VALUE_SYM:
      SELECT NEXT VALUE FOR s1, PREVIOUS VALUE FOR s1;
   but we don't need to list them here as they do not seem to cause
   conflicts (according to bison -v), as both meanings
   (as identifier, and as a sequence operation) are parts of the same target
   column_default_non_parenthesized_expr, and there are no any optional
   clauses between the start of column_default_non_parenthesized_expr
   and until NEXT_SYM / PREVIOUS_SYM.
*/
%left   PREC_BELOW_IDENTIFIER_OPT_SPECIAL_CASE
%left   TRANSACTION_SYM TIMESTAMP PERIOD_SYM SYSTEM USER COMMENT_SYM

%left   PREC_BELOW_SP_OBJECT_TYPE
%left   PACKAGE_MARIADB_SYM FUNCTION_SYM


/*
  Tokens that can appear in a token contraction on the second place
  and change the meaning of the previous token.

  - TEXT_STRING: changes the meaning of TIMESTAMP/TIME/DATE
    from identifier to literal:
      SELECT timestamp FROM t1;
      SELECT TIMESTAMP'2001-01-01 00:00:00' FROM t1;

  - Parenthesis: changes the meaning of TIMESTAMP/TIME/DATE
    from identifiers to CAST-alike functions:
      SELECT timestamp FROM t1;
      SELECT timestamp(1) FROM t1;

  - VALUE: changes NEXT and PREVIOUS from identifier to sequence operation:
      SELECT next, previous FROM t1;
      SELECT NEXT VALUE FOR s1, PREVIOUS VALUE FOR s1;

  - VERSIONING: changes SYSTEM from identifier to SYSTEM VERSIONING
      SELECT system FROM t1;
      ALTER TABLE t1 ADD SYSTEM VERSIONING;
  - OF: changes TYPE from a pluggable data type name to TYPE OF anchor
      CREATE FUNCTION f1() RETURNS TYPE RETURN 1; -- pluggable data type 'TYPE'
      CREATE FUNCTION f1() RETURNS TYPE OF t1.col; -- the data type of t1.col1

*/
%left   PREC_BELOW_CONTRACTION_TOKEN2
%left   TEXT_STRING '(' ')' VALUE_SYM VERSIONING_SYM BODY_MARIADB_SYM OF_SYM
%left EMPTY_FROM_CLAUSE
%right INTO









































































































































































































































































































































































































































































































































































































































































%token ',' '!' '{' '}'






















































































%%


/*
  Indentation of grammar rules:

rule: <-- starts at col 1
          rule1a rule1b rule1c <-- starts at col 11
          { <-- starts at col 11
            code <-- starts at col 13, indentation is 2 spaces
          }
        | rule2a rule2b
          {
            code
          }
        ; <-- on a line by itself, starts at col 9

  Also, please do not use any <TAB>, but spaces.
  Having a uniform indentation in this file helps
  code reviews, patches, merges, and make maintenance easier.
  Tip: grep [[:cntrl:]] sql_yacc.yy
  Thanks.
*/

query:
          END_OF_INPUT
          { MDBP_R(0); }







        | directly_executable_statement
          { MDBP_R(1); }





















          ';'
          opt_end_of_input { MDBP_R(2); }
        | directly_executable_statement END_OF_INPUT
          { MDBP_R(3); }



        ;

opt_end_of_input: { MDBP_R(4); }
          /* empty */
        | END_OF_INPUT { MDBP_R(5); }
        ;

directly_executable_statement:
          statement { MDBP_R(6); }
        | begin_stmt_mariadb { MDBP_R(7); }
        | compound_statement { MDBP_R(8); }
        ;

/* Verb clauses, except begin and compound_statement */
verb_clause:
          alter { MDBP_R(9); }
        | mrs_statement { MDBP_R(5946); }
        | analyze { MDBP_R(10); }
        | analyze_stmt_command { MDBP_R(11); }
        | backup { MDBP_R(12); }
        | binlog_base64_event { MDBP_R(13); }
        | call { MDBP_R(14); }
        | change { MDBP_R(15); }
        | check { MDBP_R(16); }
        | checksum { MDBP_R(17); }
        | commit { MDBP_R(18); }
        | create { MDBP_R(19); }
        | deallocate { MDBP_R(20); }
        | delete { MDBP_R(21); }
        | deny { MDBP_R(22); }
        | describe { MDBP_R(23); }
        | do { MDBP_R(24); }
        | drop { MDBP_R(25); }
        | execute { MDBP_R(26); }
        | explain_for_connection { MDBP_R(27); }
        | flush { MDBP_R(28); }
        | get_diagnostics { MDBP_R(29); }
        | grant { MDBP_R(30); }
        | handler { MDBP_R(31); }
        | help { MDBP_R(32); }
        | insert { MDBP_R(33); }
        | install { MDBP_R(34); }
	| keep_gcc_happy { MDBP_R(35); }
        | keycache { MDBP_R(36); }
        | kill { MDBP_R(37); }
        | load { MDBP_R(38); }
        | lock { MDBP_R(39); }
        | optimize { MDBP_R(40); }
        | parse_vcol_expr { MDBP_R(41); }
        | partition_entry { MDBP_R(42); }
        | preload { MDBP_R(43); }
        | prepare { MDBP_R(44); }
        | purge { MDBP_R(45); }
        | raise_stmt_oracle { MDBP_R(46); }
        | release { MDBP_R(47); }
        | rename { MDBP_R(48); }
        | repair { MDBP_R(49); }
        | replace { MDBP_R(50); }
        | reset { MDBP_R(51); }
        | resignal_stmt { MDBP_R(52); }
        | revoke { MDBP_R(53); }
        | rollback { MDBP_R(54); }
        | savepoint { MDBP_R(55); }
        | select { MDBP_R(56); }
        | select_into { MDBP_R(57); }
        | set { MDBP_R(58); }
        | signal_stmt { MDBP_R(59); }
        | show { MDBP_R(60); }
        | shutdown { MDBP_R(61); }
        | slave { MDBP_R(62); }
        | start { MDBP_R(63); }
        | truncate { MDBP_R(64); }
        | uninstall { MDBP_R(65); }
        | unlock { MDBP_R(66); }
        | update { MDBP_R(67); }
        | use { MDBP_R(68); }
        | xa { MDBP_R(69); }
        ;

deallocate:
          deallocate_or_drop PREPARE_SYM sql_statement_name
          { MDBP_R(70); }



        ;

deallocate_or_drop:
          DEALLOCATE_SYM { MDBP_R(71); }
        | DROP { MDBP_R(72); }
        ;

prepare:
          PREPARE_SYM sql_statement_name FROM
          { MDBP_R(73); }




          expr
          { MDBP_R(74); }




        ;

execute:
          EXECUTE_SYM sql_statement_name execute_using
          { MDBP_R(75); }



        | EXECUTE_SYM IMMEDIATE_SYM
          { MDBP_R(76); }




          expr
          { MDBP_R(77); }
          execute_using
          { MDBP_R(78); }



        ;

execute_using:
          /* nothing */    { MDBP_R(79); }
        | USING
          { MDBP_R(80); }
          execute_params
          { MDBP_R(81); }



        ;

execute_params:
          expr_or_ignore_or_default
          { MDBP_R(82); }



        | execute_params ',' expr_or_ignore_or_default
          { MDBP_R(83); }



        ;


/* help */

help:
          HELP_SYM
          { MDBP_R(84); }



          ident_or_text
          { MDBP_R(85); }




        ;

/* change master */

change:
          CHANGE MASTER_SYM optional_connection_name TO_SYM
          { MDBP_R(86); }


          master_defs
          optional_for_channel
          { MDBP_R(87); }
        ;

master_defs:
          master_def { MDBP_R(88); }
        | master_defs ',' master_def { MDBP_R(89); }
        ;

master_def:
          MASTER_HOST_SYM '=' TEXT_STRING_sys
          { MDBP_R(90); }


        | MASTER_USER_SYM '=' TEXT_STRING_sys
          { MDBP_R(91); }


        | MASTER_PASSWORD_SYM '=' TEXT_STRING_sys
          { MDBP_R(92); }


        | MASTER_PORT_SYM '=' ulong_num
          { MDBP_R(93); }


        | MASTER_CONNECT_RETRY_SYM '=' uint32_or_default
          { MDBP_R(94); }




        | MASTER_RETRY_COUNT_SYM '=' uint64_or_default
          { MDBP_R(95); }




        | MASTER_DELAY_SYM '=' ulong_num
          { MDBP_R(96); }








        | MASTER_SSL_SYM '=' bool_or_default
          { MDBP_R(97); }



        | MASTER_SSL_CA_SYM '=' path_or_default
          { MDBP_R(98); }



        | MASTER_SSL_CAPATH_SYM '=' path_or_default
          { MDBP_R(99); }



        | MASTER_SSL_CERT_SYM '=' path_or_default
          { MDBP_R(100); }



        | MASTER_SSL_CIPHER_SYM '=' path_or_default
          { MDBP_R(101); }



        | MASTER_SSL_KEY_SYM '=' path_or_default
          { MDBP_R(102); }



        | MASTER_SSL_VERIFY_SERVER_CERT_SYM '=' bool_or_default
          { MDBP_R(103); }



        | MASTER_SSL_CRL_SYM '=' path_or_default
          { MDBP_R(104); }



        | MASTER_SSL_CRLPATH_SYM '=' path_or_default
          { MDBP_R(105); }




        | MASTER_HEARTBEAT_PERIOD_SYM '=' opt_plus num_or_default
          { MDBP_R(106); }



























        | IGNORE_SERVER_IDS_SYM '=' '(' ignore_server_id_list ')'
          { MDBP_R(107); }


        | DO_DOMAIN_IDS_SYM '=' '(' do_domain_id_list ')'
          { MDBP_R(108); }


        | IGNORE_DOMAIN_IDS_SYM '=' '(' ignore_domain_id_list ')'
          { MDBP_R(109); }


        |
        master_file_def { MDBP_R(110); }
        ;

uint32_or_default:
          ulong_num     { MDBP_R(111); }
        | DEFAULT       { MDBP_R(112); }
        ;
uint64_or_default:
          ulonglong_num { MDBP_R(113); }
        | DEFAULT       { MDBP_R(114); }
        ;
path_or_default:
          TEXT_STRING_sys { MDBP_R(115); }
        | DEFAULT         { MDBP_R(116); }
        ;
bool_or_default:
          bool { MDBP_R(117); }
        | DEFAULT { MDBP_R(118); };
master_use_gtid_enum:
          NO_SYM          { MDBP_R(119); }
        | CURRENT_POS_SYM { MDBP_R(120); }
        | SLAVE_POS_SYM   { MDBP_R(121); }
        | DEFAULT         { MDBP_R(122); }
        ;
num_or_default:
          NUM_literal { MDBP_R(123); }
        | DEFAULT { MDBP_R(124); }
        ;

ignore_server_id_list: { MDBP_R(125); }
          /* Empty */
          | ignore_server_id { MDBP_R(126); }
          | ignore_server_id_list ',' ignore_server_id { MDBP_R(127); }
        ;

ignore_server_id:
          ulong_num
          { MDBP_R(128); }


          ;

do_domain_id_list: { MDBP_R(129); }
          /* Empty */
          | do_domain_id { MDBP_R(130); }
          | do_domain_id_list ',' do_domain_id { MDBP_R(131); }
        ;

do_domain_id:
          ulong_num
          { MDBP_R(132); }


          ;

ignore_domain_id_list: { MDBP_R(133); }
          /* Empty */
          | ignore_domain_id { MDBP_R(134); }
          | ignore_domain_id_list ',' ignore_domain_id { MDBP_R(135); }
        ;

ignore_domain_id:
          ulong_num
          { MDBP_R(136); }


          ;

master_file_def:
          MASTER_LOG_FILE_SYM '=' TEXT_STRING_sys
          { MDBP_R(137); }


        | MASTER_LOG_POS_SYM '=' ulonglong_num
          { MDBP_R(138); }













        | RELAY_LOG_FILE_SYM '=' TEXT_STRING_sys
          { MDBP_R(139); }


        | RELAY_LOG_POS_SYM '=' ulong_num
          { MDBP_R(140); }




        | MASTER_USE_GTID_SYM '=' master_use_gtid_enum
          { MDBP_R(141); }





        | MASTER_DEMOTE_TO_SLAVE_SYM '=' bool
          { MDBP_R(142); }


        ;

optional_connection_name:
          /* empty */
          { MDBP_R(143); }



        | connection_name { MDBP_R(144); }
        ;

connection_name:
        TEXT_STRING_sys
        { MDBP_R(145); }






         ;

optional_for_channel:
        /* empty */
          { MDBP_R(146); }


        | for_channel { MDBP_R(147); }

        ;

for_channel:
        FOR_SYM CHANNEL_SYM TEXT_STRING_sys
        { MDBP_R(148); }














          ;

/* create a table */

create:
          create_or_replace opt_temporary TABLE_SYM opt_if_not_exists
          { MDBP_R(149);






            lex->current_select->parsing_place= BEFORE_OPT_LIST;


}
          table_ident
          { MDBP_R(150); }















          create_body
          { MDBP_R(151); }




       | create_or_replace opt_temporary SEQUENCE_SYM opt_if_not_exists table_ident
         { MDBP_R(152); }




























         opt_sequence opt_create_table_options
         { MDBP_R(153); }

























        | create_or_replace INDEX_SYM opt_if_not_exists
          { MDBP_R(154); }



          ident
          opt_key_algorithm_clause
          ON table_ident
          { MDBP_R(155); }





          '(' key_list ')' opt_lock_wait_timeout normal_key_options
          opt_index_lock_algorithm
          { MDBP_R(156); }


        | create_or_replace UNIQUE_SYM INDEX_SYM opt_if_not_exists
          { MDBP_R(157); }



          ident
          opt_key_algorithm_clause
          ON table_ident
          { MDBP_R(158); }





          '(' key_list opt_without_overlaps ')'
          opt_lock_wait_timeout normal_key_options
          opt_index_lock_algorithm
          { MDBP_R(159); }


        | create_or_replace fulltext INDEX_SYM
          { MDBP_R(160); }



          opt_if_not_exists ident
          ON table_ident
          { MDBP_R(161); }





          '(' key_list ')' opt_lock_wait_timeout fulltext_key_options
          opt_index_lock_algorithm
          { MDBP_R(162); }


        | create_or_replace spatial_or_vector INDEX_SYM
          { MDBP_R(163); }



          opt_if_not_exists ident
          ON table_ident
          { MDBP_R(164); }





          '(' key_part_simple ')' opt_lock_wait_timeout spatial_key_options
          opt_index_lock_algorithm
          { MDBP_R(165); }



        | create_or_replace DATABASE opt_if_not_exists ident
          { MDBP_R(166); }


          opt_create_database_options
          { MDBP_R(167); }






        | create_or_replace definer_opt opt_view_suid VIEW_SYM
          opt_if_not_exists table_ident
          { MDBP_R(168); }







          view_list_opt AS view_select
          { MDBP_R(169); }


        | create_or_replace view_algorithm definer_opt opt_view_suid VIEW_SYM
          opt_if_not_exists table_ident
          { MDBP_R(170); }






          view_list_opt AS view_select
          { MDBP_R(171); }


        | create_or_replace definer_opt TRIGGER_SYM
          { MDBP_R(172); }




          trigger_tail
          { MDBP_R(173); }


        | create_or_replace definer_opt EVENT_SYM
          { MDBP_R(174); }




          event_tail
          { MDBP_R(175); }


        | create_or_replace USER_SYM opt_if_not_exists clear_privileges
          grant_list opt_require_clause opt_resource_options opt_account_locking_and_opt_password_expiration
          { MDBP_R(176); }




        | create_or_replace ROLE_SYM opt_if_not_exists
          clear_privileges role_list opt_with_admin
          { MDBP_R(177); }




        | create_or_replace { MDBP_R(178); }
          server_def
          { MDBP_R(179); }
        | create_routine { MDBP_R(180); }
        ;

opt_sequence:
         /* empty */ { MDBP_R(181); }
        | sequence_defs { MDBP_R(182); }
        ;

sequence_defs:
          sequence_def { MDBP_R(183); }
        | sequence_defs sequence_def { MDBP_R(184); }
        ;

sequence_def:
          AS int_type field_options
          { MDBP_R(185); }










        | MINVALUE_SYM opt_equal sequence_truncated_value_hybrid_num
          { MDBP_R(186); }









        | NO_SYM MINVALUE_SYM
          { MDBP_R(187); }





        | NOMINVALUE_SYM
          { MDBP_R(188); }





        | MAXVALUE_SYM opt_equal sequence_truncated_value_hybrid_num
          { MDBP_R(189); }







        | NO_SYM MAXVALUE_SYM
          { MDBP_R(190); }





        | NOMAXVALUE_SYM
          { MDBP_R(191); }





        | START_SYM opt_with sequence_value_hybrid_num
          { MDBP_R(192); }






        | INCREMENT_SYM opt_by sequence_value_num
          { MDBP_R(193); }






        | CACHE_SYM opt_equal sequence_value_num
          { MDBP_R(194); }






        | NOCACHE_SYM
          { MDBP_R(195); }






        | CYCLE_SYM
          { MDBP_R(196); }






        | NOCYCLE_SYM
          { MDBP_R(197); }






        | RESTART_SYM
          { MDBP_R(198); }










        | RESTART_SYM opt_with sequence_value_hybrid_num
          { MDBP_R(199); }












        ;

/* this rule is used to force look-ahead in the parser */
force_lookahead: { MDBP_R(200); } | FORCE_LOOKAHEAD { MDBP_R(201); } ;

server_def:
          SERVER_SYM opt_if_not_exists ident_or_text
          { MDBP_R(202); }




          FOREIGN DATA_SYM WRAPPER_SYM ident_or_text
          OPTIONS_SYM '(' server_options_list ')'
          { MDBP_R(203); }
        ;

server_options_list:
          server_option { MDBP_R(204); }
        | server_options_list ',' server_option { MDBP_R(205); }
        ;

server_option:
          USER_SYM TEXT_STRING_sys
          { MDBP_R(206); }











        | HOST_SYM TEXT_STRING_sys
          { MDBP_R(207); }











        | DATABASE TEXT_STRING_sys
          { MDBP_R(208); }











        | OWNER_SYM TEXT_STRING_sys
          { MDBP_R(209); }











        | PASSWORD_SYM TEXT_STRING_sys
          { MDBP_R(210); }











        | SOCKET_SYM TEXT_STRING_sys
          { MDBP_R(211); }











        | PORT_SYM ulong_num
          { MDBP_R(212); }

























        /* port can be a quoted number */
        | PORT_SYM TEXT_STRING_sys
          { MDBP_R(213); }



















        | IDENT_sys TEXT_STRING_sys
          { MDBP_R(214); }









        ;

event_tail:
          remember_name opt_if_not_exists sp_name
          { MDBP_R(215); }















          ON SCHEDULE_SYM ev_schedule_time
          opt_ev_on_completion
          opt_ev_status
          opt_ev_comment
          DO_SYM ev_sql_stmt
          { MDBP_R(216); }






        ;

ev_schedule_time:
          EVERY_SYM expr interval
          { MDBP_R(217); }



          ev_starts
          ev_ends { MDBP_R(218); }
        | AT_SYM expr
          { MDBP_R(219); }


        ;

opt_ev_status:
          /* empty */ { MDBP_R(220); }
        | ENABLE_SYM
          { MDBP_R(221); }




        | DISABLE_SYM ON SLAVE
          { MDBP_R(222); }




        | DISABLE_SYM
          { MDBP_R(223); }




        ;

ev_starts:
          /* empty */
          { MDBP_R(224); }





        | STARTS_SYM expr
          { MDBP_R(225); }


        ;

ev_ends: { MDBP_R(226); }
          /* empty */
        | ENDS_SYM expr
          { MDBP_R(227); }


        ;

opt_ev_on_completion:
          /* empty */ { MDBP_R(228); }
        | ev_on_completion { MDBP_R(229); }
        ;

ev_on_completion:
          ON COMPLETION_SYM opt_not PRESERVE_SYM
          { MDBP_R(230); }





        ;

opt_ev_comment:
          /* empty */ { MDBP_R(231); }
        | COMMENT_SYM TEXT_STRING_sys
          { MDBP_R(232); }



        ;

ev_sql_stmt:
          { MDBP_R(233); }































          sp_proc_stmt force_lookahead
          { MDBP_R(234); }




        ;

clear_privileges:
          /* Nothing */
          { MDBP_R(235); }





        ;

opt_aggregate:
          /* Empty */   { MDBP_R(236); }
        | AGGREGATE_SYM { MDBP_R(237); }
        ;


sp_handler:
          FUNCTION_SYM                       { MDBP_R(238); }
        | PROCEDURE_SYM                      { MDBP_R(239); }
        | sp_handler_package_spec { MDBP_R(240); }
        | sp_handler_package_body { MDBP_R(241); }
        ;

sp_handler_package_spec:
         PACKAGE_ORACLE_SYM                   { MDBP_R(242); }
       | PACKAGE_MARIADB_SYM                  { MDBP_R(243); }
       ;

sp_handler_package_body:
         PACKAGE_ORACLE_SYM BODY_ORACLE_SYM   { MDBP_R(244); }
       | PACKAGE_MARIADB_SYM BODY_MARIADB_SYM { MDBP_R(245); }
       ;

drop_routine:
          DROP sp_handler opt_if_exists ident '.' ident
          { MDBP_R(246); }



        | DROP sp_handler opt_if_exists ident
          { MDBP_R(247); }



        ;


sp_name:
          ident '.' ident
          { MDBP_R(248); }



        | ident
          { MDBP_R(249); }



        ;

sp_a_chistics:
          /* Empty */ { MDBP_R(250); }
        | sp_a_chistics sp_chistic { MDBP_R(251); }
        ;

sp_c_chistics:
          /* Empty */ { MDBP_R(252); }
        | sp_c_chistics sp_c_chistic { MDBP_R(253); }
        ;

/* Characteristics for both create and alter */
sp_chistic:
          COMMENT_SYM TEXT_STRING_sys
          { MDBP_R(254); }
        | LANGUAGE_SYM SQL_SYM
          { MDBP_R(255); }
        | NO_SYM SQL_SYM
          { MDBP_R(256); }
        | CONTAINS_SYM SQL_SYM
          { MDBP_R(257); }
        | READS_SYM SQL_SYM DATA_SYM
          { MDBP_R(258); }
        | MODIFIES_SYM SQL_SYM DATA_SYM
          { MDBP_R(259); }
        | sp_suid
          { MDBP_R(260); }
        ;

/* Create characteristics */
sp_c_chistic:
          sp_chistic            { MDBP_R(261); }
        | opt_not DETERMINISTIC_SYM { MDBP_R(262); }
        ;

sp_suid:
          SQL_SYM SECURITY_SYM DEFINER_SYM { MDBP_R(263); }
        | SQL_SYM SECURITY_SYM INVOKER_SYM { MDBP_R(264); }
        ;

call:
          CALL_SYM ident
          { MDBP_R(265); }



          opt_sp_cparam_list
          { MDBP_R(266); }



        | CALL_SYM ident '.' ident
          { MDBP_R(267); }



          opt_sp_cparam_list
          { MDBP_R(268); }



        | CALL_SYM ident '.' ident '.' ident
          { MDBP_R(269); }



          opt_sp_cparam_list
          { MDBP_R(270); }



        ;

/* CALL parameters */
opt_sp_cparam_list:
          /* Empty */ { MDBP_R(271); }
        | '('
        { MDBP_R(272); }


 opt_sp_cparams ')'
        { MDBP_R(273); }


        ;














opt_sp_cparams:
          /* Empty */ { MDBP_R(274); }
        | sp_cparams  { MDBP_R(275); }
        ;

sp_cparams:
          sp_cparams ',' expr
          { MDBP_R(276); }


        | expr
          { MDBP_R(277); }


        ;

/* Stored FUNCTION parameter declaration list */
sp_fdparam_list:
          /* Empty */
          { MDBP_R(278); }



        |
          { MDBP_R(279); }


          sp_fdparams
          { MDBP_R(280); }


        ;

sp_fdparams:
          sp_fdparams ',' sp_param_default { MDBP_R(281); }
        | sp_param_default { MDBP_R(282); }
        ;

sp_fdparams_no_default:
          sp_fdparams_no_default ',' sp_param { MDBP_R(283); }
        | sp_param { MDBP_R(284); }
        ;

sp_param_name:
          ident
          { MDBP_R(285); }



        ;

sp_param_name_and_mode_init_vars:
          sp_param_name_and_mode
          { MDBP_R(286); }



        ;

/* Stored PROCEDURE parameter declaration list */
sp_pdparam_list: { MDBP_R(287); }
          /* Empty */
        | sp_pdparams { MDBP_R(288); }
        ;

sp_pdparams:
          sp_pdparams ',' sp_param_default { MDBP_R(289); }
        | sp_param_default { MDBP_R(290); }
        ;

sp_parameter_type:
          IN_SYM      { MDBP_R(291); }
        | OUT_SYM     { MDBP_R(292); }
        | INOUT_SYM   { MDBP_R(293); }
        ;

sp_parenthesized_pdparam_list:
          '('
          { MDBP_R(294); }


          sp_pdparam_list
          ')'
          { MDBP_R(295); }


        ;

sp_param_default:
          sp_param_init_vars sp_opt_default
          { MDBP_R(296); }



        ;

sp_param:
          sp_param_init_vars
          { MDBP_R(297); }



        ;

sp_parenthesized_fdparam_list:
        '(' sp_fdparam_list ')' { MDBP_R(298); }
        ;

sp_proc_stmts:
          /* Empty */ { MDBP_R(299); }
        | sp_proc_stmts  sp_proc_stmt ';' { MDBP_R(300); }
        ;

sp_proc_stmts1:
          sp_proc_stmt ';' { MDBP_R(301); }
        | sp_proc_stmts1  sp_proc_stmt ';' { MDBP_R(302); }
        ;


optionally_qualified_column_ident:
          sp_decl_ident
          { MDBP_R(303); }




        | sp_decl_ident '.' ident
          { MDBP_R(304); }




        | sp_decl_ident '.' ident '.' ident
          { MDBP_R(305); }




        ;


row_field_definition:
          row_field_name field_type
          { MDBP_R(306); }




        ;

row_field_definition_list:
          row_field_definition
          { MDBP_R(307); }



        | row_field_definition_list ',' row_field_definition
          { MDBP_R(308); }



        ;

row_type_body:
          '(' row_field_definition_list ')' { MDBP_R(309); }
        ;















































sp_decl_idents_init_vars:
          sp_decl_idents
          { MDBP_R(310); }


        ;

sp_decl_variable_list:
          sp_decl_idents_init_vars
          field_type_all_with_typedefs
          { MDBP_R(311); }




          sp_opt_default
          { MDBP_R(312); }







        | sp_decl_idents_init_vars
          ROW_SYM row_type_body
          sp_opt_default
          { MDBP_R(313); }






        | sp_decl_variable_list_anchored { MDBP_R(314); }
        ;

sp_decl_handler:
          sp_handler_type HANDLER_SYM FOR_SYM
          { MDBP_R(315); }



          sp_hcond_list sp_proc_stmt
          { MDBP_R(316); }





        ;

opt_parenthesized_cursor_formal_parameters: { MDBP_R(317); }
          /* Empty */
        | '(' sp_fdparams_no_default ')' { MDBP_R(318); }
        ;


sp_cursor_stmt_lex:
          { MDBP_R(319); }





        ;

select_or_ps_name:
          select { MDBP_R(320); }
        | ident { MDBP_R(321); }
        ;


sql_statement_name:
          ident
          { MDBP_R(322); }


        | LOCAL_SYM ident
          { MDBP_R(323); }


        ;


open_for:
          select_for_open_cursor       // OPEN c FOR SELECT 1;
          { MDBP_R(324); }


        | bit_expr                     // OPEN c FOR 'SELECT 1';
          { MDBP_R(325); }


        | PREPARE_SYM ident            // OPEN c FOR PREPARE stmt;
          { MDBP_R(326); }


        | LOCAL_SYM ident
          { MDBP_R(327); }


        ;

sp_cursor_stmt:
          sp_cursor_stmt_lex
          { MDBP_R(328); }





          remember_name select_or_ps_name remember_end
          { MDBP_R(329); }






        ;

sp_cursor_stmt_for_open:
          sp_cursor_stmt_lex
          { MDBP_R(330); }






          remember_name open_for remember_end
          { MDBP_R(331); }









        ;

sp_handler_type:
          EXIT_MARIADB_SYM      { MDBP_R(332); }
        | CONTINUE_MARIADB_SYM  { MDBP_R(333); }
        | EXIT_ORACLE_SYM      { MDBP_R(334); }
        | CONTINUE_ORACLE_SYM  { MDBP_R(335); }
       /*| UNDO_SYM      { QQ No yet } */
        ;

sp_hcond_list:
          sp_hcond_element
          { MDBP_R(336); }
        | sp_hcond_list ',' sp_hcond_element
          { MDBP_R(337); }
        ;

sp_hcond_element:
          sp_hcond
          { MDBP_R(338); }










        ;

sp_cond:
          ulong_num
          { MDBP_R(339); }






        | sqlstate { MDBP_R(340); }
        ;

sqlstate:
          SQLSTATE_SYM opt_value TEXT_STRING_literal
          { MDBP_R(341); }















        ;

opt_value:
          /* Empty */  { MDBP_R(342); }
        | VALUE_SYM    { MDBP_R(343); }
        ;

sp_hcond:
          sp_cond
          { MDBP_R(344); }


        | ident /* CONDITION name */
          { MDBP_R(345); }




        | SQLWARNING_SYM /* SQLSTATEs 01??? */
          { MDBP_R(346); }




        | not FOUND_SYM /* SQLSTATEs 02??? */
          { MDBP_R(347); }




        | SQLEXCEPTION_SYM /* All other SQLSTATEs */
          { MDBP_R(348); }




        | OTHERS_ORACLE_SYM /* All other SQLSTATEs */
          { MDBP_R(349); }




        ;


raise_stmt_oracle:
          RAISE_ORACLE_SYM opt_set_signal_information
          { MDBP_R(350); }



        | RAISE_ORACLE_SYM signal_value opt_set_signal_information
          { MDBP_R(351); }



        ;

signal_stmt:
          SIGNAL_SYM signal_value opt_set_signal_information
          { MDBP_R(352); }



        ;

signal_value:
          ident
          { MDBP_R(353); }



        | sqlstate
          { MDBP_R(354); }
        ;

opt_signal_value:
          /* empty */
          { MDBP_R(355); }
        | signal_value
          { MDBP_R(356); }
        ;

opt_set_signal_information:
          /* empty */
          { MDBP_R(357); }


        | SET signal_information_item_list { MDBP_R(358); }
        ;

signal_information_item_list:
          signal_condition_information_item_name '=' signal_allowed_expr
          { MDBP_R(359); }






        | signal_information_item_list ','
          signal_condition_information_item_name '=' signal_allowed_expr
          { MDBP_R(360); }








        ;

/*
  Only a limited subset of <expr> are allowed in SIGNAL/RESIGNAL.
*/
signal_allowed_expr:
          literal
          { MDBP_R(361); }
        | variable
          { MDBP_R(362); }
















        | simple_ident
          { MDBP_R(363); }
        ;

/* conditions that can be set in signal / resignal */
signal_condition_information_item_name:
          CLASS_ORIGIN_SYM
          { MDBP_R(364); }
        | SUBCLASS_ORIGIN_SYM
          { MDBP_R(365); }
        | CONSTRAINT_CATALOG_SYM
          { MDBP_R(366); }
        | CONSTRAINT_SCHEMA_SYM
          { MDBP_R(367); }
        | CONSTRAINT_NAME_SYM
          { MDBP_R(368); }
        | CATALOG_NAME_SYM
          { MDBP_R(369); }
        | SCHEMA_NAME_SYM
          { MDBP_R(370); }
        | TABLE_NAME_SYM
          { MDBP_R(371); }
        | COLUMN_NAME_SYM
          { MDBP_R(372); }
        | CURSOR_NAME_SYM
          { MDBP_R(373); }
        | MESSAGE_TEXT_SYM
          { MDBP_R(374); }
        | MYSQL_ERRNO_SYM
          { MDBP_R(375); }
        | ROW_NUMBER_SYM
          { MDBP_R(376); }
        ;

resignal_stmt:
          RESIGNAL_SYM opt_signal_value opt_set_signal_information
          { MDBP_R(377); }



        ;

get_diagnostics:
          GET_SYM which_area DIAGNOSTICS_SYM
          { MDBP_R(378); }



          diagnostics_information
          { MDBP_R(379); }










        ;

which_area:
        /* If <which area> is not specified, then CURRENT is implicit. */
          { MDBP_R(380); }
        | CURRENT_SYM
          { MDBP_R(381); }
        ;

diagnostics_information:
          statement_information
          { MDBP_R(382); }




        | CONDITION_SYM condition_number condition_information
          { MDBP_R(383); }




        ;

statement_information:
          statement_information_item
          { MDBP_R(384); }





        | statement_information ',' statement_information_item
          { MDBP_R(385); }




        ;

statement_information_item:
          simple_target_specification '=' statement_information_item_name
          { MDBP_R(386); }




        ;

simple_target_specification:
          ident_cli
          { MDBP_R(387); }



        | '@' ident_or_text
          { MDBP_R(388); }









        ;

statement_information_item_name:
          NUMBER_MARIADB_SYM
          { MDBP_R(389); }
        | NUMBER_ORACLE_SYM
          { MDBP_R(390); }
        | ROW_COUNT_SYM
          { MDBP_R(391); }
        ;

/*
   Only a limited subset of <expr> are allowed in GET DIAGNOSTICS
   <condition number>, same subset as for SIGNAL/RESIGNAL.
*/
condition_number:
          signal_allowed_expr
          { MDBP_R(392); }
        ;

condition_information:
          condition_information_item
          { MDBP_R(393); }





        | condition_information ',' condition_information_item
          { MDBP_R(394); }




        ;

condition_information_item:
          simple_target_specification '=' condition_information_item_name
          { MDBP_R(395); }




        ;

condition_information_item_name:
          CLASS_ORIGIN_SYM
          { MDBP_R(396); }
        | SUBCLASS_ORIGIN_SYM
          { MDBP_R(397); }
        | CONSTRAINT_CATALOG_SYM
          { MDBP_R(398); }
        | CONSTRAINT_SCHEMA_SYM
          { MDBP_R(399); }
        | CONSTRAINT_NAME_SYM
          { MDBP_R(400); }
        | CATALOG_NAME_SYM
          { MDBP_R(401); }
        | SCHEMA_NAME_SYM
          { MDBP_R(402); }
        | TABLE_NAME_SYM
          { MDBP_R(403); }
        | COLUMN_NAME_SYM
          { MDBP_R(404); }
        | CURSOR_NAME_SYM
          { MDBP_R(405); }
        | MESSAGE_TEXT_SYM
          { MDBP_R(406); }
        | MYSQL_ERRNO_SYM
          { MDBP_R(407); }
        | RETURNED_SQLSTATE_SYM
          { MDBP_R(408); }
        | ROW_NUMBER_SYM
          { MDBP_R(409); }
        ;

sp_decl_ident:
          IDENT_sys { MDBP_R(410); }
        | keyword_sp_decl
          { MDBP_R(411); }



        ;

sp_decl_idents:
          sp_decl_ident
          { MDBP_R(412); }










        | sp_decl_idents ',' ident
          { MDBP_R(413); }










        ;

sp_proc_stmt_if:
          IF_SYM
          { MDBP_R(414); }




          sp_if END IF_SYM
          { MDBP_R(415); }
        ;

sp_proc_stmt_statement:
          { MDBP_R(416); }











          sp_statement
          { MDBP_R(417); }




        ;


RETURN_ALLMODES_SYM:
          RETURN_MARIADB_SYM { MDBP_R(418); }
        | RETURN_ORACLE_SYM { MDBP_R(419); }
        ;

sp_proc_stmt_return:
          RETURN_ALLMODES_SYM expr_lex
          { MDBP_R(420); }





          { MDBP_R(421); }
        | RETURN_ORACLE_SYM
          { MDBP_R(422); }






        ;

sp_proc_stmt_exit_oracle:
          EXIT_ORACLE_SYM
          { MDBP_R(423); }



        | EXIT_ORACLE_SYM label_ident
          { MDBP_R(424); }




        | EXIT_ORACLE_SYM WHEN_SYM expr_lex
          { MDBP_R(425); }




          { MDBP_R(426); }
        | EXIT_ORACLE_SYM label_ident WHEN_SYM expr_lex
          { MDBP_R(427); }




          { MDBP_R(428); }
        ;

sp_proc_stmt_continue_oracle:
          CONTINUE_ORACLE_SYM
          { MDBP_R(429); }



        | CONTINUE_ORACLE_SYM label_ident
          { MDBP_R(430); }



        | CONTINUE_ORACLE_SYM WHEN_SYM expr_lex
          { MDBP_R(431); }



          { MDBP_R(432); }
        | CONTINUE_ORACLE_SYM label_ident WHEN_SYM expr_lex
          { MDBP_R(433); }



          { MDBP_R(434); }
        ;


sp_proc_stmt_leave:
          LEAVE_SYM label_ident
          { MDBP_R(435); }



        ;

sp_proc_stmt_iterate:
          ITERATE_SYM label_ident
          { MDBP_R(436); }



        ;

sp_proc_stmt_goto_oracle:
          GOTO_ORACLE_SYM label_ident
          { MDBP_R(437); }



        ;


expr_lex:
          { $$ = 0; MDBP_R(438); }








          remember_start_opt expr remember_end
          { MDBP_R(439); }


















        ;


assignment_source_lex:
          { MDBP_R(440); }





        ;

assignment_source_expr:
          assignment_source_lex
          { MDBP_R(441); }





          remember_cpp_ptr expr remember_end
          { MDBP_R(442); }


















        ;

for_loop_bound_expr:
          assignment_source_lex
          { MDBP_R(443);



            Lex->current_select->parsing_place= FOR_LOOP_BOUND;
}
          remember_cpp_ptr expr remember_end
          { MDBP_R(444);
















            Lex->current_select->parsing_place= NO_MATTER;
}
        ;

cursor_actual_parameters:
          assignment_source_expr
          { MDBP_R(445); }




        | cursor_actual_parameters ',' assignment_source_expr
          { MDBP_R(446); }



        ;

opt_parenthesized_cursor_actual_parameters:
          /* Empty */                        { MDBP_R(447); }
        | '(' cursor_actual_parameters ')'   { MDBP_R(448); }
        ;

sp_proc_stmt_with_cursor:
         sp_proc_stmt_open { MDBP_R(449); }
       | sp_proc_stmt_fetch { MDBP_R(450); }
       | sp_proc_stmt_close { MDBP_R(451); }
       ;

sp_proc_stmt_open:
          OPEN_SYM ident opt_parenthesized_cursor_actual_parameters
          { MDBP_R(452); }



        | OPEN_SYM ident USING cursor_actual_parameters
          { MDBP_R(453); }



        | OPEN_SYM ident FOR_SYM sp_cursor_stmt_for_open
          { MDBP_R(454); }



        | OPEN_SYM ident FOR_SYM sp_cursor_stmt_for_open
          USING cursor_actual_parameters
          { MDBP_R(455); }



        ;

fetch_statement_source:
          ident { MDBP_R(456); }
        | FROM ident { MDBP_R(457); }
        | NEXT_SYM FROM ident { MDBP_R(458); }
        ;

sp_proc_stmt_fetch:
         FETCH_SYM fetch_statement_source INTO sp_fetch_list
         { MDBP_R(459); }



       | FETCH_SYM GROUP_SYM NEXT_SYM ROW_SYM
         { MDBP_R(460); }



        ;

sp_proc_stmt_close:
          CLOSE_SYM ident
          { MDBP_R(461); }



        ;

sp_fetch_list:
          ident
          { MDBP_R(462); }





        | sp_fetch_list ',' ident
          { MDBP_R(463); }




        ;

sp_if:
          expr_lex THEN_SYM
          { MDBP_R(464); }



          sp_if_then_statements
          { MDBP_R(465); }



          sp_elseifs
          { MDBP_R(466); }




        ;

sp_elseifs: { MDBP_R(467); }
          /* Empty */
        | ELSEIF_MARIADB_SYM sp_if { MDBP_R(468); }
        | ELSIF_ORACLE_SYM sp_if { MDBP_R(469); }
        | ELSE sp_if_then_statements { MDBP_R(470); }
        ;

case_stmt_specification:
          CASE_SYM
          { MDBP_R(471); }
















































          case_stmt_body
          else_clause_opt
          END
          CASE_SYM
          { MDBP_R(472); }












        ;

case_stmt_body:
          expr_lex
          { MDBP_R(473); }



          simple_when_clause_list
          { MDBP_R(474); }
        | searched_when_clause_list
          { MDBP_R(475); }
        ;

simple_when_clause_list:
          simple_when_clause { MDBP_R(476); }
        | simple_when_clause_list simple_when_clause { MDBP_R(477); }
        ;

searched_when_clause_list:
          searched_when_clause { MDBP_R(478); }
        | searched_when_clause_list searched_when_clause { MDBP_R(479); }
        ;

simple_when_clause:
          WHEN_SYM expr_lex
          { MDBP_R(480); }




          THEN_SYM
          sp_case_then_statements
          { MDBP_R(481); }



        ;

searched_when_clause:
          WHEN_SYM expr_lex
          { MDBP_R(482); }




          THEN_SYM
          sp_case_then_statements
          { MDBP_R(483); }



        ;

else_clause_opt:
          /* empty */
          { MDBP_R(484); }









        | ELSE sp_case_then_statements { MDBP_R(485); }
        ;

sp_opt_label:
          /* Empty  */  { MDBP_R(486); }
        | label_ident   { MDBP_R(487); }
        ;

/* This adds one shift/reduce conflict */
opt_sp_for_loop_direction:
            /* Empty */ { MDBP_R(488); }
          | REVERSE_SYM { MDBP_R(489); }
        ;

sp_for_loop_index_and_bounds:
          ident_for_loop_index sp_for_loop_bounds
          { MDBP_R(490); }



        ;

sp_for_loop_bounds:
          IN_SYM opt_sp_for_loop_direction for_loop_bound_expr
          DOT_DOT_SYM for_loop_bound_expr
          { MDBP_R(491); }


        | IN_SYM opt_sp_for_loop_direction for_loop_bound_expr
          { MDBP_R(492); }





        | IN_SYM opt_sp_for_loop_direction '(' sp_cursor_stmt ')'
          { MDBP_R(493); }




        ;

loop_body:
          sp_proc_stmts1 END LOOP_SYM
          { MDBP_R(494); }









        ;

repeat_body:
          sp_proc_stmts1 UNTIL_SYM expr_lex END REPEAT_SYM
          { MDBP_R(495); }



        ;

pop_sp_loop_label:
          sp_opt_label
          { MDBP_R(496); }



        ;

sp_labeled_control:
          sp_control_label LOOP_SYM
          { MDBP_R(497); }



          loop_body pop_sp_loop_label
          { MDBP_R(498); }
        | sp_control_label WHILE_SYM
          { MDBP_R(499); }



          while_body pop_sp_loop_label
          { MDBP_R(500); }
        | sp_control_label FOR_SYM
          { MDBP_R(501); }



          sp_for_loop_index_and_bounds
          { MDBP_R(502); }





          for_loop_statements
          { MDBP_R(503); }



          pop_sp_loop_label                    // The inner WHILE block
          { MDBP_R(504); }



        | sp_control_label REPEAT_SYM
          { MDBP_R(505); }



          repeat_body pop_sp_loop_label
          { MDBP_R(506); }
        ;

sp_unlabeled_control:
          LOOP_SYM
          { MDBP_R(507); }



          loop_body
          { MDBP_R(508); }


        | WHILE_SYM
          { MDBP_R(509); }



          while_body
          { MDBP_R(510); }


        | FOR_SYM
          { MDBP_R(511); }





          sp_for_loop_index_and_bounds
          { MDBP_R(512); }





          for_loop_statements
          { MDBP_R(513); }






        | REPEAT_SYM
          { MDBP_R(514); }



          repeat_body
          { MDBP_R(515); }


        ;

trg_action_time:
            BEFORE_SYM
            { MDBP_R(516); }
          | AFTER_SYM
            { MDBP_R(517); }
          ;

trg_event:
            INSERT
            { MDBP_R(518); }
          | UPDATE_SYM
            { MDBP_R(519); }
          | DELETE_SYM
            { MDBP_R(520); }
          ;

trg_events:
            trg_event { MDBP_R(521); }
          | trg_events OR_SYM trg_event { MDBP_R(522); }
          ;

create_body:
          create_field_list_parens
          { MDBP_R(523); }
          opt_create_table_options opt_create_partitioning opt_create_select { MDBP_R(524); }
        | opt_create_table_options opt_create_partitioning opt_create_select { MDBP_R(525); }
        | create_like
          { MDBP_R(526); }









        ;

create_like:
          LIKE table_ident                      { MDBP_R(527); }
        | LEFT_PAREN_LIKE LIKE table_ident ')'  { MDBP_R(528); }
        ;

opt_create_select:
          /* empty */ { MDBP_R(529); }
        | opt_duplicate opt_as create_select_query_expression
        opt_versioning_option
          { MDBP_R(530); }




        ;

create_select_query_expression:
          query_expression
          { MDBP_R(531); }



        | LEFT_PAREN_WITH with_clause query_expression_no_with_clause ')'
          { MDBP_R(532); }






        ;

opt_create_partitioning:
          opt_partitioning
          { MDBP_R(533); }








        ;

/*
 This part of the parser is about handling of the partition information.

 Its first version was written by Mikael Ronström with lots of answers to
 questions provided by Antony Curtis.

 The partition grammar can be called from three places.
 1) CREATE TABLE ... PARTITION ..
 2) ALTER TABLE table_name PARTITION ...
 3) PARTITION ...

 The first place is called when a new table is created from a MySQL client.
 The second place is called when a table is altered with the ALTER TABLE
 command from a MySQL client.
 The third place is called when opening an frm file and finding partition
 info in the .frm file. It is necessary to avoid allowing PARTITION to be
 an allowed entry point for SQL client queries. This is arranged by setting
 some state variables before arriving here.

 To be able to handle errors we will only set error code in this code
 and handle the error condition in the function calling the parser. This
 is necessary to ensure we can also handle errors when calling the parser
 from the openfrm function.
*/
opt_partitioning:
          /* empty */ { MDBP_R(534); }
        | partitioning { MDBP_R(535); }
        ;

partitioning:
          PARTITION_SYM have_partitioning
          { MDBP_R(536); }









          partition { MDBP_R(537); }
        ;

have_partitioning:
          /* empty */
          { MDBP_R(538); }










        ;

partition_entry:
          PARTITION_SYM
          { MDBP_R(539); }












          partition
          { MDBP_R(540); }


        ;

partition:
          BY
	  { MDBP_R(541); }
	   part_type_def opt_num_parts opt_sub_part part_defs { MDBP_R(542); }
        ;

part_type_def:
          opt_linear KEY_SYM opt_key_algo '(' part_field_list ')'
          { MDBP_R(543); }





        | opt_linear HASH_SYM
          { MDBP_R(544); }
          part_func { MDBP_R(545); }
        | RANGE_SYM part_func
          { MDBP_R(546); }
        | RANGE_SYM part_column_list
          { MDBP_R(547); }
        | LIST_SYM 
	  { MDBP_R(548);
	    Select->parsing_place= IN_PART_FUNC;
}
          part_func
          { MDBP_R(549);

	    Select->parsing_place= NO_MATTER;
}
        | LIST_SYM part_column_list
          { MDBP_R(550); }




        | SYSTEM_TIME_SYM
          { MDBP_R(551); }



          opt_versioning_rotation { MDBP_R(552); }
        ;

opt_linear:
          /* empty */ { MDBP_R(553); }
        | LINEAR_SYM
          { MDBP_R(554); }
        ;

opt_key_algo:
          /* empty */
          { MDBP_R(555); }
        | ALGORITHM_SYM '=' ident
          { MDBP_R(556); }







        | ALGORITHM_SYM '=' real_ulong_num
          { MDBP_R(557); }












        ;

part_field_list:
          /* empty */ { MDBP_R(558); }
        | part_field_item_list { MDBP_R(559); }
        ;

part_field_item_list:
          part_field_item { MDBP_R(560); }
        | part_field_item_list ',' part_field_item { MDBP_R(561); }
        ;

part_field_item:
          ident
          { MDBP_R(562); }









        ;

part_column_list:
          COLUMNS '(' part_field_list ')' opt_part_interval
          { MDBP_R(563); }








        ;

opt_part_interval:
          /* empty */ { MDBP_R(564); }
        | { MDBP_R(565); }
          opt_part_interval2
          { MDBP_R(566); }
        ;

opt_part_interval2:
          INTERVAL_SYM expr interval opt_auto
          { MDBP_R(567); }








        | INTERVAL_SYM '(' NUMTODSINTERVAL_SYM
                           '(' NUM ',' TEXT_STRING_sys ')' ')'
          { MDBP_R(568); }








        | INTERVAL_SYM '(' NUMTOYMINTERVAL_SYM
                           '(' NUM ',' TEXT_STRING_sys ')' ')'
          { MDBP_R(569); }








        ;

part_func:
          '(' part_func_expr ')'
          { MDBP_R(570); }






        ;

sub_part_func:
          '(' part_func_expr ')'
          { MDBP_R(571); }



        ;


opt_num_parts:
          /* empty */ { MDBP_R(572); }
        | PARTITIONS_SYM real_ulong_num
          { MDBP_R(573); }








        ;

opt_sub_part:
          /* empty */ { MDBP_R(574); }
        | SUBPARTITION_SYM BY opt_linear HASH_SYM sub_part_func
          { MDBP_R(575); }
          opt_num_subparts { MDBP_R(576); }
        | SUBPARTITION_SYM BY opt_linear KEY_SYM opt_key_algo
          '(' sub_part_field_list ')'
          { MDBP_R(577); }




          opt_num_subparts { MDBP_R(578); }
        ;

sub_part_field_list:
          sub_part_field_item { MDBP_R(579); }
        | sub_part_field_list ',' sub_part_field_item { MDBP_R(580); }
        ;

sub_part_field_item:
          ident
          { MDBP_R(581); }









        ;

part_func_expr:
          bit_expr
          { MDBP_R(582); }







        ;

opt_num_subparts:
          /* empty */ { MDBP_R(583); }
        | SUBPARTITIONS_SYM real_ulong_num
          { MDBP_R(584); }







        ;

part_defs:
          /* empty */
          { MDBP_R(585); }








        | '(' part_def_list ')'
          { MDBP_R(586); }

















        ;

part_def_list:
          part_definition { MDBP_R(587); }
        | part_def_list ',' part_definition { MDBP_R(588); }
        ;

opt_partition: { MDBP_R(589); }
          /* empty */
          | PARTITION_SYM { MDBP_R(590); }
          ;

part_definition:
          opt_partition
          { MDBP_R(591); }














          part_name
          opt_part_values
          opt_part_options
          opt_sub_partition
          { MDBP_R(592); }
        ;

part_name:
          ident
          { MDBP_R(593); }






        ;

opt_part_values:
          /* empty */
          { MDBP_R(594); }














        | VALUES_LESS_SYM THAN_SYM
          { MDBP_R(595); }











          part_func_max { MDBP_R(596); }
        | VALUES_IN_SYM
          { MDBP_R(597); }











          part_values_in { MDBP_R(598); }
        | CURRENT_SYM
          { MDBP_R(599); }





        | HISTORY_SYM
          { MDBP_R(600); }





        | DEFAULT
         { MDBP_R(601); }















        ;

part_func_max:
          MAXVALUE_SYM
          { MDBP_R(602); }





















        | part_value_item { MDBP_R(603); }
        ;

part_values_in:
          part_value_item
          { MDBP_R(604); }

























        | '(' part_value_list ')'
          { MDBP_R(605); }







        ;

part_value_list:
          part_value_item { MDBP_R(606); }
        | part_value_list ',' part_value_item { MDBP_R(607); }
        ;

part_value_item:
          '('
          { MDBP_R(608); }








          part_value_item_list { MDBP_R(609); }
          ')'
          { MDBP_R(610); }



















        ;

part_value_item_list:
          part_value_expr_item { MDBP_R(611); }
        | part_value_item_list ',' part_value_expr_item { MDBP_R(612); }
        ;

part_value_expr_item:
          MAXVALUE_SYM
          { MDBP_R(613); }









        | bit_expr
          { MDBP_R(614); }












        ;


opt_sub_partition:
          /* empty */
          { MDBP_R(615); }












        | '(' sub_part_list ')'
          { MDBP_R(616); }





















        ;

sub_part_list:
          sub_part_definition { MDBP_R(617); }
        | sub_part_list ',' sub_part_definition { MDBP_R(618); }
        ;

sub_part_definition:
          SUBPARTITION_SYM
          { MDBP_R(619); }































          sub_name opt_subpart_options { MDBP_R(620); }
        ;

sub_name:
          ident_or_text
          { MDBP_R(621); }





        ;

opt_part_options:
         /* empty */ { MDBP_R(622); }
       | part_option_list { MDBP_R(623); }
       ;

part_option_list:
         part_option_list part_option { MDBP_R(624); }
       | part_option { MDBP_R(625); }
       ;

part_option:
          server_part_option { MDBP_R(626); }
        | engine_defined_option
          { MDBP_R(627); }



        ;

opt_subpart_options:
         /* empty */ { MDBP_R(628); }
       | subpart_option_list { MDBP_R(629); }
       ;

subpart_option_list:
         subpart_option_list server_part_option { MDBP_R(630); }
       | server_part_option { MDBP_R(631); }
       ;

server_part_option:
          TABLESPACE opt_equal ident_or_text
          { MDBP_R(632); }
        | opt_storage ENGINE_SYM opt_equal storage_engines
          { MDBP_R(633); }




        | NODEGROUP_SYM opt_equal real_ulong_num
          { MDBP_R(634); }
        | MAX_ROWS opt_equal real_ulonglong_num
          { MDBP_R(635); }
        | MIN_ROWS opt_equal real_ulonglong_num
          { MDBP_R(636); }
        | DATA_SYM DIRECTORY_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(637); }
        | INDEX_SYM DIRECTORY_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(638); }
        | COMMENT_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(639); }
        ;

opt_versioning_rotation:
         /* empty */ { MDBP_R(640); }
       | { MDBP_R(641); }
         INTERVAL_SYM expr interval opt_versioning_interval_start opt_vers_auto_part
         { MDBP_R(642); }






       | LIMIT ulonglong_num opt_vers_auto_part
         { MDBP_R(643); }





       ;


opt_versioning_interval_start:
         /* empty */
         { MDBP_R(644); }


       | STARTS_SYM literal
         { MDBP_R(645); }


       ;

opt_vers_auto_part:
         /* empty */
         { MDBP_R(646); }


       | AUTO_SYM
         { MDBP_R(647); }


       ;

opt_auto:
          /* empty */ { MDBP_R(648); }
        | AUTO_SYM { MDBP_R(649); }
        ;

/*
 End of partition parser part
*/

opt_as:
          /* empty */ { MDBP_R(650); }
        | AS { MDBP_R(651); }
        ;

opt_create_database_options:
          /* empty */ { MDBP_R(652); }
        | create_database_options { MDBP_R(653); }
        ;

create_database_options:
          create_database_option { MDBP_R(654); }
        | create_database_options create_database_option { MDBP_R(655); }
        ;

create_database_option:
          default_collation { MDBP_R(656); }
        | default_charset { MDBP_R(657); }
        | COMMENT_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(658); }



        ;

opt_if_not_exists_table_element:
          /* empty */
          { MDBP_R(659); }


        | IF_SYM not EXISTS
          { MDBP_R(660); }


         ;

opt_if_not_exists:
          /* empty */        %prec PREC_BELOW_CONTRACTION_TOKEN2
          { MDBP_R(661); }


        | IF_SYM not EXISTS
          { MDBP_R(662); }


         ;

create_or_replace:
          CREATE /* empty */
          { MDBP_R(663); }


        | CREATE OR_SYM REPLACE
          { MDBP_R(664); }


         ;

opt_create_table_options: { MDBP_R(665); }
          /* empty */
        | create_table_options { MDBP_R(666); }
        ;

create_table_options_space_separated:
          create_table_option { MDBP_R(667); }
        | create_table_option create_table_options_space_separated { MDBP_R(668); }
        ;

create_table_options:
          create_table_option { MDBP_R(669); }
        | create_table_option     create_table_options { MDBP_R(670); }
        | create_table_option ',' create_table_options { MDBP_R(671); }
        ;

create_table_option:
          ENGINE_SYM opt_equal ident_or_text
          { MDBP_R(672); }













        | MAX_ROWS opt_equal ulonglong_num
          { MDBP_R(673); }



        | MIN_ROWS opt_equal ulonglong_num
          { MDBP_R(674); }



        | AVG_ROW_LENGTH opt_equal ulong_num
          { MDBP_R(675); }



        | PASSWORD_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(676); }



        | COMMENT_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(677); }



        | AUTO_INC opt_equal ulonglong_num
          { MDBP_R(678); }



        | PACK_KEYS_SYM opt_equal ulong_num
          { MDBP_R(679); }













        | PACK_KEYS_SYM opt_equal DEFAULT
          { MDBP_R(680); }




        | STATS_AUTO_RECALC_SYM opt_equal ulong_num
          { MDBP_R(681); }













        | STATS_AUTO_RECALC_SYM opt_equal DEFAULT
          { MDBP_R(682); }



        | STATS_PERSISTENT_SYM opt_equal ulong_num
          { MDBP_R(683); }













        | STATS_PERSISTENT_SYM opt_equal DEFAULT
          { MDBP_R(684); }




        | STATS_SAMPLE_PAGES_SYM opt_equal ulong_num
          { MDBP_R(685); }
















        | STATS_SAMPLE_PAGES_SYM opt_equal DEFAULT
          { MDBP_R(686); }



        | CHECKSUM_SYM opt_equal ulong_num
          { MDBP_R(687); }



        | TABLE_CHECKSUM_SYM opt_equal ulong_num
          { MDBP_R(688); }



        | PAGE_CHECKSUM_SYM opt_equal choice
          { MDBP_R(689); }



        | DELAY_KEY_WRITE_SYM opt_equal ulong_num
          { MDBP_R(690); }



        | ROW_FORMAT_SYM opt_equal row_types
          { MDBP_R(691); }



        | UNION_SYM opt_equal
          { MDBP_R(692); }


          '(' opt_table_list ')'
          { MDBP_R(693); }




















        | default_charset { MDBP_R(694); }
        | default_collation { MDBP_R(695); }
        | INSERT_METHOD opt_equal merge_insert_types
          { MDBP_R(696); }



        | DATA_SYM DIRECTORY_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(697); }



        | INDEX_SYM DIRECTORY_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(698); }



        | TABLESPACE ident
          { MDBP_R(699); }
        | STORAGE_SYM DISK_SYM
          { MDBP_R(700); }
        | STORAGE_SYM MEMORY_SYM
          { MDBP_R(701); }
        | KEY_BLOCK_SIZE opt_equal ulong_num
          { MDBP_R(702); }



        | TRANSACTIONAL_SYM opt_equal choice
          { MDBP_R(703); }



        | engine_defined_option
          { MDBP_R(704); }


        | SEQUENCE_SYM opt_equal choice
          { MDBP_R(705); }



        | versioning_option { MDBP_R(706); }
        ;

engine_defined_option:
          ident_options equal TEXT_STRING_sys
          { MDBP_R(707); }







        | ident_options equal ident
          { MDBP_R(708); }







        | ident_options equal real_ulonglong_num
          { MDBP_R(709); }





        | ident_options equal DEFAULT
          { MDBP_R(710); }




        ;

ident_options:
          IDENT_sys { MDBP_R(711); }
        | keyword_options
          { MDBP_R(712); }



        ;

/*
  These keywords are fine for option names.
*/
keyword_options:
          READ_ONLY_SYM { MDBP_R(713); }
        | CONNECTION_SYM { MDBP_R(714); }
        | WRAPPER_SYM { MDBP_R(715); }
        ;

opt_versioning_option: { MDBP_R(716); }
          /* empty */
        | versioning_option { MDBP_R(717); }
        ;

versioning_option:
        WITH_SYSTEM_SYM VERSIONING_SYM
          { MDBP_R(718); }














        ;

default_charset:
          opt_default charset opt_equal charset_name_or_default
          { MDBP_R(719); }




        ;

default_collation:
          opt_default COLLATE_SYM opt_equal collation_name_or_default
          { MDBP_R(720); }





        ;

storage_engines:
          ident_or_text
          { MDBP_R(721); }





        ;

known_storage_engines:
          ident_or_text
          { MDBP_R(722); }






        ;

row_types:
          DEFAULT        { MDBP_R(723); }
        | FIXED_SYM      { MDBP_R(724); }
        | DYNAMIC_SYM    { MDBP_R(725); }
        | COMPRESSED_SYM { MDBP_R(726); }
        | REDUNDANT_SYM  { MDBP_R(727); }
        | COMPACT_SYM    { MDBP_R(728); }
        | PAGE_SYM       { MDBP_R(729); }
        ;

merge_insert_types:
         NO_SYM          { MDBP_R(730); }
       | FIRST_SYM       { MDBP_R(731); }
       | LAST_SYM        { MDBP_R(732); }
       ;

udf_type:
          STRING_SYM { MDBP_R(733); }
        | REAL { MDBP_R(734); }
        | DECIMAL_SYM { MDBP_R(735); }
        | INT_SYM { MDBP_R(736); }
        ;


create_field_list:
        field_list
        { MDBP_R(737); }


        ;

create_field_list_parens:
        LEFT_PAREN_ALT field_list ')'
        { MDBP_R(738); }


        ;

field_list:
          field_list_item { MDBP_R(739); }
        | field_list ',' field_list_item { MDBP_R(740); }
        ;

field_list_item:
          column_def { MDBP_R(741); }
        | key_def { MDBP_R(742); }
        | constraint_def { MDBP_R(743); }
        | period_for_system_time { MDBP_R(744); }
        | PERIOD_SYM period_for_application_time { MDBP_R(745); }
        ;

column_def:
          field_spec
          { MDBP_R(746); }
        | field_spec opt_constraint references
          { MDBP_R(747); }





        ;

key_def:
          key_or_index opt_if_not_exists opt_ident opt_USING_key_algorithm
          { MDBP_R(748); }




          '(' key_list ')' normal_key_options { MDBP_R(749); }
        | key_or_index opt_if_not_exists ident TYPE_SYM btree_or_rtree
          { MDBP_R(750); }




          '(' key_list ')' normal_key_options { MDBP_R(751); }
        | fulltext opt_key_or_index opt_if_not_exists opt_ident
          { MDBP_R(752); }




          '(' key_list ')' fulltext_key_options { MDBP_R(753); }
        | spatial_or_vector opt_key_or_index opt_if_not_exists opt_ident
          { MDBP_R(754); }




          '(' key_part_simple ')' spatial_key_options
          { MDBP_R(755); }


        | opt_constraint constraint_key_type
          opt_if_not_exists opt_ident
          opt_USING_key_algorithm
          { MDBP_R(756); }




          '(' key_list opt_without_overlaps ')' normal_key_options { MDBP_R(757); }
        | opt_constraint constraint_key_type opt_if_not_exists ident
          TYPE_SYM btree_or_rtree
          { MDBP_R(758); }




          '(' key_list opt_without_overlaps ')' normal_key_options { MDBP_R(759); }
        | opt_constraint FOREIGN KEY_SYM opt_if_not_exists opt_ident
          { MDBP_R(760); }








          '(' key_list ')' references
          { MDBP_R(761); }




	;

constraint_def:
         opt_constraint check_constraint
         { MDBP_R(762); }


       ;

period_for_system_time:
          // If FOR_SYM is followed by SYSTEM_TIME_SYM then they are merged to: FOR_SYSTEM_TIME_SYM .
          PERIOD_SYM FOR_SYSTEM_TIME_SYM '(' ident ',' ident ')'
          { MDBP_R(763); }



        ;

period_for_application_time:
          FOR_SYM ident '(' ident ',' ident ')'
          { MDBP_R(764); }



        ;

opt_check_constraint:
          /* empty */      { MDBP_R(765); }
        | check_constraint { MDBP_R(766); }
        ;

check_constraint:
          CHECK_SYM '('
          { MDBP_R(767); }
          expr ')'
          { MDBP_R(768); }






        ;

opt_constraint_no_id:
          /* Empty */  { MDBP_R(769); }
        | CONSTRAINT   { MDBP_R(770); }
        ;

opt_constraint:
          /* empty */ { MDBP_R(771); }
        | constraint { MDBP_R(772); }
        ;

constraint:
          CONSTRAINT opt_ident { MDBP_R(773); }
        ;

field_spec:
          field_ident
          { $$ = 0; MDBP_R(774); }














          field_type_or_serial opt_check_constraint
          { MDBP_R(775); }

















        ;

field_type_or_serial:
          qualified_field_type field_def
          { MDBP_R(776); }









        | SERIAL_SYM
          { MDBP_R(777); }









          opt_serial_attribute { MDBP_R(778); }
        ;

opt_serial_attribute:
          /* empty */ { MDBP_R(779); }
        | opt_serial_attribute_list { MDBP_R(780); }
        ;

opt_serial_attribute_list:
          opt_serial_attribute_list serial_attribute { MDBP_R(781); }
        | serial_attribute { MDBP_R(782); }
        ;

opt_asrow_attribute:
          /* empty */ { MDBP_R(783); }
        | opt_asrow_attribute_list { MDBP_R(784); }
        ;

opt_asrow_attribute_list:
          opt_asrow_attribute_list asrow_attribute { MDBP_R(785); }
        | asrow_attribute { MDBP_R(786); }
        ;

field_def:
          /* empty */     { MDBP_R(787); }
        | attribute_list { MDBP_R(788); }
        | opt_generated_always AS virtual_column_func
         { MDBP_R(789); }


          vcol_opt_specifier vcol_opt_attribute
          { MDBP_R(790); }


        | opt_generated_always AS ROW_SYM START_SYM opt_asrow_attribute
          { MDBP_R(791); }




        | opt_generated_always AS ROW_SYM END opt_asrow_attribute
          { MDBP_R(792); }




        ;

opt_generated_always:
          /* empty */ { MDBP_R(793); }
        | GENERATED_SYM ALWAYS_SYM { MDBP_R(794); }
        ;

vcol_opt_specifier:
          /* empty */
          { MDBP_R(795); }


        | VIRTUAL_SYM
          { MDBP_R(796); }


        | PERSISTENT_SYM
          { MDBP_R(797); }


        | STORED_SYM
          { MDBP_R(798); }


        ;

vcol_opt_attribute:
          /* empty */ { MDBP_R(799); }
        | vcol_opt_attribute_list { MDBP_R(800); }
        ;

vcol_opt_attribute_list:
          vcol_opt_attribute_list vcol_attribute { MDBP_R(801); }
        | vcol_attribute { MDBP_R(802); }
        ;

vcol_attribute:
          UNIQUE_SYM
          { MDBP_R(803); }




        | UNIQUE_SYM KEY_SYM
          { MDBP_R(804); }




        | COMMENT_SYM TEXT_STRING_sys { MDBP_R(805); }
        | INVISIBLE_SYM
          { MDBP_R(806); }


        ;

parse_vcol_expr:
          PARSE_VCOL_EXPR_SYM
          { MDBP_R(807); }









          expr
          { MDBP_R(808); }






        ;

parenthesized_expr:
          expr { MDBP_R(809); }
        | expr ',' expr_list
          { MDBP_R(810); }





          ;

virtual_column_func:
          '(' parenthesized_expr ')'
          { MDBP_R(811); }






        | subquery
          { MDBP_R(812); }








        ;

expr_or_literal: column_default_non_parenthesized_expr { MDBP_R(813); } | signed_literal { MDBP_R(814); } ;

column_default_expr:
          virtual_column_func { MDBP_R(815); }
        | expr_or_literal
          { MDBP_R(816); }



        ;

field_type: field_type_all
        { MDBP_R(817); }


        ;

qualified_field_type:
          field_type_all
          { MDBP_R(818); }


        | sp_decl_ident '.' field_type_all
          { MDBP_R(819); }



        ;

udt_name:
          IDENT_sys                     { MDBP_R(820); }
        | reserved_keyword_udt          { MDBP_R(821); }
        | non_reserved_keyword_udt      { MDBP_R(822); }
        ;

field_type_all_builtin:
          field_type_numeric { MDBP_R(823); }
        | field_type_temporal { MDBP_R(824); }
        | field_type_string { MDBP_R(825); }
        | field_type_lob { MDBP_R(826); }
        | field_type_misc { MDBP_R(827); }
      ;

field_type_all:
          field_type_all_builtin { MDBP_R(828); }
        | udt_name float_options srid_option opt_binary
          { MDBP_R(829); }



        ;

field_type_all_with_typedefs:
          field_type_all_builtin
          { MDBP_R(830); }


        | udt_name float_options srid_option opt_binary
          { MDBP_R(831); }



        | sp_decl_ident '.' ident
          { MDBP_R(832); }



        | sp_decl_ident '.' ident '.' ident
          { MDBP_R(833); }



        ;

field_type_numeric:
          int_type opt_field_length last_field_options
          { MDBP_R(834); }


        | real_type opt_precision last_field_options   { MDBP_R(835); }
        | FLOAT_SYM float_options last_field_options
          { MDBP_R(836); }












        | BIT_SYM opt_field_length
          { MDBP_R(837); }


        | BOOL_SYM
          { MDBP_R(838); }


        | BOOLEAN_SYM
          { MDBP_R(839); }


        | DECIMAL_SYM float_options last_field_options
          { MDBP_R(840); }
        | NUMBER_ORACLE_SYM float_options last_field_options
          { MDBP_R(841); }





        | NUMERIC_SYM float_options last_field_options
          { MDBP_R(842); }
        | FIXED_SYM float_options last_field_options
          { MDBP_R(843); }
        ;


opt_binary_and_compression:
          /* empty */                                      { MDBP_R(844); }
        | binary                                           { MDBP_R(845); }
        | compressed opt_binary                            { MDBP_R(846); }
        ;

field_type_string:
          char opt_field_length opt_binary
          { MDBP_R(847); }


        | nchar opt_field_length opt_bin_mod
          { MDBP_R(848); }



        | BINARY opt_field_length
          { MDBP_R(849); }


        | varchar opt_field_length opt_binary_and_compression
          { MDBP_R(850); }


        | VARCHAR2_ORACLE_SYM opt_field_length opt_binary_and_compression
          { MDBP_R(851); }


        | nvarchar opt_field_length opt_compressed opt_bin_mod
          { MDBP_R(852); }



        | VARBINARY opt_field_length opt_compressed
          { MDBP_R(853); }


        | RAW_ORACLE_SYM opt_field_length opt_compressed
          { MDBP_R(854); }


        ;

field_type_temporal:
          YEAR_SYM opt_field_length last_field_options
          { MDBP_R(855); }












        | DATE_SYM { MDBP_R(856); }
        | TIME_SYM opt_field_length
          { MDBP_R(857); }





        | TIMESTAMP opt_field_length
          { MDBP_R(858); }





        | DATETIME opt_field_length
          { MDBP_R(859); }


        ;


field_type_lob:
          TINYBLOB opt_compressed
          { MDBP_R(860); }


        | BLOB_MARIADB_SYM opt_field_length opt_compressed
          { MDBP_R(861); }


        | BLOB_ORACLE_SYM field_length opt_compressed
          { MDBP_R(862); }


        | BLOB_ORACLE_SYM opt_compressed
          { MDBP_R(863); }


        | MEDIUMBLOB opt_compressed
          { MDBP_R(864); }


        | LONGBLOB opt_compressed
          { MDBP_R(865); }


        | LONG_SYM VARBINARY opt_compressed
          { MDBP_R(866); }


        | LONG_SYM varchar opt_binary_and_compression
          { MDBP_R(867); }
        | TINYTEXT opt_binary_and_compression
          { MDBP_R(868); }
        | TEXT_SYM opt_field_length opt_binary_and_compression
          { MDBP_R(869); }
        | MEDIUMTEXT opt_binary_and_compression
          { MDBP_R(870); }
        | LONGTEXT opt_binary_and_compression
          { MDBP_R(871); }
        | CLOB_ORACLE_SYM opt_binary_and_compression
          { MDBP_R(872); }
        | LONG_SYM opt_binary_and_compression
          { MDBP_R(873); }
        | JSON_SYM opt_compressed
          { MDBP_R(874); }


        ;

field_type_misc:
          ENUM '(' string_list ')' opt_binary
          { MDBP_R(875); }
        | SET '(' string_list ')' opt_binary
          { MDBP_R(876); }
        ;

char:
          CHAR_SYM { MDBP_R(877); }
        ;

nchar:
          NCHAR_SYM { MDBP_R(878); }
        | NATIONAL_SYM CHAR_SYM { MDBP_R(879); }
        ;

varchar:
          char VARYING { MDBP_R(880); }
        | VARCHAR { MDBP_R(881); }
        ;

nvarchar:
          NATIONAL_SYM VARCHAR { MDBP_R(882); }
        | NVARCHAR_SYM { MDBP_R(883); }
        | NCHAR_SYM VARCHAR { MDBP_R(884); }
        | NATIONAL_SYM CHAR_SYM VARYING { MDBP_R(885); }
        | NCHAR_SYM VARYING { MDBP_R(886); }
        ;

int_type:
          INT_SYM   { MDBP_R(887); }
        | TINYINT   { MDBP_R(888); }
        | SMALLINT  { MDBP_R(889); }
        | MEDIUMINT { MDBP_R(890); }
        | BIGINT    { MDBP_R(891); }
        ;

real_type:
          REAL
          { MDBP_R(892); }




        | DOUBLE_SYM           { MDBP_R(893); }
        | DOUBLE_SYM PRECISION { MDBP_R(894); }
        ;

srid_option:
          /* empty */
          { MDBP_R(895); }
        |
          REF_SYSTEM_ID_SYM '=' NUM
          { MDBP_R(896); }


        ;

float_options:
          /* empty */  { MDBP_R(897); }
        | field_length { MDBP_R(898); }
        | precision { MDBP_R(899); }
        ;

precision:
          '(' NUM ',' NUM ')' { MDBP_R(900); }
        ;

field_options:
          /* empty */       { MDBP_R(901); }
        | SIGNED_SYM        { MDBP_R(902); }
        | UNSIGNED          { MDBP_R(903); }
        | ZEROFILL          { MDBP_R(904); }
        | UNSIGNED ZEROFILL { MDBP_R(905); }
        | ZEROFILL UNSIGNED { MDBP_R(906); }
        ;

last_field_options:
          field_options { MDBP_R(907); }
        ;

field_length_str:
          '(' LONG_NUM ')'      { MDBP_R(908); }
        | '(' ULONGLONG_NUM ')' { MDBP_R(909); }
        | '(' DECIMAL_NUM ')'   { MDBP_R(910); }
        | '(' NUM ')'           { MDBP_R(911); }
        ;

field_length: field_length_str  { MDBP_R(912); }
        ;


field_scale: field_length_str   { MDBP_R(913); }
        ;


opt_field_length:
          /* empty */  { MDBP_R(914); }
        | field_length { MDBP_R(915); }
        ;

opt_field_scale:
          /* empty */ { MDBP_R(916); }
        | field_scale { MDBP_R(917); }
        ;

opt_precision:
          /* empty */    { MDBP_R(918); }
        | precision      { MDBP_R(919); }
        ;


attribute_list:
          attribute_list attribute
          { MDBP_R(920); }




        | attribute { MDBP_R(921); }
        ;

attribute:
          NULL_SYM
          { MDBP_R(922); }




        | DEFAULT column_default_expr
          { MDBP_R(923); }



        | ON UPDATE_SYM NOW_SYM opt_default_time_precision
          { MDBP_R(924); }






        | AUTO_INC { MDBP_R(925); }
        | SERIAL_SYM DEFAULT VALUE_SYM
          { MDBP_R(926); }





        | COLLATE_SYM collation_name
          { MDBP_R(927); }


        | serial_attribute { MDBP_R(928); }
        ;

opt_compression_method:
          /* empty */ { MDBP_R(929); }
        | equal ident { MDBP_R(930); }
        ;

opt_compressed:
          /* empty */ { MDBP_R(931); }
        | compressed { MDBP_R(932); }
        ;

opt_enable:
          /* empty */ { MDBP_R(933); }
        | ENABLE_SYM { MDBP_R(934); }
        ;

compressed:
          COMPRESSED_SYM opt_compression_method
          { MDBP_R(935); }



        ;

asrow_attribute:
          not NULL_SYM opt_enable
          { MDBP_R(936); }


        | opt_primary KEY_SYM
          { MDBP_R(937); }




        | vcol_attribute { MDBP_R(938); }
        ;

serial_attribute:
          asrow_attribute { MDBP_R(939); }
        | engine_defined_option
          { MDBP_R(940); }


        | with_or_without_system VERSIONING_SYM
          { MDBP_R(941); }








        ;

with_or_without_system:
        WITH_SYSTEM_SYM
          { MDBP_R(942); }




        | WITHOUT SYSTEM
          { MDBP_R(943); }




        ;


charset:
          CHAR_SYM SET { MDBP_R(944); }
        | CHARSET { MDBP_R(945); }
        ;

charset_name:
          ident_or_text
          { MDBP_R(946); }





        | BINARY { MDBP_R(947); }
        ;

charset_name_or_default:
          charset_name { MDBP_R(948); }
        | DEFAULT    { MDBP_R(949); }
        ;

opt_load_data_charset:
          /* Empty */ { MDBP_R(950); }
        | charset charset_name_or_default { MDBP_R(951); }
        ;

old_or_new_charset_name:
          ident_or_text
          { MDBP_R(952); }







        | BINARY { MDBP_R(953); }
        ;

old_or_new_charset_name_or_default:
          old_or_new_charset_name { MDBP_R(954); }
        | DEFAULT    { MDBP_R(955); }
        ;

collation_name:
          ident_or_text
          { MDBP_R(956); }



        ;

collation_name_or_default:
          collation_name { MDBP_R(957); }
        | DEFAULT        { MDBP_R(958); }
        | BINARY // MySQL compatibility
          { MDBP_R(959); }



        ;

opt_default:
          /* empty */ { MDBP_R(960); }
        | DEFAULT { MDBP_R(961); }
        ;

charset_or_alias:
          charset charset_name { MDBP_R(962); }
        | ASCII_SYM { MDBP_R(963); }
        | UNICODE_SYM
          { MDBP_R(964); }



        ;

opt_binary:
          /* empty */             { MDBP_R(965); }
        | binary { MDBP_R(966); }
        ;

binary:
          BYTE_SYM
          { MDBP_R(967); }


        | charset_or_alias
          { MDBP_R(968); }


        | charset_or_alias BINARY
          { MDBP_R(969); }



        | BINARY { MDBP_R(970); }
        | BINARY charset_or_alias
          { MDBP_R(971); }



        | charset_or_alias COLLATE_SYM DEFAULT
          { MDBP_R(972); }





        | charset_or_alias COLLATE_SYM collation_name
          { MDBP_R(973); }







        | COLLATE_SYM collation_name
          { MDBP_R(974); }


        | COLLATE_SYM DEFAULT
          { MDBP_R(975); }


        | charset_or_alias COLLATE_SYM BINARY // MySQL compatibility
          { MDBP_R(976); }








        | COLLATE_SYM BINARY // MySQL compatibility
          { MDBP_R(977); }




        ;

opt_bin_mod:
          /* empty */   { MDBP_R(978); }
        | BINARY        { MDBP_R(979); }
        ;

ws_nweights:
        '(' real_ulong_num
        { MDBP_R(980); }






        ')'
        { MDBP_R(981); }
        ;

ws_level_flag_desc:
        ASC { MDBP_R(982); }
        | DESC { MDBP_R(983); }
        ;

ws_level_flag_reverse:
        REVERSE_SYM { MDBP_R(984); } ;

ws_level_flags:
        /* empty */ { MDBP_R(985); }
        | ws_level_flag_desc { MDBP_R(986); }
        | ws_level_flag_desc ws_level_flag_reverse { MDBP_R(987); }
        | ws_level_flag_reverse { MDBP_R(988); }
        ;

ws_level_number:
        real_ulong_num
        { MDBP_R(989); }



        ;

ws_level_list_item:
        ws_level_number ws_level_flags
        { MDBP_R(990); }


        ;

ws_level_list:
        ws_level_list_item { MDBP_R(991); }
        | ws_level_list ',' ws_level_list_item { MDBP_R(992); }
        ;

ws_level_range:
        ws_level_number '-' ws_level_number
        { MDBP_R(993); }





        ;

ws_level_list_or_range:
        ws_level_list { MDBP_R(994); }
        | ws_level_range { MDBP_R(995); }
        ;

opt_ws_levels:
        /* empty*/ { MDBP_R(996); }
        | LEVEL_SYM ws_level_list_or_range { MDBP_R(997); }
        ;

opt_primary: { MDBP_R(998); }
          /* empty */
        | PRIMARY_SYM { MDBP_R(999); }
        ;

references:
          REFERENCES
          table_ident
          opt_ref_list
          opt_match_clause
          opt_on_update_delete
          { MDBP_R(1000); }


        ;

opt_ref_list:
          /* empty */
          { MDBP_R(1001); }
        | '(' ref_list ')' { MDBP_R(1002); }
        ;

ref_list:
          ref_list ',' ident
          { MDBP_R(1003); }





        | ident
          { MDBP_R(1004); }







        ;

opt_match_clause:
          /* empty */
          { MDBP_R(1005); }
        | MATCH FULL
          { MDBP_R(1006); }
        | MATCH PARTIAL
          { MDBP_R(1007); }
        | MATCH SIMPLE_SYM
          { MDBP_R(1008); }
        ;

opt_on_update_delete:
          /* empty */
          { MDBP_R(1009); }




        | ON UPDATE_SYM delete_option
          { MDBP_R(1010); }




        | ON DELETE_SYM delete_option
          { MDBP_R(1011); }




        | ON UPDATE_SYM delete_option
          ON DELETE_SYM delete_option
          { MDBP_R(1012); }




        | ON DELETE_SYM delete_option
          ON UPDATE_SYM delete_option
          { MDBP_R(1013); }




        ;

delete_option:
          RESTRICT      { MDBP_R(1014); }
        | CASCADE       { MDBP_R(1015); }
        | SET NULL_SYM  { MDBP_R(1016); }
        | NO_SYM ACTION { MDBP_R(1017); }
        | SET DEFAULT   { MDBP_R(1018); }
        ;

constraint_key_type:
          PRIMARY_SYM KEY_SYM { MDBP_R(1019); }
        | UNIQUE_SYM opt_key_or_index { MDBP_R(1020); }
        ;

key_or_index:
          KEY_SYM { MDBP_R(1021); }
        | INDEX_SYM { MDBP_R(1022); }
        ;

opt_key_or_index:
          /* empty */ { MDBP_R(1023); }
        | key_or_index { MDBP_R(1024); }
        ;

keys_or_index:
          KEYS { MDBP_R(1025); }
        | INDEX_SYM { MDBP_R(1026); }
        | INDEXES { MDBP_R(1027); }
        ;

fulltext:
          FULLTEXT_SYM { MDBP_R(1028); }
        ;

spatial_or_vector:
          SPATIAL_SYM
          { MDBP_R(1029); }


        | VECTOR_SYM { MDBP_R(1030); }
        ;

normal_key_options:
          /* empty */ { MDBP_R(1031); }
        | normal_key_opts { MDBP_R(1032); }
        ;

fulltext_key_options:
          /* empty */ { MDBP_R(1033); }
        | fulltext_key_opts { MDBP_R(1034); }
        ;

spatial_key_options:
          /* empty */ { MDBP_R(1035); }
        | spatial_key_opts { MDBP_R(1036); }
        ;

normal_key_opts:
          normal_key_opt { MDBP_R(1037); }
        | normal_key_opts normal_key_opt { MDBP_R(1038); }
        ;

spatial_key_opts:
          spatial_key_opt { MDBP_R(1039); }
        | spatial_key_opts spatial_key_opt { MDBP_R(1040); }
        ;

fulltext_key_opts:
          fulltext_key_opt { MDBP_R(1041); }
        | fulltext_key_opts fulltext_key_opt { MDBP_R(1042); }
        ;

opt_USING_key_algorithm:
          /* Empty*/              { MDBP_R(1043); }
        | USING    btree_or_rtree { MDBP_R(1044); }
        ;

/* TYPE is a valid identifier, so it's handled differently than USING */
opt_key_algorithm_clause:
          /* Empty*/              { MDBP_R(1045); }
        | USING    btree_or_rtree { MDBP_R(1046); }
        | TYPE_SYM btree_or_rtree { MDBP_R(1047); }
        ;

key_using_alg:
          USING btree_or_rtree
          { MDBP_R(1048); }
        | TYPE_SYM btree_or_rtree
          { MDBP_R(1049); }
        ;

all_key_opt:
          KEY_BLOCK_SIZE opt_equal ulong_num
          { MDBP_R(1050); }



        | COMMENT_SYM TEXT_STRING_sys
          { MDBP_R(1051); }
        | VISIBLE_SYM
          { MDBP_R(1052); }


        | ignorability
          { MDBP_R(1053); }


        | engine_defined_option
          { MDBP_R(1054); }


        ;

normal_key_opt:
          all_key_opt { MDBP_R(1055); }
        | key_using_alg { MDBP_R(1056); }
        ;

spatial_key_opt:
          all_key_opt { MDBP_R(1057); }
        ;

fulltext_key_opt:
          all_key_opt { MDBP_R(1058); }
        | WITH PARSER_SYM IDENT_sys
          { MDBP_R(1059); }





        ;

btree_or_rtree:
          BTREE_SYM { MDBP_R(1060); }
        | RTREE_SYM { MDBP_R(1061); }
        | HASH_SYM  { MDBP_R(1062); }
        ;

ignorability:
          IGNORED_SYM { MDBP_R(1063); }
        | NOT_SYM IGNORED_SYM { MDBP_R(1064); }
        ;

key_list:
          key_list ',' key_part order_dir
          { MDBP_R(1065); }



        | key_part order_dir
          { MDBP_R(1066); }



        ;

opt_without_overlaps:
         /* nothing */ { MDBP_R(1067); }
         | ',' ident WITHOUT OVERLAPS_SYM
          { MDBP_R(1068); }



         ;

key_part:
          key_part_simple { MDBP_R(1069); }
        | ident '(' NUM ')'
          { MDBP_R(1070); }







        ;

key_part_simple:
          ident
          { MDBP_R(1071); }




          ;

opt_ident:
          /* empty */ { MDBP_R(1072); }
        | field_ident { MDBP_R(1073); }
        ;

string_list:
          text_string
          { MDBP_R(1074); }
        | string_list ',' text_string
          { MDBP_R(1075); }
        ;

/*
** Alter table
*/

alter:
          ALTER
          { MDBP_R(1076); }














          alter_options TABLE_SYM opt_if_exists table_ident opt_lock_wait_timeout
          { MDBP_R(1077); }










          alter_commands
          { MDBP_R(1078); }









        | ALTER DATABASE ident_or_empty
          { MDBP_R(1079); }




          create_database_options
          { MDBP_R(1080); }








        | ALTER DATABASE COMMENT_SYM opt_equal TEXT_STRING_sys
          { MDBP_R(1081); }




          opt_create_database_options
          { MDBP_R(1082); }







        | ALTER DATABASE ident UPGRADE_SYM DATA_SYM DIRECTORY_SYM NAME_SYM
          { MDBP_R(1083); }






        | ALTER PROCEDURE_SYM sp_name
          { MDBP_R(1084); }



          sp_a_chistics
          stmt_end { MDBP_R(1085); }
        | ALTER FUNCTION_SYM sp_name
          { MDBP_R(1086); }



          sp_a_chistics
          stmt_end { MDBP_R(1087); }
        | ALTER view_algorithm definer_opt opt_view_suid VIEW_SYM table_ident
          { MDBP_R(1088); }





          view_list_opt AS view_select stmt_end { MDBP_R(1089); }
        | ALTER definer_opt opt_view_suid VIEW_SYM table_ident
          /*
            We have two separate rules for ALTER VIEW rather that
            optional view_algorithm above, to resolve the ambiguity
            with the ALTER EVENT below.
          */
          { MDBP_R(1090); }





          view_list_opt AS view_select stmt_end { MDBP_R(1091); }
        | ALTER definer_opt remember_name EVENT_SYM sp_name
          { MDBP_R(1092); }

















          ev_alter_on_schedule_completion
          opt_ev_rename_to
          opt_ev_status
          opt_ev_comment
          opt_ev_sql_stmt
          { MDBP_R(1093); }














        | ALTER SERVER_SYM ident_or_text
          { MDBP_R(1094); }



 OPTIONS_SYM '(' server_options_list ')' { MDBP_R(1095); }
          /* ALTER USER foo is allowed for MySQL compatibility. */
        | ALTER USER_SYM opt_if_exists clear_privileges grant_list
          opt_require_clause opt_resource_options opt_account_locking_and_opt_password_expiration
          { MDBP_R(1096); }



        | ALTER SEQUENCE_SYM opt_if_exists
          { MDBP_R(1097); }







          table_ident
          { MDBP_R(1098); }








          sequence_defs
          { MDBP_R(1099); }











 stmt_end { MDBP_R(1100); }
        ;

account_locking_option:
          LOCK_SYM
          { MDBP_R(1101); }


        | UNLOCK_SYM
          { MDBP_R(1102); }


        ;

opt_password_expire_option:
          /* empty */
          { MDBP_R(1103); }


        | NEVER_SYM
          { MDBP_R(1104); }


        | DEFAULT
          { MDBP_R(1105); }


        | INTERVAL_SYM NUM DAY_SYM
          { MDBP_R(1106); }




        ;

opt_account_locking_and_opt_password_expiration: { MDBP_R(1107); }
          /* empty */
        | ACCOUNT_SYM account_locking_option { MDBP_R(1108); }
        | PASSWORD_SYM EXPIRE_SYM opt_password_expire_option { MDBP_R(1109); }
        | ACCOUNT_SYM account_locking_option PASSWORD_SYM EXPIRE_SYM opt_password_expire_option { MDBP_R(1110); }
        | PASSWORD_SYM EXPIRE_SYM opt_password_expire_option ACCOUNT_SYM account_locking_option { MDBP_R(1111); }
        ;

ev_alter_on_schedule_completion:
          /* empty */ { MDBP_R(1112); }
        | ON SCHEDULE_SYM ev_schedule_time { MDBP_R(1113); }
        | ev_on_completion { MDBP_R(1114); }
        | ON SCHEDULE_SYM ev_schedule_time ev_on_completion { MDBP_R(1115); }
        ;

opt_ev_rename_to:
          /* empty */ { MDBP_R(1116); }
        | RENAME TO_SYM sp_name
          { MDBP_R(1117); }







        ;

opt_ev_sql_stmt:
          /* empty*/ { MDBP_R(1118); }
        | DO_SYM ev_sql_stmt { MDBP_R(1119); }
        ;

ident_or_empty:
          /* empty */
          %prec PREC_BELOW_IDENTIFIER_OPT_SPECIAL_CASE { MDBP_R(1120); }
        | ident { MDBP_R(1121); }
        ;

alter_commands: { MDBP_R(1122); }
          /* empty */
        | DISCARD TABLESPACE
          { MDBP_R(1123); }






        | IMPORT TABLESPACE
          { MDBP_R(1124); }







        | alter_list
          opt_partitioning { MDBP_R(1125); }
        | alter_list
          remove_partitioning { MDBP_R(1126); }
        | remove_partitioning { MDBP_R(1127); }
        | partitioning { MDBP_R(1128); }
/*
  This part was added for release 5.1 by Mikael Ronstrm.
  From here we insert a number of commands to manage the partitions of a
  partitioned table such as adding partitions, dropping partitions,
  reorganising partitions in various manners. In future releases the list
  will be longer.
*/
        | add_partition_rule { MDBP_R(1129); }
        | DROP PARTITION_SYM opt_if_exists alt_part_name_list
          { MDBP_R(1130); }




        | REBUILD_SYM PARTITION_SYM opt_no_write_to_binlog
          all_or_alt_part_name_list
          { MDBP_R(1131); }




        | OPTIMIZE PARTITION_SYM opt_no_write_to_binlog
          all_or_alt_part_name_list
          { MDBP_R(1132); }









          opt_no_write_to_binlog { MDBP_R(1133); }
        | ANALYZE_SYM PARTITION_SYM opt_no_write_to_binlog
          all_or_alt_part_name_list
          { MDBP_R(1134); }









        | CHECK_SYM PARTITION_SYM all_or_alt_part_name_list
          { MDBP_R(1135); }








          opt_mi_check_type { MDBP_R(1136); }
        | REPAIR PARTITION_SYM opt_no_write_to_binlog
          all_or_alt_part_name_list
          { MDBP_R(1137); }









          opt_mi_repair_type { MDBP_R(1138); }
        | COALESCE PARTITION_SYM opt_no_write_to_binlog real_ulong_num
          { MDBP_R(1139); }





        | TRUNCATE_SYM PARTITION_SYM all_or_alt_part_name_list
          { MDBP_R(1140); }








        | reorg_partition_rule { MDBP_R(1141); }
        | EXCHANGE_SYM PARTITION_SYM alt_part_name_item
          WITH TABLE_SYM table_ident opt_without_validation have_partitioning
          { MDBP_R(1142); }



        | CONVERT_SYM PARTITION_SYM alt_part_name_item
          TO_SYM TABLE_SYM table_ident opt_without_validation have_partitioning
          { MDBP_R(1143); }








        | CONVERT_SYM TABLE_SYM table_ident
          { MDBP_R(1144); }




























          TO_SYM PARTITION_SYM part_definition opt_without_validation have_partitioning
          { MDBP_R(1145); }





        ;

remove_partitioning:
          REMOVE_SYM PARTITIONING_SYM
          { MDBP_R(1146); }


        ;

all_or_alt_part_name_list:
          ALL
          { MDBP_R(1147); }


        | alt_part_name_list { MDBP_R(1148); }
        ;

add_partition_rule:
          ADD PARTITION_SYM opt_if_not_exists
          opt_no_write_to_binlog
          { MDBP_R(1149); }










          add_part_extra
          { MDBP_R(1150); }
        ;

add_part_extra: { MDBP_R(1151); }
          /* empty */
        | '(' part_def_list ')'
          { MDBP_R(1152); }



        | PARTITIONS_SYM real_ulong_num
          { MDBP_R(1153); }


        ;

reorg_partition_rule:
          REORGANIZE_SYM PARTITION_SYM opt_no_write_to_binlog
          { MDBP_R(1154); }







          reorg_parts_rule { MDBP_R(1155); }
        ;

reorg_parts_rule:
          /* empty */
          { MDBP_R(1156); }


        | alt_part_name_list
          { MDBP_R(1157); }


          INTO '(' part_def_list ')'
          { MDBP_R(1158); }



        ;

alt_part_name_list:
          alt_part_name_item { MDBP_R(1159); }
        | alt_part_name_list ',' alt_part_name_item { MDBP_R(1160); }
        ;

alt_part_name_item:
          ident
          { MDBP_R(1161); }




        ;

/*
  End of management of partition commands
*/

alter_list:
          alter_list_item { MDBP_R(1162); }
        | alter_list ',' alter_list_item { MDBP_R(1163); }
        ;

add_column:
          ADD opt_column opt_if_not_exists_table_element { MDBP_R(1164); }
        ;

alter_list_item:
          add_column column_def opt_place
          { MDBP_R(1165); }





        | ADD key_def
          { MDBP_R(1166); }



        | ADD period_for_system_time
          { MDBP_R(1167); }


        | ADD
          PERIOD_SYM opt_if_not_exists_table_element period_for_application_time
          { MDBP_R(1168); }




        | add_column '(' create_field_list ')'
          { MDBP_R(1169); }





        | ADD constraint_def
          { MDBP_R(1170); }


        | ADD CONSTRAINT IF_SYM not EXISTS field_ident check_constraint
         { MDBP_R(1171); }



        | CHANGE opt_column opt_if_exists_table_element field_ident
          field_spec opt_place
          { MDBP_R(1172); }





        | MODIFY_SYM opt_column opt_if_exists_table_element
          field_spec opt_place
          { MDBP_R(1173); }





        | DROP opt_column opt_if_exists_table_element field_ident opt_restrict
          { MDBP_R(1174); }








	| DROP CONSTRAINT opt_if_exists_table_element field_ident
          { MDBP_R(1175); }









        | DROP FOREIGN KEY_SYM opt_if_exists_table_element field_ident
          { MDBP_R(1176); }








        | DROP opt_constraint_no_id PRIMARY_SYM KEY_SYM
          { MDBP_R(1177); }









        | DROP key_or_index opt_if_exists_table_element field_ident
          { MDBP_R(1178); }








        | DISABLE_SYM KEYS
          { MDBP_R(1179); }




        | ENABLE_SYM KEYS
          { MDBP_R(1180); }




        | ALTER opt_column opt_if_exists_table_element field_ident SET DEFAULT column_default_expr
          { MDBP_R(1181); }





        | ALTER key_or_index opt_if_exists_table_element ident ignorability
          { MDBP_R(1182); }








        | ALTER opt_column opt_if_exists_table_element field_ident DROP DEFAULT
          { MDBP_R(1183); }



        | RENAME opt_to table_ident
          { MDBP_R(1184); }




        | RENAME COLUMN_SYM opt_if_exists_table_element ident TO_SYM ident
          { MDBP_R(1185); }



        | RENAME key_or_index opt_if_exists_table_element field_ident TO_SYM field_ident
          { MDBP_R(1186); }








        | CONVERT_SYM TO_SYM charset charset_name_or_default
          { MDBP_R(1187); }




        | CONVERT_SYM TO_SYM charset charset_name_or_default
                             COLLATE_SYM collation_name_or_default
          { MDBP_R(1188); }




        | create_table_options_space_separated
          { MDBP_R(1189); }



        | FORCE_SYM
          { MDBP_R(1190); }


        | alter_order_clause
          { MDBP_R(1191); }



        | alter_algorithm_option { MDBP_R(1192); }
        | alter_lock_option { MDBP_R(1193); }
        | ADD SYSTEM VERSIONING_SYM
          { MDBP_R(1194); }



        | DROP SYSTEM VERSIONING_SYM
          { MDBP_R(1195); }



        | DROP PERIOD_SYM FOR_SYSTEM_TIME_SYM
          { MDBP_R(1196); }


        | DROP PERIOD_SYM opt_if_exists_table_element FOR_SYM ident
          { MDBP_R(1197); }






        ;

opt_without_validation: { MDBP_R(1198); }
          /* empty */
        | WITH VALIDATION_SYM
        { MDBP_R(1199); }


        | WITHOUT VALIDATION_SYM
        { MDBP_R(1200); }


        ;

opt_index_lock_algorithm: { MDBP_R(1201); }
          /* empty */
        | alter_lock_option { MDBP_R(1202); }
        | alter_algorithm_option { MDBP_R(1203); }
        | alter_lock_option alter_algorithm_option { MDBP_R(1204); }
        | alter_algorithm_option alter_lock_option { MDBP_R(1205); }
        ;

alter_algorithm_option:
          ALGORITHM_SYM opt_equal DEFAULT
          { MDBP_R(1206); }


        | ALGORITHM_SYM opt_equal ident
          { MDBP_R(1207); }



        ;

alter_lock_option:
          LOCK_SYM opt_equal DEFAULT
          { MDBP_R(1208); }



        | LOCK_SYM opt_equal ident
          { MDBP_R(1209); }



        ;

opt_column:
          /* empty */ { MDBP_R(1210); }     %prec PREC_BELOW_IDENTIFIER_OPT_SPECIAL_CASE
        | COLUMN_SYM { MDBP_R(1211); }
        ;

opt_ignore:
          /* empty */ { MDBP_R(1212); }
        | IGNORE_SYM { MDBP_R(1213); }
        ;

alter_options:
        { MDBP_R(1214); } alter_options_part2 { MDBP_R(1215); }
	;
	
alter_options_part2: { MDBP_R(1216); }
          /* empty */ 
        | alter_option_list { MDBP_R(1217); }
        ;

alter_option_list:
        alter_option_list alter_option { MDBP_R(1218); }
        | alter_option { MDBP_R(1219); }
        ;

alter_option:
	  IGNORE_SYM { MDBP_R(1220); }
        | ONLINE_SYM
          { MDBP_R(1221); }



        ;

opt_restrict:
          /* empty */ { MDBP_R(1222); }
        | RESTRICT    { MDBP_R(1223); }
        | CASCADE     { MDBP_R(1224); }
        ;

opt_place:
        /* empty */ { MDBP_R(1225); }
        | AFTER_SYM ident
          { MDBP_R(1226); }



        | FIRST_SYM
          { MDBP_R(1227); }




        ;

opt_to:
          /* empty */ { MDBP_R(1228); }
        | TO_SYM { MDBP_R(1229); }
        | '=' { MDBP_R(1230); }
        | AS { MDBP_R(1231); }
        ;

slave:
          START_SYM SLAVE optional_connection_name slave_thread_opts optional_for_channel
          { MDBP_R(1232); }





          slave_until
          { MDBP_R(1233); }
        | START_SYM ALL SLAVES slave_thread_opts
          { MDBP_R(1234); }





          { MDBP_R(1235); }
        | STOP_SYM SLAVE optional_connection_name slave_thread_opts optional_for_channel
          { MDBP_R(1236); }





        | STOP_SYM ALL SLAVES slave_thread_opts
          { MDBP_R(1237); }





        ;

start:
          START_SYM TRANSACTION_SYM opt_start_transaction_option_list
          { MDBP_R(1238); }











        ;

opt_start_transaction_option_list:
          /* empty */
          { MDBP_R(1239); }


        | start_transaction_option_list
          { MDBP_R(1240); }


        ;

start_transaction_option_list:
          start_transaction_option
          { MDBP_R(1241); }


        | start_transaction_option_list ',' start_transaction_option
          { MDBP_R(1242); }


        ;

start_transaction_option:
          WITH CONSISTENT_SYM SNAPSHOT_SYM
          { MDBP_R(1243); }


        | READ_SYM ONLY_SYM
          { MDBP_R(1244); }


        | READ_SYM WRITE_SYM
          { MDBP_R(1245); }


        ;

slave_thread_opts:
          { MDBP_R(1246); }
          slave_thread_opt_list
          { MDBP_R(1247); }
        ;

slave_thread_opt_list:
          slave_thread_opt { MDBP_R(1248); }
        | slave_thread_opt_list ',' slave_thread_opt { MDBP_R(1249); }
        ;

slave_thread_opt:
          /*empty*/ { MDBP_R(1250); }
        | SQL_THREAD   { MDBP_R(1251); }
        | RELAY_THREAD { MDBP_R(1252); }
        ;

slave_until:
          /*empty*/ { MDBP_R(1253); }
        | UNTIL_SYM slave_until_opts
          { MDBP_R(1254); }







        | UNTIL_SYM MASTER_GTID_POS_SYM '=' TEXT_STRING_sys
          { MDBP_R(1255); }



        | UNTIL_SYM SQL_AFTER_GTIDS_SYM '=' TEXT_STRING_sys
          { MDBP_R(1256); }



        | UNTIL_SYM SQL_BEFORE_GTIDS_SYM '=' TEXT_STRING_sys
          { MDBP_R(1257); }



        ;

slave_until_opts:
          slave_until_file_def { MDBP_R(1258); }
        | slave_until_opts ',' slave_until_file_def { MDBP_R(1259); }
        ;

slave_until_file_def:
    MASTER_LOG_FILE_SYM '=' TEXT_STRING_sys
    { MDBP_R(1260); }


  | MASTER_LOG_POS_SYM '=' ulonglong_num
    { MDBP_R(1261); }













  | RELAY_LOG_FILE_SYM '=' TEXT_STRING_sys
    { MDBP_R(1262); }


  | RELAY_LOG_POS_SYM '=' ulong_num
    { MDBP_R(1263); }



  ;

checksum:
          CHECKSUM_SYM table_or_tables
          { MDBP_R(1264); }





          table_list opt_checksum_type
          { MDBP_R(1265); }
        ;

opt_checksum_type:
          /* nothing */ { MDBP_R(1266); }
        | QUICK         { MDBP_R(1267); }
        | EXTENDED_SYM  { MDBP_R(1268); }
        ;

repair_table_or_view:
          table_or_tables table_list opt_mi_repair_type { MDBP_R(1269); }
        | VIEW_SYM
          { MDBP_R(1270); }
          table_list opt_view_repair_type { MDBP_R(1271); }
        ;

repair:
          REPAIR opt_no_write_to_binlog
          { MDBP_R(1272); }








          repair_table_or_view
          { MDBP_R(1273); }






        ;

opt_mi_repair_type:
          /* empty */ { MDBP_R(1274); }
        | mi_repair_types { MDBP_R(1275); }
        ;

mi_repair_types:
          mi_repair_type { MDBP_R(1276); }
        | mi_repair_type mi_repair_types { MDBP_R(1277); }
        ;

mi_repair_type:
          QUICK        { MDBP_R(1278); }
        | EXTENDED_SYM { MDBP_R(1279); }
        | USE_FRM      { MDBP_R(1280); }
        | FORCE_SYM    { MDBP_R(1281); }
        ;

opt_view_repair_type:
          /* empty */    { MDBP_R(1282); }
        | FOR_SYM UPGRADE_SYM { MDBP_R(1283); }
        | FROM MYSQL_SYM { MDBP_R(1284); }
        ;

analyze:
          ANALYZE_SYM opt_no_write_to_binlog table_or_tables
          { MDBP_R(1285); }








          analyze_table_list
          { MDBP_R(1286); }






        ;

analyze_table_list:
          analyze_table_elem_spec { MDBP_R(1287); }
        | analyze_table_list ',' analyze_table_elem_spec { MDBP_R(1288); }
        ;

analyze_table_elem_spec:
          table_name opt_persistent_stat_clause { MDBP_R(1289); }
        ;

opt_persistent_stat_clause:
          /* empty */
          { MDBP_R(1290); }        
        | PERSISTENT_SYM FOR_SYM persistent_stat_spec  
          { MDBP_R(1291); }


        ;

persistent_stat_spec:
          ALL
          { MDBP_R(1292); }
        | COLUMNS persistent_column_stat_spec INDEXES persistent_index_stat_spec
          { MDBP_R(1293); }
        ;

persistent_column_stat_spec:
          ALL { MDBP_R(1294); }
        | '('
          { MDBP_R(1295); }





          table_column_list
          ')' 
          { MDBP_R(1296); }
        ;
 
persistent_index_stat_spec:
          ALL { MDBP_R(1297); }
        | '('
          { MDBP_R(1298); }





          table_index_list
          ')' 
          { MDBP_R(1299); }
        ;

table_column_list:
          /* empty */
          { MDBP_R(1300); }
        | ident
          { MDBP_R(1301); }



        | table_column_list ',' ident
          { MDBP_R(1302); }



        ;

table_index_list:
          /* empty */
          { MDBP_R(1303); }
        | table_index_name { MDBP_R(1304); } 
        | table_index_list ',' table_index_name { MDBP_R(1305); }
        ;

table_index_name:
          ident
          { MDBP_R(1306); }




        |
          PRIMARY_SYM
          { MDBP_R(1307); }




  
        ;  

binlog_base64_event:
          BINLOG_SYM TEXT_STRING_sys
          { MDBP_R(1308); }





          |
          BINLOG_SYM '@' ident_or_text ',' '@' ident_or_text
          { MDBP_R(1309); }




          ;

check_view_or_table:
          table_or_tables table_list opt_mi_check_type { MDBP_R(1310); }
        | VIEW_SYM
          { MDBP_R(1311); }
          table_list opt_view_check_type { MDBP_R(1312); }
        ;

check:    CHECK_SYM
          { MDBP_R(1313); }








          check_view_or_table
          { MDBP_R(1314); }








        ;

opt_mi_check_type:
          /* empty */ { MDBP_R(1315); }
        | mi_check_types { MDBP_R(1316); }
        ;

mi_check_types:
          mi_check_type { MDBP_R(1317); }
        | mi_check_type mi_check_types { MDBP_R(1318); }
        ;

mi_check_type:
          QUICK               { MDBP_R(1319); }
        | FAST_SYM            { MDBP_R(1320); }
        | MEDIUM_SYM          { MDBP_R(1321); }
        | EXTENDED_SYM        { MDBP_R(1322); }
        | CHANGED             { MDBP_R(1323); }
        | FOR_SYM UPGRADE_SYM { MDBP_R(1324); }
        ;

opt_view_check_type:
          /* empty */         { MDBP_R(1325); }
        | FOR_SYM UPGRADE_SYM { MDBP_R(1326); }
        ;

optimize:
          OPTIMIZE opt_no_write_to_binlog table_or_tables
          { MDBP_R(1327); }








          table_list opt_lock_wait_timeout
          { MDBP_R(1328); }






        ;

opt_no_write_to_binlog:
          /* empty */ { MDBP_R(1329); }
        | NO_WRITE_TO_BINLOG { MDBP_R(1330); }
        | LOCAL_SYM { MDBP_R(1331); }
        ;

rename:
          RENAME table_or_tables opt_if_exists
          { MDBP_R(1332); }





          table_to_table_list
          { MDBP_R(1333); }


        | RENAME USER_SYM clear_privileges rename_list
          { MDBP_R(1334); }


        ;

rename_list:
          user TO_SYM user
          { MDBP_R(1335); }




        | rename_list ',' user TO_SYM user
          { MDBP_R(1336); }




        ;

table_to_table_list:
          table_to_table { MDBP_R(1337); }
        | table_to_table_list ',' table_to_table { MDBP_R(1338); }
        ;

table_to_table:
          table_ident opt_lock_wait_timeout TO_SYM table_ident
          { MDBP_R(1339); }










        ;

keycache:
          CACHE_SYM INDEX_SYM
          { MDBP_R(1340); }


          keycache_list_or_parts IN_SYM key_cache_name
          { MDBP_R(1341); }




        ;

keycache_list_or_parts:
          keycache_list { MDBP_R(1342); }
        | assign_to_keycache_parts { MDBP_R(1343); }
        ;

keycache_list:
          assign_to_keycache { MDBP_R(1344); }
        | keycache_list ',' assign_to_keycache { MDBP_R(1345); }
        ;

assign_to_keycache:
          table_ident cache_keys_spec
          { MDBP_R(1346); }






        ;

assign_to_keycache_parts:
          table_ident adm_partition cache_keys_spec
          { MDBP_R(1347); }






        ;

key_cache_name:
          ident    { MDBP_R(1348); }
        | DEFAULT  { MDBP_R(1349); }
        ;

preload:
          LOAD INDEX_SYM INTO CACHE_SYM
          { MDBP_R(1350); }






          preload_list_or_parts
          { MDBP_R(1351); }


        ;

preload_list_or_parts:
          preload_keys_parts { MDBP_R(1352); }
        | preload_list { MDBP_R(1353); }
        ;

preload_list:
          preload_keys { MDBP_R(1354); }
        | preload_list ',' preload_keys { MDBP_R(1355); }
        ;

preload_keys:
          table_ident cache_keys_spec opt_ignore_leaves
          { MDBP_R(1356); }






        ;

preload_keys_parts:
          table_ident adm_partition cache_keys_spec opt_ignore_leaves
          { MDBP_R(1357); }






        ;

adm_partition:
          PARTITION_SYM have_partitioning
          { MDBP_R(1358); }


          '(' all_or_alt_part_name_list ')' { MDBP_R(1359); }
        ;

cache_keys_spec:
          { MDBP_R(1360); }




          cache_key_list_or_empty { MDBP_R(1361); }
        ;

cache_key_list_or_empty:
          /* empty */ { MDBP_R(1362); }
        | key_or_index '(' opt_key_usage_list ')' { MDBP_R(1363); }
        ;

opt_ignore_leaves:
          /* empty */
          { MDBP_R(1364); }
        | IGNORE_SYM LEAVES { MDBP_R(1365); }
        ;

/*
  Select : retrieve data from table
*/


select_no_with_clause_no_parens:
          query_expression_body_ext
          { MDBP_R(1366); }





          opt_procedure_or_into
          { MDBP_R(1367); }





        ;

select_no_with_clause_parens:
          query_expression_body_ext_parens
          { MDBP_R(1368); }





          opt_procedure_or_into
          { MDBP_R(1369); }





        ;

select_with_with_clause:
          with_clause query_expression_no_with_clause
          { MDBP_R(1370); }





          opt_procedure_or_into
          { MDBP_R(1371); }






        ;


select:
          select_no_with_clause_no_parens { MDBP_R(1372); }
        | select_no_with_clause_parens { MDBP_R(1373); }
        | select_with_with_clause { MDBP_R(1374); }
        ;


select_for_open_cursor:
          select_no_with_clause_no_parens { MDBP_R(1375); }
        | select_with_with_clause { MDBP_R(1376); }
        ;


select_into:
          select_into_query_specification
          { MDBP_R(1377); }



          opt_order_limit_lock
          { MDBP_R(1378); }








        | with_clause
          select_into_query_specification
          { MDBP_R(1379); }



          opt_order_limit_lock
          { MDBP_R(1380); }










        ;

simple_table:
          query_specification      { MDBP_R(1381); }
        | table_value_constructor  { MDBP_R(1382); }
        ;

table_value_constructor:
	  VALUES
	  { MDBP_R(1383); }



	  values_list
	  { MDBP_R(1384); }



	;

opt_hint_comment:
          /*empty */   { MDBP_R(1385); }
        | HINT_COMMENT { MDBP_R(1386); }
        ;

opt_optimizer_hint:
          { MDBP_R(1387); }
          opt_hint_comment
          { MDBP_R(1388); }






        ;

query_specification_start:
          SELECT_SYM opt_optimizer_hint
          { MDBP_R(1389); }








          select_options
          { MDBP_R(1390);
            Select->parsing_place= SELECT_LIST;
}
          select_item_list
          { MDBP_R(1391);
            Select->parsing_place= NO_MATTER;
}
          ;

query_specification:
          query_specification_start
          opt_from_clause
          opt_where_clause
          opt_group_clause
          opt_having_clause
          opt_window_clause
          { MDBP_R(1392); }



        ;

select_into_query_specification:
          query_specification_start
          into
          opt_from_clause
          opt_where_clause
          opt_group_clause
          opt_having_clause
          opt_window_clause
          { MDBP_R(1393); }



        ;

/**

  The following grammar for query expressions conformant to
  the latest SQL Standard is supported:

    <query expression> ::=
     [ <with clause> ] <query expression body>
       [ <order by clause> ] [ <result offset clause> ] [ <fetch first clause> ]

   <with clause> ::=
     WITH [ RECURSIVE ] <with_list

   <with list> ::=
     <with list element> [ { <comma> <with list element> }... ]

   <with list element> ::=
     <query name> [ '(' <with column list> ')' ]
         AS <table subquery>

   <with column list> ::=
     <column name list>

   <query expression body> ::
       <query term>
     | <query expression body> UNION [ ALL | DISTINCT ] <query term>
     | <query expression body> EXCEPT [ DISTINCT ] <query term>

   <query term> ::=
       <query primary>
     | <query term> INTERSECT [ DISTINCT ] <query primary>

   <query primary> ::=
       <simple table>
     | '(' <query expression body>
       [ <order by clause> ] [ <result offset clause> ] [ <fetch first clause> ]
       ')'

   <simple table>
       <query specification>
     | <table value constructor>

  <subquery>
       '(' <query_expression> ')'

*/

/*
  query_expression produces the same expressions as
      <query expression>
*/

query_expression:
          query_expression_no_with_clause
          { MDBP_R(1394); }



        | with_clause
          query_expression_no_with_clause
          { MDBP_R(1395); }




        ;

/*
   query_expression_no_with_clause produces the same expressions as
       <query expression> without [ <with clause> ]
*/

query_expression_no_with_clause:
          query_expression_body_ext { MDBP_R(1396); }
        | query_expression_body_ext_parens { MDBP_R(1397); }
        ;

/*
  query_expression_body_ext produces the same expressions as
      <query expression body>
       [ <order by clause> ] [ <result offset clause> ] [ <fetch first clause> ]
    | '('... <query expression body>
       [ <order by clause> ] [ <result offset clause> ] [ <fetch first clause> ]
      ')'...
  Note: number of ')' must be equal to the number of '(' in the rule above
*/

query_expression_body_ext:
          query_expression_body
          { MDBP_R(1398); }






          opt_query_expression_tail
          { MDBP_R(1399); }





        | query_expression_body_ext_parens
          { MDBP_R(1400); }



          query_expression_tail
          { MDBP_R(1401); }



        ;

query_expression_body_ext_parens:
          '(' query_expression_body_ext_parens ')'
          { MDBP_R(1402); }
        | '(' query_expression_body_ext ')'
          { MDBP_R(1403); }





        ;

/*
  query_expression_body produces the same expressions as
      <query expression body>
*/

query_expression_body:
          query_simple
          { MDBP_R(1404); }




        | query_expression_body
          unit_type_decl
          { MDBP_R(1405); }





          query_primary
          { MDBP_R(1406); }





        | query_expression_body_ext_parens
          unit_type_decl
          query_primary
          { MDBP_R(1407); }






        ;

/*
  query_primary produces the same expressions as
      <query primary>
*/

query_primary:
          query_simple
          { MDBP_R(1408); }
        | query_expression_body_ext_parens
          { MDBP_R(1409); }
        ;

/*
  query_simple produces the same expressions as
      <simple table>
*/

query_simple:
          simple_table { MDBP_R(1410); }
        ;

subselect:
          query_expression
          { MDBP_R(1411); }



        ;

/*
  subquery produces the same expressions as
     <subquery>

  Consider the production rule of the SQL Standard
     subquery:
        '(' query_expression ')'

  This rule is equivalent to the rule
     subquery:
          '(' query_expression_no_with_clause ')'
        | '(' with_clause query_expression_no_with_clause ')'
  that in its turn is equivalent to
     subquery:
          '(' query_expression_body_ext ')'
        | query_expression_body_ext_parens
        | '(' with_clause query_expression_no_with_clause ')'

  The latter can be re-written into
     subquery:
          query_expression_body_ext_parens
        | '(' with_clause query_expression_no_with_clause ')'

  The last rule allows us to resolve properly the shift/reduce conflict
  when subquery is used in expressions such as in the following queries
     select (select * from t1 limit 1) + t2.a from t2
     select * from t1 where t1.a [not] in (select t2.a from t2)

  In the rule below %prec SUBQUERY_AS_EXPR forces the parser to perform a shift
  operation rather then a reduce operation when ')' is encountered and can be
  considered as the last symbol a query expression.
*/

subquery:
          query_expression_body_ext_parens %prec SUBQUERY_AS_EXPR
          { MDBP_R(1412); }







        | '(' with_clause query_expression_no_with_clause ')'
          { MDBP_R(1413); }





        ;

opt_from_clause:
        /* empty */ %prec EMPTY_FROM_CLAUSE { MDBP_R(1414); }
        | from_clause { MDBP_R(1415); }
        ;

from_clause:
          FROM table_reference_list { MDBP_R(1416); }
        ;

table_reference_list:
          join_table_list
          { MDBP_R(1417); }




        | DUAL_SYM { MDBP_R(1418); }
          /* oracle compatibility: oracle always requires FROM clause,
             and DUAL is system table without fields.
             Is "SELECT 1 FROM DUAL" any better than "SELECT 1" ?
          Hmmm :) */
        ;

select_options: { MDBP_R(1419); }
          /* empty*/
        | select_option_list
          { MDBP_R(1420); }




        ;

opt_history_unit:
          /* empty*/         %prec PREC_BELOW_IDENTIFIER_OPT_SPECIAL_CASE
          { MDBP_R(1421); }


        | TRANSACTION_SYM
          { MDBP_R(1422); }


        | TIMESTAMP
          { MDBP_R(1423); }


        ;

history_point:
          TIMESTAMP TEXT_STRING
          { MDBP_R(1424); }







        | function_call_keyword_timestamp
          { MDBP_R(1425); }


        | opt_history_unit bit_expr
          { MDBP_R(1426); }


        ;

for_portion_of_time_clause:
          FOR_SYM PORTION_SYM OF_SYM remember_tok_start ident FROM
          bit_expr TO_SYM bit_expr
          { MDBP_R(1427); }










        ;

opt_for_portion_of_time_clause:
          /* empty */
          { MDBP_R(1428); }


        | for_portion_of_time_clause
          { MDBP_R(1429); }


        ;

opt_for_system_time_clause:
          /* empty */
          { MDBP_R(1430); }


        | FOR_SYSTEM_TIME_SYM system_time_expr
          { MDBP_R(1431); }


        ;

system_time_expr:
          AS OF_SYM history_point
          { MDBP_R(1432); }


        | ALL
          { MDBP_R(1433); }


        | FROM history_point TO_SYM history_point
          { MDBP_R(1434); }


        | BETWEEN_SYM history_point AND_SYM history_point
          { MDBP_R(1435); }


        ;

select_option_list:
          select_option_list select_option { MDBP_R(1436); }
        | select_option { MDBP_R(1437); }
        ;

select_option:
          query_expression_option { MDBP_R(1438); }
        | SQL_NO_CACHE_SYM
          { MDBP_R(1439); }







        | SQL_CACHE_SYM
          { MDBP_R(1440); }







        ;


select_lock_type:
          FOR_SYM UPDATE_SYM opt_lock_wait_timeout_new
          { MDBP_R(1441); }




        | LOCK_SYM IN_SYM SHARE_SYM MODE_SYM opt_lock_wait_timeout_new
          { MDBP_R(1442); }




        ;


opt_select_lock_type:
        /* empty */
        { MDBP_R(1443); }


        | select_lock_type
        { MDBP_R(1444); }


        ;

opt_lock_wait_timeout_new:
        /* empty */
        { MDBP_R(1445); }


        | WAIT_SYM ulong_num
        { MDBP_R(1446); }




        | NOWAIT_SYM
        { MDBP_R(1447); }




        | SKIP_SYM LOCKED_SYM
        { MDBP_R(1448); }




      ;

select_item_list:
          select_item_list ',' select_item { MDBP_R(1449); }
        | select_item { MDBP_R(1450); }
        | '*'
          { MDBP_R(1451); }


















        ;

select_item:
          remember_name select_sublist_qualified_asterisk remember_end
          { MDBP_R(1452); }



        | remember_name expr remember_end select_alias
          { MDBP_R(1453); }

















        ;

remember_tok_start:
          { MDBP_R(1454); }


        ;

remember_name:
          { MDBP_R(1455); }


        ;

remember_end:
          { MDBP_R(1456); }


        ;

remember_cpp_ptr:
          { MDBP_R(1457); }


        ;

remember_start_opt:
          { MDBP_R(1458); }





        ;

remember_end_opt:
          { MDBP_R(1459); }





        ;

remember_lex:
          { MDBP_R(1460); }


        ;

select_alias:
          /* empty */ { MDBP_R(1461); }
        | AS ident { MDBP_R(1462); }
        | AS TEXT_STRING_sys { MDBP_R(1463); }
        | ident { MDBP_R(1464); }
        | TEXT_STRING_sys { MDBP_R(1465); }
        ;

opt_default_time_precision:
          /* empty */             { MDBP_R(1466); }
        | '(' ')'                 { MDBP_R(1467); }
        | '(' real_ulong_num ')'  { MDBP_R(1468); }
        ;

opt_time_precision:
          /* empty */             { MDBP_R(1469); }
        | '(' ')'                 { MDBP_R(1470); }
        | '(' real_ulong_num ')'  { MDBP_R(1471); }
        ;

optional_braces:
          /* empty */ { MDBP_R(1472); }
        | '(' ')' { MDBP_R(1473); }
        ;

search_condition:
          expr { MDBP_R(1474); }
        ;

/* all possible expressions */
expr:
          expr or expr %prec OR_SYM
          { MDBP_R(1475); }

















































        | expr XOR expr %prec XOR
          { MDBP_R(1476); }





        | expr and expr %prec AND_SYM
          { MDBP_R(1477); }









































        | NOT_SYM expr
          { MDBP_R(1478); }




        | boolean_test %prec PREC_BELOW_NOT { MDBP_R(1479); }
        | OLD_VALUE_SYM '(' ident ')'
          { MDBP_R(1480); }















        ;

boolean_test:
          boolean_test IS TRUE_SYM %prec IS
          { MDBP_R(1481); }




        | boolean_test IS not TRUE_SYM %prec IS
          { MDBP_R(1482); }




        | boolean_test IS FALSE_SYM %prec IS
          { MDBP_R(1483); }




        | boolean_test IS not FALSE_SYM %prec IS
          { MDBP_R(1484); }




        | boolean_test IS UNKNOWN_SYM %prec IS
          { MDBP_R(1485); }




        | boolean_test IS not UNKNOWN_SYM %prec IS
          { MDBP_R(1486); }




        | boolean_test IS NULL_SYM %prec IS
          { MDBP_R(1487); }




        | boolean_test IS not NULL_SYM %prec IS
          { MDBP_R(1488); }




        | boolean_test IS json_predicate %prec IS
          { MDBP_R(1489); }







        | boolean_test IS not json_predicate %prec IS
          { MDBP_R(1490); }







        | boolean_test EQUAL_SYM predicate %prec EQUAL_SYM
          { MDBP_R(1491); }




        | boolean_test comp_op predicate %prec '='
          { MDBP_R(1492); }




        | boolean_test comp_op all_or_any '(' subselect ')' %prec '='
          { MDBP_R(1493); }




        | predicate %prec BETWEEN_SYM { MDBP_R(1494); }
        ;

json_predicate:
          JSON_SYM json_type_constraint json_key_unique_constraint
          { MDBP_R(1495); }



        ;

json_type_constraint:
          ARRAY_SYM        { MDBP_R(1496); }
        | OBJECT_SYM       { MDBP_R(1497); }
        | SCALAR_SYM       { MDBP_R(1498); }
        | VALUE_SYM        { MDBP_R(1499); }
        | /* empty */      { MDBP_R(1500); }
        ;

json_key_unique_constraint:
          WITH UNIQUE_SYM              { MDBP_R(1501); }
        | WITH UNIQUE_SYM KEYS         { MDBP_R(1502); }
        | WITHOUT UNIQUE_SYM           { MDBP_R(1503); }
        | WITHOUT UNIQUE_SYM KEYS      { MDBP_R(1504); }
        | /* empty */                  { MDBP_R(1505); }

predicate:
          predicate IN_SYM subquery
          { MDBP_R(1506); }




        | predicate not IN_SYM subquery
          { MDBP_R(1507); }







        | predicate IN_SYM '(' expr ')'
          { MDBP_R(1508); }




        | predicate IN_SYM '(' expr ',' expr_list ')'
          { MDBP_R(1509); }






        | predicate not IN_SYM '(' expr ')'
          { MDBP_R(1510); }




        | predicate not IN_SYM '(' expr ',' expr_list ')'
          { MDBP_R(1511); }







        | predicate BETWEEN_SYM predicate AND_SYM predicate %prec BETWEEN_SYM
          { MDBP_R(1512); }




        | predicate not BETWEEN_SYM predicate AND_SYM predicate %prec BETWEEN_SYM
          { MDBP_R(1513); }






        | predicate SOUNDS_SYM LIKE predicate
          { MDBP_R(1514); }








        | predicate LIKE predicate
          { MDBP_R(1515); }




        | predicate LIKE predicate ESCAPE_SYM predicate %prec LIKE
          { MDBP_R(1516); }





        | predicate not LIKE predicate
          { MDBP_R(1517); }





        | predicate not LIKE predicate ESCAPE_SYM predicate %prec LIKE
          { MDBP_R(1518); }






        | predicate REGEXP predicate
          { MDBP_R(1519); }




        | predicate not REGEXP predicate
          { MDBP_R(1520); }







        | bit_expr %prec PREC_BELOW_NOT { MDBP_R(1521); }
        ;

bit_expr:
          bit_expr '|' bit_expr %prec '|'
          { MDBP_R(1522); }




        | bit_expr '&' bit_expr %prec '&'
          { MDBP_R(1523); }




        | bit_expr SHIFT_LEFT bit_expr %prec SHIFT_LEFT
          { MDBP_R(1524); }




        | bit_expr SHIFT_RIGHT bit_expr %prec SHIFT_RIGHT
          { MDBP_R(1525); }




        | bit_expr ORACLE_CONCAT_SYM bit_expr
          { MDBP_R(1526); }





        | bit_expr '+' bit_expr %prec '+'
          { MDBP_R(1527); }




        | bit_expr '-' bit_expr %prec '-'
          { MDBP_R(1528); }




        | bit_expr '+' INTERVAL_SYM expr interval %prec '+'
          { MDBP_R(1529); }




        | bit_expr '-' INTERVAL_SYM expr interval %prec '-'
          { MDBP_R(1530); }




        | INTERVAL_SYM expr interval '+' expr
          /* we cannot put interval before - */
          { MDBP_R(1531); }




        | '+' INTERVAL_SYM expr interval '+' expr %prec NEG
          { MDBP_R(1532); }




        | '-' INTERVAL_SYM expr interval '+' expr %prec NEG
          { MDBP_R(1533); }




        | bit_expr '*' bit_expr %prec '*'
          { MDBP_R(1534); }




        | bit_expr '/' bit_expr %prec '/'
          { MDBP_R(1535); }




        | bit_expr '%' bit_expr %prec '%'
          { MDBP_R(1536); }




        | bit_expr DIV_SYM bit_expr %prec DIV_SYM
          { MDBP_R(1537); }




        | bit_expr MOD_SYM bit_expr %prec MOD_SYM
          { MDBP_R(1538); }




        | bit_expr '^' bit_expr
          { MDBP_R(1539); }




        | mysql_concatenation_expr %prec '^' { MDBP_R(1540); }
        ;

or:
          OR_SYM { MDBP_R(1541); }
       | OR2_SYM { MDBP_R(1542); }
       ;

and:
          AND_SYM { MDBP_R(1543); }
       | AND_AND_SYM { MDBP_R(1544); }
       ;

not:
          NOT_SYM { MDBP_R(1545); }
        | NOT2_SYM { MDBP_R(1546); }
        ;

not2:
          '!' { MDBP_R(1547); }
        | NOT2_SYM { MDBP_R(1548); }
        ;

comp_op:
          '='     { MDBP_R(1549); }
        | GE     { MDBP_R(1550); }
        | '>' { MDBP_R(1551); }
        | LE     { MDBP_R(1552); }
        | '<'     { MDBP_R(1553); }
        | NE     { MDBP_R(1554); }
        ;

all_or_any:
          ALL     { MDBP_R(1555); }
        | ANY_SYM { MDBP_R(1556); }
        ;

opt_dyncol_type:
          /* empty */ 
          { MDBP_R(1557); }


        | AS dyncol_type { MDBP_R(1558); }
        ;

dyncol_type:
          numeric_dyncol_type { MDBP_R(1559); }
        | temporal_dyncol_type { MDBP_R(1560); }
        | string_dyncol_type { MDBP_R(1561); }
        ;

numeric_dyncol_type:
          INT_SYM                         { MDBP_R(1562); }
        | UNSIGNED INT_SYM                { MDBP_R(1563); }
        | DOUBLE_SYM                      { MDBP_R(1564); }
        | REAL                            { MDBP_R(1565); }
        | FLOAT_SYM                       { MDBP_R(1566); }
        | DECIMAL_SYM float_options       { MDBP_R(1567); }
        ;

temporal_dyncol_type:
          DATE_SYM                        { MDBP_R(1568); }
        | TIME_SYM opt_field_scale        { MDBP_R(1569); }
        | DATETIME opt_field_scale        { MDBP_R(1570); }
        ;

string_dyncol_type:
          char opt_binary
          { MDBP_R(1571); }





        | nchar
          { MDBP_R(1572); }


        ;

dyncall_create_element:
   expr ',' expr opt_dyncol_type
   { MDBP_R(1573); }

















   ;

dyncall_create_list:
     dyncall_create_element
       { MDBP_R(1574); }





   | dyncall_create_list ',' dyncall_create_element
       { MDBP_R(1575); }



   ;


plsql_cursor_attr:
          ISOPEN_SYM    { MDBP_R(1576); }
        | FOUND_SYM     { MDBP_R(1577); }
        | NOTFOUND_SYM  { MDBP_R(1578); }
        | ROWCOUNT_SYM  { MDBP_R(1579); }
        ;

explicit_cursor_attr:
          ident PERCENT_ORACLE_SYM plsql_cursor_attr
          { MDBP_R(1580); }



        ;


trim_operands:
          trim_operands_regular { MDBP_R(1581); }
        | trim_operands_special { MDBP_R(1582); }
        ;

trim_operands_regular:
          expr                     { MDBP_R(1583); }
        ;

trim_operands_special:
          LEADING  expr FROM expr  { MDBP_R(1584); }
        | TRAILING expr FROM expr  { MDBP_R(1585); }
        | BOTH     expr FROM expr  { MDBP_R(1586); }
        | LEADING       FROM expr  { MDBP_R(1587); }
        | TRAILING      FROM expr  { MDBP_R(1588); }
        | BOTH          FROM expr  { MDBP_R(1589); }
        | expr          FROM expr  { MDBP_R(1590); }
        ;

/*
  Expressions that the parser allows in a column DEFAULT clause
  without parentheses. These expressions cannot end with a COLLATE clause.

  If we allowed any "expr" in DEFAULT clause, there would be a confusion
  in queries like this:
    CREATE TABLE t1 (a TEXT DEFAULT 'a' COLLATE latin1_bin);
  It would be not clear what COLLATE stands for:
  - the collation of the column `a`, or
  - the collation of the string literal 'a'

  This restriction allows to parse the above query unambiguously:
  COLLATE belongs to the column rather than the literal.
  If one needs COLLATE to belong to the literal, parentheses must be used:
    CREATE TABLE t1 (a TEXT DEFAULT ('a' COLLATE latin1_bin));
  Note: the COLLATE clause is rather meaningless here, but the query
  is syntactically correct.

  Note, some of the expressions are not actually allowed in DEFAULT,
  e.g. sum_expr, window_func_expr, ROW(...), VALUES().
  We could move them to simple_expr, but that would make
  these two queries return a different error messages:
    CREATE TABLE t1 (a INT DEFAULT AVG(1));
    CREATE TABLE t1 (a INT DEFAULT (AVG(1)));
  The first query would return "syntax error".
  Currenly both return:
   Function or expression 'avg(' is not allowed for 'DEFAULT' ...
*/
column_default_non_parenthesized_expr:
          simple_ident { MDBP_R(1591); }
        | function_call_keyword { MDBP_R(1592); }
        | function_call_nonkeyword { MDBP_R(1593); }
        | function_call_generic { MDBP_R(1594); }
        | function_call_conflict { MDBP_R(1595); }
        | literal { MDBP_R(1596); }
        | param_marker { MDBP_R(1597); }
        | variable { MDBP_R(1598); }
        | sum_expr
          { MDBP_R(1599); }






        | window_func_expr
          { MDBP_R(1600); }






        | inverse_distribution_function { MDBP_R(1601); }
        | ROW_SYM '(' expr ',' expr_list ')'
          { MDBP_R(1602); }





        | EXISTS '(' subselect ')'
          { MDBP_R(1603); }




        | '{' ident expr '}'
          { MDBP_R(1604); }



        | MATCH ident_list_arg AGAINST '(' bit_expr fulltext_options ')'
          { MDBP_R(1605); }








        | CAST_SYM '(' expr AS cast_type ')'
          { MDBP_R(1606); }



        | CASE_SYM when_list_opt_else END
          { MDBP_R(1607); }



        | CASE_SYM expr when_list_opt_else END
          { MDBP_R(1608); }




        | CONVERT_SYM '(' expr ',' cast_type ')'
          { MDBP_R(1609); }



        | CONVERT_SYM '(' expr USING charset_name ')'
          { MDBP_R(1610); }






        | DEFAULT '(' simple_ident ')'
          { MDBP_R(1611); }









        | VALUE_SYM '(' simple_ident_nospvar ')'
          { MDBP_R(1612); }





        | NEXT_SYM VALUE_SYM FOR_SYM table_ident
          { MDBP_R(1613); }



        | NEXTVAL_SYM '(' table_ident ')'
          { MDBP_R(1614); }



        | PREVIOUS_SYM VALUE_SYM FOR_SYM table_ident
          { MDBP_R(1615); }



        | LASTVAL_SYM '(' table_ident ')'
          { MDBP_R(1616); }



        | SETVAL_SYM '(' table_ident ',' sequence_value_hybrid_num ')'
          { MDBP_R(1617); }






        | SETVAL_SYM '(' table_ident ',' sequence_value_hybrid_num ',' bool ')'
          { MDBP_R(1618); }






        | SETVAL_SYM '(' table_ident ',' sequence_value_hybrid_num ',' bool ','
          ulonglong_num ')'
          { MDBP_R(1619); }






        | TO_DATE '(' expr ',' expr opt_nls_param')'
        { MDBP_R(1620); }




        | TO_DATE '(' expr DEFAULT expr ON CONVERSION_SYM ERROR_SYM
           ',' expr opt_nls_param')'
        { MDBP_R(1621); }




        ;

primary_base_expr:
          column_default_non_parenthesized_expr { MDBP_R(1622); }
        | explicit_cursor_attr { MDBP_R(1623); }
        | '(' parenthesized_expr ')' { MDBP_R(1624); }
        | subquery
          { MDBP_R(1625); }



        ;

primary_expr:
          primary_base_expr { MDBP_R(1626); }
        | primary_base_expr JSON_SEPARATOR_SYM TEXT_STRING
          { MDBP_R(1627); }













        | primary_base_expr JSON_UNQUOTED_SEPARATOR_SYM TEXT_STRING
          { MDBP_R(1628); }
















        ;
        ;

string_factor_expr:
          primary_expr { MDBP_R(1629); }
        | string_factor_expr COLLATE_SYM collation_name
          { MDBP_R(1630); }




        ;

simple_expr:
          string_factor_expr %prec NEG { MDBP_R(1631); }
        | BINARY simple_expr
          { MDBP_R(1632); }




        | '+' simple_expr %prec NEG
          { MDBP_R(1633); }


        | '-' simple_expr %prec NEG
          { MDBP_R(1634); }




        | '~' simple_expr %prec NEG
          { MDBP_R(1635); }




        | not2 simple_expr %prec NEG
          { MDBP_R(1636); }




        ;

mysql_concatenation_expr:
          simple_expr { MDBP_R(1637); }
        | mysql_concatenation_expr MYSQL_CONCAT_SYM simple_expr
          { MDBP_R(1638); }




        ;

function_call_keyword_timestamp:
          TIMESTAMP '(' expr ')'
          { MDBP_R(1639); }





        | TIMESTAMP '(' expr ',' expr ')'
          { MDBP_R(1640); }




        ;

opt_nls_param:
          /* empty */
          { MDBP_R(1641); }


          | ',' TEXT_STRING_sys
          { MDBP_R(1642); }




/*
  Function call syntax using official SQL 2003 keywords.
  Because the function name is an official token,
  a dedicated grammar rule is needed in the parser.
  There is no potential for conflicts
*/
function_call_keyword:
          CHAR_SYM '(' expr_list ')'
          { MDBP_R(1643); }




        | CHAR_SYM '(' expr_list USING charset_name ')'
          { MDBP_R(1644); }






        | CURRENT_PATH optional_braces
          { MDBP_R(1645); }






        | CURRENT_USER optional_braces
          { MDBP_R(1646); }







        | CURRENT_ROLE optional_braces
          { MDBP_R(1647); }







        | DATE_SYM '(' expr ')'
          { MDBP_R(1648); }





        | DAY_SYM '(' expr ')'
          { MDBP_R(1649); }




        | HOUR_SYM '(' expr ')'
          { MDBP_R(1650); }




        | INSERT '(' expr ',' expr ',' expr ',' expr ')'
          { MDBP_R(1651); }




        | INTERVAL_SYM '(' expr ',' expr ')'
          { MDBP_R(1652); }













        | INTERVAL_SYM '(' expr ',' expr ',' expr_list ')'
          { MDBP_R(1653); }









        | LEFT '(' expr ',' expr ')'
          { MDBP_R(1654); }




        | MINUTE_SYM '(' expr ')'
          { MDBP_R(1655); }




        | MONTH_SYM '(' expr ')'
          { MDBP_R(1656); }





        | RIGHT '(' expr ',' expr ')'
          { MDBP_R(1657); }




        | SECOND_SYM '(' expr ')'
          { MDBP_R(1658); }




        | SQL_SYM PERCENT_ORACLE_SYM ROWCOUNT_SYM
          { MDBP_R(1659); }






        | SESSION_USER_SYM '(' ')'
          { MDBP_R(1660); }






        | TIME_SYM '(' expr ')'
          { MDBP_R(1661); }





        | function_call_keyword_timestamp
          { MDBP_R(1662); }


        | TRIM '(' trim_operands ')'
          { MDBP_R(1663); }




        | USER_SYM '(' ')'
          { MDBP_R(1664); }






        | YEAR_SYM '(' expr ')'
          { MDBP_R(1665); }





        ;

substring_operands:
          substring_operands_regular { MDBP_R(1666); }
        | substring_operands_special { MDBP_R(1667); }
        ;

substring_operands_regular:
          expr ',' expr ',' expr
          { MDBP_R(1668); }


        | expr ',' expr
          { MDBP_R(1669); }


        ;

substring_operands_special:
          expr FROM expr FOR_SYM expr
          { MDBP_R(1670); }


        | expr FROM expr
          { MDBP_R(1671); }


        ;


/*
  Function calls using non reserved keywords, with special syntaxic forms.
  Dedicated grammar rules are needed because of the syntax,
  but also have the potential to cause incompatibilities with other
  parts of the language.
  MAINTAINER:
  The only reasons a function should be added here are:
  - for compatibility reasons with another SQL syntax (CURDATE),
  - for typing reasons (GET_FORMAT)
  Any other 'Syntaxic sugar' enhancements should be *STRONGLY*
  discouraged.
*/
function_call_nonkeyword:
        ADDDATE_SYM '(' expr ',' expr ')'
          { MDBP_R(1672); }





        | ADDDATE_SYM '(' expr ',' INTERVAL_SYM expr interval ')'
          { MDBP_R(1673); }




        | CURDATE optional_braces
          { MDBP_R(1674); }





        | CURTIME opt_time_precision
          { MDBP_R(1675); }





        | DATE_ADD_INTERVAL '(' expr ',' INTERVAL_SYM expr interval ')'
          { MDBP_R(1676); }




        | DATE_SUB_INTERVAL '(' expr ',' INTERVAL_SYM expr interval ')'
          { MDBP_R(1677); }




        | EXTRACT_SYM '(' interval FROM expr ')'
          { MDBP_R(1678); }




        | GET_FORMAT '(' date_time_type  ',' expr ')'
          { MDBP_R(1679); }




        | LOCALTIMESTAMP opt_time_precision
          { MDBP_R(1680); }





        | NOW_SYM opt_time_precision
          { MDBP_R(1681); }





        | POSITION_SYM '(' bit_expr IN_SYM expr ')'
          { MDBP_R(1682); }




         | ROWNUM_SYM

           '(' ')'



          { MDBP_R(1683); }




        | SUBDATE_SYM '(' expr ',' expr ')'
          { MDBP_R(1684); }





        | SUBDATE_SYM '(' expr ',' INTERVAL_SYM expr interval ')'
          { MDBP_R(1685); }




        | SUBSTRING '(' substring_operands ')'
          { MDBP_R(1686); }











        | SYSDATE '(' ')'
          { MDBP_R(1687); }



        | SYSDATE '(' real_ulong_num ')'
          { MDBP_R(1688); }



        | TIMESTAMP_ADD '(' interval_time_stamp ',' expr ',' expr ')'
          { MDBP_R(1689); }




        | TIMESTAMP_DIFF '(' interval_time_stamp ',' expr ',' expr ')'
          { MDBP_R(1690); }




        | TRIM_ORACLE '(' trim_operands ')'
          { MDBP_R(1691); }



        | UTC_DATE_SYM optional_braces
          { MDBP_R(1692); }





        | UTC_TIME_SYM opt_time_precision
          { MDBP_R(1693); }





        | UTC_TIMESTAMP_SYM opt_time_precision
          { MDBP_R(1694); }





        |
          COLUMN_ADD_SYM '(' expr ',' dyncall_create_list ')'
          { MDBP_R(1695); }




        |
          COLUMN_DELETE_SYM '(' expr ',' expr_list ')'
          { MDBP_R(1696); }




        |
          COLUMN_CREATE_SYM '(' dyncall_create_list ')'
          { MDBP_R(1697); }




        |
          COLUMN_GET_SYM '(' expr ',' expr AS cast_type ')'
          { MDBP_R(1698); }





        ;

/*
  Functions calls using a non reserved keyword, and using a regular syntax.
  Because the non reserved keyword is used in another part of the grammar,
  a dedicated rule is needed here.
*/
function_call_conflict:
          ASCII_SYM '(' expr ')'
          { MDBP_R(1699); }




        | CHARSET '(' expr ')'
          { MDBP_R(1700); }




        | IF_SYM '(' expr ',' expr ',' expr ')'
          { MDBP_R(1701); }




          /* LAST_VALUE here conflicts with the definition for window functions.
             We have these 2 separate rules to remove the shift/reduce conflict.
          */
        | LAST_VALUE '(' expr ')'
          { MDBP_R(1702); }









        | LAST_VALUE '(' expr_list ',' expr ')'
          { MDBP_R(1703); }





        | MOD_SYM '(' expr ',' expr ')'
          { MDBP_R(1704); }




        | PASSWORD_SYM '(' expr ')'
          { MDBP_R(1705); }






        | REPEAT_SYM '(' expr ',' expr ')'
          { MDBP_R(1706); }




        | REPLACE '(' expr ',' expr ',' expr ')'
          { MDBP_R(1707); }




        | TRUNCATE_SYM '(' expr ',' expr ')'
          { MDBP_R(1708); }




        | WEIGHT_STRING_SYM '(' expr opt_ws_levels ')'
          { MDBP_R(1709); }




        | WEIGHT_STRING_SYM '(' expr AS CHAR_SYM ws_nweights opt_ws_levels ')'
          { MDBP_R(1710); }






        | WEIGHT_STRING_SYM '(' expr AS BINARY ws_nweights ')'
          { MDBP_R(1711); }










        | WEIGHT_STRING_SYM '(' expr ',' ulong_num ',' ulong_num ',' ulong_num ')'
          { MDBP_R(1712); }





        ;

/*
  Regular function calls.
  The function name is *not* a token, and therefore is guaranteed to not
  introduce side effects to the language in general.
  MAINTAINER:
  All the new functions implemented for new features should fit into
  this category. The place to implement the function itself is
  in sql/item_create.cc
*/
function_call_generic:
          ident_cli_func '('
          { $$ = 0; MDBP_R(1713); }
























          opt_udf_expr_list_or_join_operator ')' opt_object_member_access
          { MDBP_R(1714); }


































































































        | CONTAINS_SYM '(' opt_expr_list ')'
          { MDBP_R(1715); }




        | OVERLAPS_SYM '(' opt_expr_list ')'
          { MDBP_R(1716); }




        | WITHIN '(' opt_expr_list ')'
          { MDBP_R(1717); }




        | ident_cli '.' ident_cli '(' opt_expr_list_or_join_operator ')'
          { MDBP_R(1718); }






        | '.' ident_cli '.' ident_cli '(' '+' ')'
          { MDBP_R(1719); }












        | ident_cli '.' ident_cli '.' ident_cli '(' opt_expr_list_or_join_operator ')'
          { MDBP_R(1720); }



        | ident_cli '.' REPLACE '(' opt_expr_list ')'
          { MDBP_R(1721); }



        | ident_cli '.' SUBSTRING '(' opt_expr_list ')'
          { MDBP_R(1722); }



        | ident_cli '.' SUBSTRING '(' substring_operands_special ')'
          { MDBP_R(1723); }



        | ident_cli '.' TRIM '(' opt_expr_list ')'
          { MDBP_R(1724); }



        | ident_cli '.' TRIM '(' trim_operands_special ')'
          { MDBP_R(1725); }



          /*
            We don't add a qualified syntax for TRIM_ORACLE here,
            as this syntax is not absolutely required:
              SELECT mariadb_schema.TRIM_ORACLE(..);
            What absolutely required is only:
              SELECT mariadb_schema.TRIM(..);
            Adding a qualified syntax for TRIM_ORACLE would be tricky because
            it is a non-reserved keyword. To avoid new shift/reduce conflicts
            it would require grammar changes, like introducing a new rule
            ident_step2_cli (which would include everything that ident_cli
            includes but TRIM_ORACLE).
          */
        ;

opt_object_member_access:
          /* empty */    { MDBP_R(1726); }
        | '.' ident_cli  { MDBP_R(1727); }
        ;

fulltext_options:
          opt_natural_language_mode opt_query_expansion
          { MDBP_R(1728); }
        | IN_SYM BOOLEAN_SYM MODE_SYM
          { MDBP_R(1729); }
        ;

opt_natural_language_mode:
          /* nothing */                         { MDBP_R(1730); }
        | IN_SYM NATURAL LANGUAGE_SYM MODE_SYM  { MDBP_R(1731); }
        ;

opt_query_expansion:
          /* nothing */                         { MDBP_R(1732); }
        | WITH QUERY_SYM EXPANSION_SYM          { MDBP_R(1733); }
        ;

opt_udf_expr_list:
        /* empty */     { MDBP_R(1734); }
        | udf_expr_list { MDBP_R(1735); }



        ;

opt_udf_expr_list_or_join_operator:
          opt_udf_expr_list { MDBP_R(1736); }
        | remember_name '+' remember_end
          { MDBP_R(1737); }







        ;

udf_expr_list:
          udf_expr
          { MDBP_R(1738); }





        | udf_expr_list ',' udf_expr
          { MDBP_R(1739); }



        ;

udf_expr:
          remember_name expr remember_end select_alias
          { MDBP_R(1740); }






















        ;

sum_expr:
          AVG_SYM '(' in_sum_expr ')'
          { MDBP_R(1741); }




        | AVG_SYM '(' DISTINCT in_sum_expr ')'
          { MDBP_R(1742); }




        | BIT_AND  '(' in_sum_expr ')'
          { MDBP_R(1743); }




        | BIT_OR  '(' in_sum_expr ')'
          { MDBP_R(1744); }




        | BIT_XOR  '(' in_sum_expr ')'
          { MDBP_R(1745); }




        | COUNT_SYM '(' opt_all '*' ')'
          { MDBP_R(1746); }







        | COUNT_SYM '(' in_sum_expr ')'
          { MDBP_R(1747); }




        | COUNT_SYM '(' DISTINCT
          { MDBP_R(1748); }
          expr_list
          { MDBP_R(1749); }
          ')'
          { MDBP_R(1750); }




        | MIN_SYM '(' in_sum_expr ')'
          { MDBP_R(1751); }




        /*
          According to ANSI SQL, DISTINCT is allowed and has
          no sense inside MIN and MAX grouping functions; so MIN|MAX(DISTINCT ...)
          is processed like an ordinary MIN | MAX()
        */
        | MIN_SYM '(' DISTINCT in_sum_expr ')'
          { MDBP_R(1752); }




        | MAX_SYM '(' in_sum_expr ')'
          { MDBP_R(1753); }




        | MAX_SYM '(' DISTINCT in_sum_expr ')'
          { MDBP_R(1754); }




        | STD_SYM '(' in_sum_expr ')'
          { MDBP_R(1755); }




        | VARIANCE_SYM '(' in_sum_expr ')'
          { MDBP_R(1756); }




        | STDDEV_SAMP_SYM '(' in_sum_expr ')'
          { MDBP_R(1757); }




        | VAR_SAMP_SYM '(' in_sum_expr ')'
          { MDBP_R(1758); }




        | SUM_SYM '(' in_sum_expr ')'
          { MDBP_R(1759); }




        | SUM_SYM '(' DISTINCT in_sum_expr ')'
          { MDBP_R(1760); }




        | GROUP_CONCAT_SYM '(' opt_distinct
          { MDBP_R(1761); }
          expr_list opt_gorder_clause
          opt_gconcat_separator opt_glimit_clause
          ')'
          { MDBP_R(1762); }














        | JSON_ARRAYAGG_SYM '(' opt_distinct
          { MDBP_R(1763); }
          expr_list opt_gorder_clause opt_glimit_clause
          ')'
          { MDBP_R(1764); }

























        | JSON_OBJECTAGG_SYM '('
          { MDBP_R(1765); }
          expr ',' expr ')'
          { MDBP_R(1766); }







        | ST_COLLECT_SYM '(' opt_distinct in_sum_expr ')'
          { MDBP_R(1767); }




        ;

window_func_expr:
          window_func OVER_SYM window_name
          { MDBP_R(1768); }






        |
          window_func OVER_SYM window_spec
          { MDBP_R(1769); }













        ;

window_func:
          simple_window_func { MDBP_R(1770); }
        |
          sum_expr { MDBP_R(1771); }
        |
          function_call_generic
          { MDBP_R(1772); }










        ;

simple_window_func:
          ROW_NUMBER_SYM '(' ')'
          { MDBP_R(1773); }




        |
          RANK_SYM '(' ')'
          { MDBP_R(1774); }




        |
          DENSE_RANK_SYM '(' ')'
          { MDBP_R(1775); }




        |
          PERCENT_RANK_SYM '(' ')'
          { MDBP_R(1776); }




        |
          CUME_DIST_SYM '(' ')'
          { MDBP_R(1777); }




        |
          NTILE_SYM '(' expr ')'
          { MDBP_R(1778); }




        |
          FIRST_VALUE_SYM '(' expr ')'
          { MDBP_R(1779); }




        |
          LAST_VALUE '(' expr ')'
          { MDBP_R(1780); }




        |
          NTH_VALUE_SYM '(' expr ',' expr ')'
          { MDBP_R(1781); }




        |
          LEAD_SYM '(' expr ')'
          { MDBP_R(1782); }








        |
          LEAD_SYM '(' expr ',' expr ')'
          { MDBP_R(1783); }




        |
          LAG_SYM '(' expr ')'
          { MDBP_R(1784); }








        |
          LAG_SYM '(' expr ',' expr ')'
          { MDBP_R(1785); }




        ;



inverse_distribution_function:
          percentile_function OVER_SYM
          '(' opt_window_partition_clause ')'
          { MDBP_R(1786); }













        ;

percentile_function:
          inverse_distribution_function_def  WITHIN GROUP_SYM '('
           { MDBP_R(1787); }
           order_by_single_element_list ')'
           { MDBP_R(1788); }


        | MEDIAN_SYM '(' expr ')'
          { MDBP_R(1789); }












        ;

inverse_distribution_function_def:
          PERCENTILE_CONT_SYM '(' expr ')'
          { MDBP_R(1790); }




        |  PERCENTILE_DISC_SYM '(' expr ')'
          { MDBP_R(1791); }




        ;

order_by_single_element_list:
          ORDER_SYM BY order_ident order_dir
          { MDBP_R(1792); }



        ;


window_name:
          ident
          { MDBP_R(1793); }




        ;

variable:
          '@'
          { MDBP_R(1794); }



          variable_aux
          { MDBP_R(1795); }


        ;

variable_aux:
          ident_or_text SET_VAR expr
          { MDBP_R(1796); }













        | ident_or_text
          { MDBP_R(1797); }











        | '@' opt_var_ident_type ident_sysvar_name
          { MDBP_R(1798); }



        | '@' opt_var_ident_type ident_sysvar_name '.' ident
          { MDBP_R(1799); }



        ;

opt_distinct:
          /* empty */ { MDBP_R(1800); }
        | DISTINCT    { MDBP_R(1801); }
        ;

opt_gconcat_separator:
          /* empty */
          { MDBP_R(1802); }




        | SEPARATOR_SYM text_string { MDBP_R(1803); }
        ;

opt_gorder_clause: { MDBP_R(1804); }
          /* empty */
        | ORDER_SYM BY gorder_list { MDBP_R(1805); }
        ;

gorder_list:
          gorder_list ',' order_ident order_dir
          { MDBP_R(1806); }



        | order_ident order_dir
          { MDBP_R(1807); }



        ;

opt_glimit_clause:
          /* empty */ { MDBP_R(1808); }
        | glimit_clause { MDBP_R(1809); }
        ;


glimit_clause:
          LIMIT glimit_options
          { MDBP_R(1810); }


        ;

glimit_options:
          limit_options
          { MDBP_R(1811); }


        ;



in_sum_expr:
          opt_all
          { MDBP_R(1812); }







          expr
          { MDBP_R(1813); }



        ;

cast_type:
          BINARY opt_field_length
          { MDBP_R(1814); }
        | CHAR_SYM opt_field_length opt_binary
          { MDBP_R(1815); }





        | VARCHAR field_length opt_binary
          { MDBP_R(1816); }





        | VARCHAR2_ORACLE_SYM field_length opt_binary
          { MDBP_R(1817); }





        | NCHAR_SYM opt_field_length
          { MDBP_R(1818); }


        | cast_type_numeric  { MDBP_R(1819); }
        | cast_type_temporal { MDBP_R(1820); }
        | udt_name opt_binary
          { MDBP_R(1821); }



        ;

cast_type_numeric:
          INT_SYM                        { MDBP_R(1822); }
        | SIGNED_SYM                     { MDBP_R(1823); }
        | SIGNED_SYM INT_SYM             { MDBP_R(1824); }
        | UNSIGNED                       { MDBP_R(1825); }
        | UNSIGNED INT_SYM               { MDBP_R(1826); }
        | DECIMAL_SYM float_options      { MDBP_R(1827); }
        | FLOAT_SYM                      { MDBP_R(1828); }
        | DOUBLE_SYM opt_precision       { MDBP_R(1829); }
        ;

cast_type_temporal:
          DATE_SYM                       { MDBP_R(1830); }
        | TIME_SYM opt_field_scale       { MDBP_R(1831); }
        | DATETIME opt_field_scale       { MDBP_R(1832); }
        | INTERVAL_SYM DAY_SECOND_SYM field_scale
          { MDBP_R(1833); }


        ;

opt_expr_list:
          /* empty */ { MDBP_R(1834); }
        | expr_list { MDBP_R(1835); }
        ;

opt_expr_list_or_join_operator:
          opt_expr_list { MDBP_R(1836); }
        | '+'
          { MDBP_R(1837); }



        ;

expr_list:
          expr
          { MDBP_R(1838); }



        | expr_list ',' expr
          { MDBP_R(1839); }



        ;

ident_list_arg:
          ident_list          { MDBP_R(1840); }
        | '(' ident_list ')'  { MDBP_R(1841); }
        ;

ident_list:
          simple_ident
          { MDBP_R(1842); }





        | ident_list ',' simple_ident
          { MDBP_R(1843); }



        ;

when_list:
          WHEN_SYM expr THEN_SYM expr
          { MDBP_R(1844); }







        | when_list WHEN_SYM expr THEN_SYM expr
          { MDBP_R(1845); }





        ;

when_list_opt_else:
          when_list { MDBP_R(1846); }
        | when_list ELSE expr
          { MDBP_R(1847); }




        ;

/* Equivalent to <table reference> in the SQL:2003 standard. */
/* Warning - may return NULL in case of incomplete SELECT */
table_ref:
          table_factor     { MDBP_R(1848); }
        | join_table
          { MDBP_R(1849); }











        ;

json_text_literal:
          TEXT_STRING { MDBP_R(1850); }
        | NCHAR_STRING { MDBP_R(1851); }
        | UNDERSCORE_CHARSET TEXT_STRING
          { MDBP_R(1852); }


        ;

join_table_list:
          derived_table_list { MDBP_R(1853); }
        ;

json_table_columns_clause:
          COLUMNS '(' json_table_columns_list ')'
          { MDBP_R(1854); }
        ;

json_table_columns_list:
          json_table_column { MDBP_R(1855); }
        | json_table_columns_list ',' json_table_column
          { MDBP_R(1856); }
        ;

json_table_column:
          ident
          { MDBP_R(1857); }

















          json_table_column_type
          { MDBP_R(1858); }







        | NESTED_SYM PATH_SYM json_text_literal
          { MDBP_R(1859); }






          json_table_columns_clause
          { MDBP_R(1860); }



        ;

json_table_column_type:
          FOR_SYM ORDINALITY_SYM
          { MDBP_R(1861); }








        | json_table_field_type PATH_SYM json_text_literal
            json_opt_on_empty_or_error
          { MDBP_R(1862); }










        | json_table_field_type FORMAT_SYM JSON_SYM PATH_SYM json_text_literal
            json_opt_on_empty_or_error
          { MDBP_R(1863); }











        | json_table_field_type EXISTS PATH_SYM json_text_literal
          { MDBP_R(1864); }








        ;

json_table_field_type:
          field_type_numeric { MDBP_R(1865); }
        | field_type_temporal { MDBP_R(1866); }
        | field_type_string { MDBP_R(1867); }
        | field_type_lob { MDBP_R(1868); }
        ;

json_opt_on_empty_or_error:
          /* none */
          { MDBP_R(1869); }
        | json_on_error_response { MDBP_R(1870); }
        | json_on_error_response json_on_empty_response { MDBP_R(1871); }
        | json_on_empty_response { MDBP_R(1872); }
        | json_on_empty_response json_on_error_response { MDBP_R(1873); }
        ;

json_default_literal:
          literal { MDBP_R(1874); }
        | signed_literal { MDBP_R(1875); }
        ;


json_on_response:
          ERROR_SYM
          { MDBP_R(1876); }


        | NULL_SYM
          { MDBP_R(1877); }


        | DEFAULT json_default_literal
          { MDBP_R(1878); }



        ;

json_on_error_response:
          json_on_response ON ERROR_SYM
          { MDBP_R(1879); }


        ;

json_on_empty_response:
          json_on_response ON EMPTY_SYM
          { MDBP_R(1880); }


        ;

table_function:
          JSON_TABLE_SYM '('
          { MDBP_R(1881);


            Select->parsing_place= IN_ON;
}
          expr ','
          { MDBP_R(1882);








            Select->parsing_place= NO_MATTER;

}
          json_text_literal json_table_columns_clause ')' opt_table_alias_clause
          { MDBP_R(1883); }






















        ;

/*
  The ODBC escape syntax for Outer Join is: '{' OJ join_table '}'
  The parser does not define OJ as a token, any ident is accepted
  instead in $2 (ident). Also, all productions from table_ref can
  be escaped, not only join_table. Both syntax extensions are safe
  and are ignored.
*/
esc_table_ref:
          table_ref { MDBP_R(1884); }
        | '{' ident table_ref '}' { MDBP_R(1885); }
        ;

/* Equivalent to <table reference list> in the SQL:2003 standard. */
/* Warning - may return NULL in case of incomplete SELECT */
derived_table_list:
          esc_table_ref
          { MDBP_R(1886); }



        | derived_table_list ',' esc_table_ref
          { MDBP_R(1887); }



        ;

/*
  Notice that JOIN can be a left-associative operator in one context and
  a right-associative operator in another context (see the comment for
  st_select_lex::add_cross_joined_table).
*/
join_table:
          /* INNER JOIN variants */
          table_ref normal_join table_ref %prec CONDITIONLESS_JOIN
          { MDBP_R(1888); }




        | table_ref normal_join table_ref
          ON
          { MDBP_R(1889);






            Select->parsing_place= IN_ON;
}
          expr
          { MDBP_R(1890);



            Select->parsing_place= NO_MATTER;
}
        | table_ref normal_join table_ref
          USING
          { MDBP_R(1891); }




          '(' using_list ')'
          { MDBP_R(1892); }




        | table_ref NATURAL inner_join table_factor
          { MDBP_R(1893); }







          /* LEFT JOIN variants */
        | table_ref LEFT opt_outer JOIN_SYM table_ref
          ON
          { MDBP_R(1894);






            Select->parsing_place= IN_ON;
}
          search_condition
          { MDBP_R(1895);




            Select->parsing_place= NO_MATTER;
}
        | table_ref LEFT opt_outer JOIN_SYM table_factor
          { MDBP_R(1896); }




          USING '(' using_list ')'
          { MDBP_R(1897); }




        | table_ref NATURAL LEFT opt_outer JOIN_SYM table_factor
          { MDBP_R(1898); }








          /* RIGHT JOIN variants */
        | table_ref RIGHT opt_outer JOIN_SYM table_ref
          ON
          { MDBP_R(1899);






            Select->parsing_place= IN_ON;
}
          expr
          { MDBP_R(1900);





            Select->parsing_place= NO_MATTER;
}
        | table_ref RIGHT opt_outer JOIN_SYM table_factor
          { MDBP_R(1901); }




          USING '(' using_list ')'
          { MDBP_R(1902); }





        | table_ref NATURAL RIGHT opt_outer JOIN_SYM table_factor
          { MDBP_R(1903); }








        ;


inner_join: /* $$ set if using STRAIGHT_JOIN, false otherwise */
          JOIN_SYM           { MDBP_R(1904); }
        | INNER_SYM JOIN_SYM { MDBP_R(1905); }
        | STRAIGHT_JOIN      { MDBP_R(1906); }
        ;

normal_join:
          inner_join         { MDBP_R(1907); }
        | CROSS JOIN_SYM     { MDBP_R(1908); }
        ;

/*
  table PARTITION (list of partitions), reusing using_list instead of creating
  a new rule for partition_list.
*/
opt_use_partition:
          /* empty */ { MDBP_R(1909); }
        | use_partition { MDBP_R(1910); }
        ;
        
use_partition:
          PARTITION_SYM '(' using_list ')' have_partitioning
          { MDBP_R(1911);

            Select->parsing_place= Select->save_parsing_place;
            Select->save_parsing_place= NO_MATTER;
}
        ;

table_factor:
          table_primary_ident_opt_parens { MDBP_R(1912); }
        | table_primary_derived_opt_parens { MDBP_R(1913); }
        | join_table_parens
          { MDBP_R(1914); }



        | table_reference_list_parens { MDBP_R(1915); }
        | table_function { MDBP_R(1916); }
        ;

table_primary_ident_opt_parens:
          table_primary_ident { MDBP_R(1917); }
        | '(' table_primary_ident_opt_parens ')' { MDBP_R(1918); }
        ;

table_primary_derived_opt_parens:
          table_primary_derived { MDBP_R(1919); }
        | '(' table_primary_derived_opt_parens ')' { MDBP_R(1920); }
        ;

table_reference_list_parens:
          '(' table_reference_list_parens ')' { MDBP_R(1921); }
        | '(' nested_table_reference_list ')'
          { MDBP_R(1922); }



        ;

nested_table_reference_list:
          table_ref ',' table_ref
          { MDBP_R(1923); }






        | nested_table_reference_list ',' table_ref
          { MDBP_R(1924); }



        ;

join_table_parens:
          '(' join_table_parens ')' { MDBP_R(1925); }
        | '(' join_table ')'
          { MDBP_R(1926); }







        ;


table_primary_ident:
          table_ident opt_use_partition opt_for_system_time_clause
          opt_table_alias_clause opt_key_definition
          { MDBP_R(1927); }










        ;

derived_column_list:
        opt_column_name_list { MDBP_R(1928); }
        ;

table_primary_derived:
          subquery
          opt_for_system_time_clause
          table_alias_clause
          derived_column_list
          { MDBP_R(1929); }













        ;

opt_outer:
          /* empty */ { MDBP_R(1930); }
        | OUTER { MDBP_R(1931); }
        ;

index_hint_clause:
          /* empty */
          { MDBP_R(1932); }



        | FOR_SYM JOIN_SYM      { MDBP_R(1933); }
        | FOR_SYM ORDER_SYM BY  { MDBP_R(1934); }
        | FOR_SYM GROUP_SYM BY  { MDBP_R(1935); }
        ;

index_hint_type:
          FORCE_SYM  { MDBP_R(1936); }
        | IGNORE_SYM { MDBP_R(1937); } 
        ;

index_hint_definition:
          index_hint_type key_or_index index_hint_clause
          { MDBP_R(1938); }


          '(' key_usage_list ')' { MDBP_R(1939); }
        | USE_SYM key_or_index index_hint_clause
          { MDBP_R(1940); }


          '(' opt_key_usage_list ')' { MDBP_R(1941); }
       ;

index_hints_list:
          index_hint_definition { MDBP_R(1942); }
        | index_hints_list index_hint_definition { MDBP_R(1943); }
        ;

opt_index_hints_list: { MDBP_R(1944); }
          /* empty */
        | { MDBP_R(1945); } index_hints_list { MDBP_R(1946); }
        ;

opt_key_definition:
          { MDBP_R(1947); }
          opt_index_hints_list { MDBP_R(1948); }
        ;

opt_key_usage_list:
          /* empty */ { MDBP_R(1949); }
        | key_usage_list { MDBP_R(1950); }
        ;

key_usage_element:
          ident
          { MDBP_R(1951); }
        | PRIMARY_SYM
          { MDBP_R(1952); }
        ;

key_usage_list:
          key_usage_element { MDBP_R(1953); }
        | key_usage_list ',' key_usage_element { MDBP_R(1954); }
        ;

using_list:
          ident
          { MDBP_R(1955); }









        | using_list ',' ident
          { MDBP_R(1956); }









        ;

interval:
          interval_time_stamp    { MDBP_R(1957); }
        | DAY_HOUR_SYM           { MDBP_R(1958); }
        | DAY_MICROSECOND_SYM    { MDBP_R(1959); }
        | DAY_MINUTE_SYM         { MDBP_R(1960); }
        | DAY_SECOND_SYM         { MDBP_R(1961); }
        | HOUR_MICROSECOND_SYM   { MDBP_R(1962); }
        | HOUR_MINUTE_SYM        { MDBP_R(1963); }
        | HOUR_SECOND_SYM        { MDBP_R(1964); }
        | MINUTE_MICROSECOND_SYM { MDBP_R(1965); }
        | MINUTE_SECOND_SYM      { MDBP_R(1966); }
        | SECOND_MICROSECOND_SYM { MDBP_R(1967); }
        | YEAR_MONTH_SYM         { MDBP_R(1968); }
        ;

interval_time_stamp:
          DAY_SYM         { MDBP_R(1969); }
        | WEEK_SYM        { MDBP_R(1970); }
        | HOUR_SYM        { MDBP_R(1971); }
        | MINUTE_SYM      { MDBP_R(1972); }
        | MONTH_SYM       { MDBP_R(1973); }
        | QUARTER_SYM     { MDBP_R(1974); }
        | SECOND_SYM      { MDBP_R(1975); }
        | MICROSECOND_SYM { MDBP_R(1976); }
        | YEAR_SYM        { MDBP_R(1977); }
        ;

date_time_type:
          DATE_SYM  { MDBP_R(1978); }
        | TIME_SYM  { MDBP_R(1979); }
        | DATETIME  { MDBP_R(1980); }
        | TIMESTAMP { MDBP_R(1981); }
        ;

table_alias: { MDBP_R(1982); }
          /* empty */
        | AS { MDBP_R(1983); }
        | '=' { MDBP_R(1984); }
        ;

opt_table_alias_clause:
          /* empty */ { MDBP_R(1985); }
        | table_alias_clause { MDBP_R(1986); }
        ;

table_alias_clause:
          table_alias ident_table_alias
          { MDBP_R(1987); }




        ;

opt_all: { MDBP_R(1988); }
          /* empty */
        | ALL { MDBP_R(1989); }
        ;

opt_where_clause:
          /* empty */  { MDBP_R(1990); }
        | WHERE
          { MDBP_R(1991);
            Select->parsing_place= IN_WHERE;
}
          search_condition
          { MDBP_R(1992);


            Select->parsing_place= NO_MATTER;
}
        ;

opt_having_clause: { MDBP_R(1993); }
          /* empty */
        | HAVING
          { MDBP_R(1994);
            Select->parsing_place= IN_HAVING;
}
          search_condition
          { MDBP_R(1995);


            sel->parsing_place= NO_MATTER;


}
        ;

/*
   group by statement in select
*/

opt_group_clause: { MDBP_R(1996); }
          /* empty */
        | GROUP_SYM BY group_list olap_opt { MDBP_R(1997); }
        ;

group_list:
          group_list ',' order_ident order_dir
          { MDBP_R(1998); }



        | order_ident order_dir
          { MDBP_R(1999); }



        ;

olap_opt:
          /* empty */ { MDBP_R(2000); }
        | WITH_CUBE_SYM
          { MDBP_R(2001); }















        | WITH_ROLLUP_SYM
          { MDBP_R(2002); }













        ;

/*
  optional window clause in select
*/

opt_window_clause:
          /* empty */
          { MDBP_R(2003); }
        | WINDOW_SYM
          window_def_list
          { MDBP_R(2004); }
        ;

window_def_list:
          window_def_list ',' window_def { MDBP_R(2005); }
        | window_def { MDBP_R(2006); }
        ;

window_def:
          window_name AS window_spec
          { MDBP_R(2007); }







        ;

window_spec:
          '(' 
          { MDBP_R(2008); }
          opt_window_ref opt_window_partition_clause
          opt_window_order_clause opt_window_frame_clause
          ')'
          { MDBP_R(2009); }
        ;

opt_window_ref:
          /* empty */ { MDBP_R(2010); } 
        | ident
          { MDBP_R(2011); }




        ;

opt_window_partition_clause:
          /* empty */ { MDBP_R(2012); }
        | PARTITION_SYM BY group_list { MDBP_R(2013); }
        ;

opt_window_order_clause:
          /* empty */ { MDBP_R(2014); }
        | ORDER_SYM BY order_list { MDBP_R(2015); }
        ;

opt_window_frame_clause:
          /* empty */ { MDBP_R(2016); }
        | window_frame_units window_frame_extent opt_window_frame_exclusion
          { MDBP_R(2017); }









        ;

window_frame_units:
          ROWS_SYM { MDBP_R(2018); }
        | RANGE_SYM { MDBP_R(2019); }
        ;
         
window_frame_extent:
          window_frame_start
          { MDBP_R(2020); }








        | BETWEEN_SYM window_frame_bound AND_SYM window_frame_bound
          { MDBP_R(2021); }




        ;

window_frame_start:
          UNBOUNDED_SYM PRECEDING_SYM
          { MDBP_R(2022); }




 
        | CURRENT_SYM ROW_SYM
          { MDBP_R(2023); }





        | literal PRECEDING_SYM
          { MDBP_R(2024); }





        ;

window_frame_bound:
          window_frame_start { MDBP_R(2025); }
        | UNBOUNDED_SYM FOLLOWING_SYM        
          { MDBP_R(2026); }




 
        | literal FOLLOWING_SYM
          { MDBP_R(2027); }





        ;

opt_window_frame_exclusion:
          /* empty */ { MDBP_R(2028); }
        | EXCLUDE_SYM CURRENT_SYM ROW_SYM
          { MDBP_R(2029); }
        | EXCLUDE_SYM GROUP_SYM
          { MDBP_R(2030); }
        | EXCLUDE_SYM TIES_SYM
          { MDBP_R(2031); }
        | EXCLUDE_SYM NO_SYM OTHERS_MARIADB_SYM
          { MDBP_R(2032); }
        | EXCLUDE_SYM NO_SYM OTHERS_ORACLE_SYM
          { MDBP_R(2033); }
        ;      
       
/*
  Order by statement in ALTER TABLE
*/

alter_order_clause:
          ORDER_SYM BY alter_order_list { MDBP_R(2034); }
        ;

alter_order_list:
          alter_order_list ',' alter_order_item { MDBP_R(2035); }
        | alter_order_item { MDBP_R(2036); }
        ;

alter_order_item:
          simple_ident_nospvar order_dir
          { MDBP_R(2037); }




        ;

/*
   Order by statement in select
*/

opt_order_clause:
          /* empty */
          { MDBP_R(2038); }
        | order_clause
          { MDBP_R(2039); }
        ;

order_clause:
          ORDER_SYM BY
          { MDBP_R(2040); }



          order_list
          { MDBP_R(2041); }


         ;

order_list:
          order_list ',' order_ident order_dir
          { MDBP_R(2042); }




        | order_ident order_dir
          { MDBP_R(2043); }




        ;

order_dir:
          /* empty */ { MDBP_R(2044); }
        | ASC  { MDBP_R(2045); }
        | DESC { MDBP_R(2046); }
        ;


opt_limit_clause:
          /* empty */
          { MDBP_R(2047); }
        | limit_clause
          { MDBP_R(2048); }
        ;

limit_clause:
          LIMIT limit_options
          { MDBP_R(2049); }





        | LIMIT limit_options
          ROWS_SYM EXAMINED_SYM limit_rows_option
          { MDBP_R(2050); }



        | LIMIT ROWS_SYM EXAMINED_SYM limit_rows_option
          { MDBP_R(2051); }



        | fetch_first_clause
          { MDBP_R(2052); }






        ;

fetch_first_clause:
          FETCH_SYM first_or_next row_or_rows only_or_with_ties
          { MDBP_R(2053); }








        | OFFSET_SYM limit_option row_or_rows
          FETCH_SYM first_or_next row_or_rows only_or_with_ties
          { MDBP_R(2054); }








        | FETCH_SYM first_or_next limit_option row_or_rows only_or_with_ties
          { MDBP_R(2055); }





        | OFFSET_SYM limit_option row_or_rows
          FETCH_SYM first_or_next limit_option row_or_rows only_or_with_ties
          { MDBP_R(2056); }





        | OFFSET_SYM limit_option row_or_rows
          { MDBP_R(2057); }





        ;

first_or_next:
          FIRST_SYM { MDBP_R(2058); }
        | NEXT_SYM { MDBP_R(2059); }
        ;

row_or_rows:
          ROW_SYM { MDBP_R(2060); }
        | ROWS_SYM { MDBP_R(2061); }
        ;

only_or_with_ties:
          ONLY_SYM      { MDBP_R(2062); }
        | WITH TIES_SYM { MDBP_R(2063); }
        ;


opt_global_limit_clause:
          opt_limit_clause
          { MDBP_R(2064); }


        ;

limit_options:
          limit_option
          { MDBP_R(2065); }





        | limit_option ',' limit_option
          { MDBP_R(2066); }





        | limit_option OFFSET_SYM limit_option
          { MDBP_R(2067); }





        ;

limit_option:
          ident_cli
          { MDBP_R(2068); }



        | ident_cli '.' ident_cli
          { MDBP_R(2069); }



        | param_marker
          { MDBP_R(2070); }


        | ULONGLONG_NUM
          { MDBP_R(2071); }




        | LONG_NUM
          { MDBP_R(2072); }




        | NUM
          { MDBP_R(2073); }




        ;

limit_rows_option:
          limit_option
          { MDBP_R(2074); }


        ;

delete_limit_clause:
          /* empty */
          { MDBP_R(2075); }



        | LIMIT limit_option
          { MDBP_R(2076); }





       | LIMIT ROWS_SYM EXAMINED_SYM { MDBP_R(2077); }
       | LIMIT limit_option ROWS_SYM EXAMINED_SYM { MDBP_R(2078); }
        ;

order_limit_lock:
          order_or_limit
          { MDBP_R(2079); }



        | order_or_limit select_lock_type
          { MDBP_R(2080); }



        | select_lock_type
          { MDBP_R(2081); }







        ;

opt_order_limit_lock:
          /* empty */
          { MDBP_R(2082); }



        | order_limit_lock { MDBP_R(2083); }
        ;

query_expression_tail:
          order_limit_lock { MDBP_R(2084); }
        ;

opt_query_expression_tail:
          opt_order_limit_lock { MDBP_R(2085); }
        ;

opt_procedure_or_into:
          /* empty */
          { MDBP_R(2086); }


        | procedure_clause opt_select_lock_type
          { MDBP_R(2087); }


        | into opt_select_lock_type
          { MDBP_R(2088); }



        ;

opt_order_or_limit:
          /* empty */
          { MDBP_R(2089); }


        |
          order_or_limit
          { MDBP_R(2090); }



        ;

order_or_limit:
          order_clause opt_limit_clause
          { MDBP_R(2091); }






        | limit_clause
          { MDBP_R(2092); }






        ;


opt_plus: { MDBP_R(2093); }
          /* empty */
        | '+' { MDBP_R(2094); }
        ;

int_num:
          opt_plus NUM           { MDBP_R(2095); }
        | '-' NUM       { MDBP_R(2096); }
        ;

ulong_num:
          opt_plus NUM           { MDBP_R(2097); }
        | HEX_NUM       { MDBP_R(2098); }
        | opt_plus LONG_NUM      { MDBP_R(2099); }
        | opt_plus ULONGLONG_NUM { MDBP_R(2100); }
        | opt_plus DECIMAL_NUM   { MDBP_R(2101); }
        | opt_plus FLOAT_NUM     { MDBP_R(2102); }
        ;

real_ulong_num:
          NUM           { MDBP_R(2103); }
        | HEX_NUM       { MDBP_R(2104); }
        | LONG_NUM      { MDBP_R(2105); }
        | ULONGLONG_NUM { MDBP_R(2106); }
        | dec_num_error { MDBP_R(2107); }
        ;

/*
  For simple sequence metadata values that are signed and do not need
  truncation
*/
sequence_value_num:
          opt_plus NUM
          { MDBP_R(2108); }



        | opt_plus LONG_NUM
          { MDBP_R(2109); }



        | '-' NUM
          { MDBP_R(2110); }



        | '-' LONG_NUM
          { MDBP_R(2111); }



        | '-' ULONGLONG_NUM
          { MDBP_R(2112); }







        ;

/*
  For sequence metadata values that may be unsigned but do not need
  truncation (start, restart)
*/
sequence_value_hybrid_num:
          opt_plus NUM
          { MDBP_R(2113); }




        | opt_plus LONG_NUM
          { MDBP_R(2114); }




        | opt_plus ULONGLONG_NUM
          { MDBP_R(2115); }




        | '-' NUM
          { MDBP_R(2116); }




        | '-' LONG_NUM
          { MDBP_R(2117); }




        | '-' ULONGLONG_NUM
          { MDBP_R(2118); }










        ;

/*
  For sequence metadata values that may be unsigned and need
  truncation (maxvalue, minvalue)
*/
sequence_truncated_value_hybrid_num:
          opt_plus NUM
          { MDBP_R(2119); }




        | opt_plus LONG_NUM
          { MDBP_R(2120); }




        | opt_plus ULONGLONG_NUM
          { MDBP_R(2121); }




        | opt_plus DECIMAL_NUM
          { MDBP_R(2122); }



        | '-' NUM
          { MDBP_R(2123); }




        | '-' LONG_NUM
          { MDBP_R(2124); }




        | '-' ULONGLONG_NUM
          { MDBP_R(2125); }



        | '-' DECIMAL_NUM
          { MDBP_R(2126); }



        ;

ulonglong_num:
          opt_plus NUM           { MDBP_R(2127); }
        | opt_plus ULONGLONG_NUM { MDBP_R(2128); }
        | opt_plus LONG_NUM      { MDBP_R(2129); }
        | opt_plus DECIMAL_NUM   { MDBP_R(2130); }
        | opt_plus FLOAT_NUM     { MDBP_R(2131); }
        ;

real_ulonglong_num:
          NUM           { MDBP_R(2132); }
        | ULONGLONG_NUM { MDBP_R(2133); }
        | HEX_NUM       { MDBP_R(2134); }
        | LONG_NUM      { MDBP_R(2135); }
        | dec_num_error { MDBP_R(2136); }
        ;

dec_num_error:
          dec_num
          { MDBP_R(2137); }
        ;

dec_num:
          DECIMAL_NUM { MDBP_R(2138); }
        | FLOAT_NUM { MDBP_R(2139); }
        ;

choice:
	ulong_num { MDBP_R(2140); }
	| DEFAULT { MDBP_R(2141); }
	;

bool:
        ulong_num   { MDBP_R(2142); }
        | TRUE_SYM  { MDBP_R(2143); }
        | FALSE_SYM { MDBP_R(2144); }
        ;

procedure_clause:
          PROCEDURE_SYM ident /* Procedure name */
          { MDBP_R(2145); }






















          '(' procedure_list ')'
          { MDBP_R(2146); }



        ;

procedure_list:
          /* empty */ { MDBP_R(2147); }
        | procedure_list2 { MDBP_R(2148); }
        ;

procedure_list2:
          procedure_list2 ',' procedure_item { MDBP_R(2149); }
        | procedure_item { MDBP_R(2150); }
        ;

procedure_item:
          remember_name expr remember_end
          { MDBP_R(2151); }





        ;

select_var_list_init:
          { MDBP_R(2152); }






          select_var_list
          { MDBP_R(2153); }
        ;

select_var_list:
          select_var_list ',' select_var_ident { MDBP_R(2154); }
        | select_var_ident { MDBP_R(2155); }
        ;

select_var_ident: select_outvar
          { MDBP_R(2156); }















        ;

select_outvar:
          '@' ident_or_text
          { MDBP_R(2157); }








        | ident_or_text
          { MDBP_R(2158); }




        | ident '.' ident
          { MDBP_R(2159); }



        | ident '(' expr ')' opt_object_member_access
          { MDBP_R(2160); }







        ;

into:
          INTO into_destination
          { MDBP_R(2161); }
        ;

into_destination:
          OUTFILE TEXT_STRING_filesystem
          { MDBP_R(2162); }










          opt_load_data_charset
          { MDBP_R(2163); }
          opt_field_term opt_line_term { MDBP_R(2164); }
        | DUMPFILE TEXT_STRING_filesystem
          { MDBP_R(2165); }













        | select_var_list_init
          { MDBP_R(2166); }



        ;

/*
  DO statement
*/

do:
          DO_SYM
          { MDBP_R(2167); }






          expr_list
          { MDBP_R(2168); }





        ;

/*
  Drop : delete tables or index or user
*/

drop:
          DROP opt_temporary table_or_tables opt_if_exists
          { MDBP_R(2169); }





          table_list opt_lock_wait_timeout opt_restrict
          { MDBP_R(2170); }
        | DROP INDEX_SYM
          { MDBP_R(2171); }



          opt_if_exists_table_element ident ON table_ident opt_lock_wait_timeout
          { MDBP_R(2172); }
















        | DROP DATABASE opt_if_exists ident
          { MDBP_R(2173); }




        | DROP USER_SYM opt_if_exists clear_privileges user_list
          { MDBP_R(2174); }


        | DROP ROLE_SYM opt_if_exists clear_privileges role_list
          { MDBP_R(2175); }


        | DROP VIEW_SYM opt_if_exists
          { MDBP_R(2176); }





          table_list opt_restrict
          { MDBP_R(2177); }
        | DROP EVENT_SYM opt_if_exists sp_name
          { MDBP_R(2178); }



        | DROP TRIGGER_SYM opt_if_exists sp_name
          { MDBP_R(2179); }




        | DROP SERVER_SYM opt_if_exists ident_or_text
          { MDBP_R(2180); }



        | DROP opt_temporary SEQUENCE_SYM opt_if_exists

          { MDBP_R(2181); }






          table_list
          { MDBP_R(2182); }
        | drop_routine { MDBP_R(2183); }
        ;

table_list:
          table_name { MDBP_R(2184); }
        | table_list ',' table_name { MDBP_R(2185); }
        ;

table_name:
          table_ident
          { MDBP_R(2186); }







        ;

table_name_with_opt_use_partition:
          table_ident opt_use_partition
          { MDBP_R(2187); }








        ;

table_alias_ref_list:
          table_alias_ref { MDBP_R(2188); }
        | table_alias_ref_list ',' table_alias_ref { MDBP_R(2189); }
        ;

table_alias_ref:
          table_ident_opt_wild
          { MDBP_R(2190); }








        ;

opt_if_exists_table_element:
          /* empty */
        { MDBP_R(2191); }



        | IF_SYM EXISTS
        { MDBP_R(2192); }



        ;

opt_if_exists:
          /* empty */        %prec PREC_BELOW_CONTRACTION_TOKEN2
        { MDBP_R(2193); }


        | IF_SYM EXISTS
        { MDBP_R(2194); }


        ;

opt_temporary:
          /* empty */ { MDBP_R(2195); }
        | TEMPORARY { MDBP_R(2196); }
        ;
/*
** Insert : add new data to table
*/

insert:
          INSERT opt_optimizer_hint
          { MDBP_R(2197); }





          insert_start insert_lock_option opt_ignore opt_into insert_table
          { MDBP_R(2198); }



          insert_field_spec opt_insert_update opt_returning
          insert_stmt_end
          { MDBP_R(2199); }




          ;

replace:
          REPLACE opt_optimizer_hint
          { MDBP_R(2200); }





          insert_start replace_lock_option opt_into insert_table
          { MDBP_R(2201); }



          insert_field_spec opt_returning
          insert_stmt_end
          { MDBP_R(2202); }




          ;

insert_start: { MDBP_R(2203);




                Lex->current_select->parsing_place= BEFORE_OPT_LIST;
}
              ;

stmt_end: { MDBP_R(2204); }





            ;

insert_stmt_end: { MDBP_R(2205); }




            ;

insert_lock_option:
          /* empty */
          { MDBP_R(2206); }







        | insert_replace_option { MDBP_R(2207); }
        | HIGH_PRIORITY { MDBP_R(2208); }
        ;

replace_lock_option:
          /* empty */ { MDBP_R(2209); }
        | insert_replace_option { MDBP_R(2210); }
        ;

insert_replace_option:
          LOW_PRIORITY  { MDBP_R(2211); }
        | DELAYED_SYM
        { MDBP_R(2212); }




        ;

opt_into: { MDBP_R(2213); } /* nothing */ | INTO { MDBP_R(2214); } ;

insert_table:
          { MDBP_R(2215);
            Select->save_parsing_place= Select->parsing_place;
}
          table_name_with_opt_use_partition
          { MDBP_R(2216); }





        ;

insert_field_spec:
          insert_values { MDBP_R(2217); }
        | insert_field_list insert_values { MDBP_R(2218); }
        | SET
          { MDBP_R(2219);





            lex->current_select->parsing_place= NO_MATTER;
}
          ident_eq_list { MDBP_R(2220); }
        ;

insert_field_list:
          LEFT_PAREN_ALT opt_fields ')'
          { MDBP_R(2221);
            Lex->current_select->parsing_place= AFTER_LIST;
}
        ;

opt_fields: { MDBP_R(2222); }
          /* empty */
        | fields { MDBP_R(2223); }
        ;

fields:
          fields ',' insert_ident
          { MDBP_R(2224); }
        | insert_ident { MDBP_R(2225); }
        ;



insert_values:
         create_select_query_expression { MDBP_R(2226); }
        ;

values_list:
          values_list ','  no_braces { MDBP_R(2227); }
        | no_braces_with_names { MDBP_R(2228); }
        ;

ident_eq_list:
          ident_eq_list ',' ident_eq_value { MDBP_R(2229); }
        | ident_eq_value { MDBP_R(2230); }
        ;

ident_eq_value:
          simple_ident_nospvar equal expr_or_ignore_or_default
          { MDBP_R(2231); }





        ;

equal:
          '=' { MDBP_R(2232); }
        | SET_VAR { MDBP_R(2233); }
        ;

opt_equal:
          /* empty */ { MDBP_R(2234); }
        | equal { MDBP_R(2235); }
        ;

opt_with:
          opt_equal { MDBP_R(2236); }
        | WITH { MDBP_R(2237); }
        ;

opt_by:
          opt_equal { MDBP_R(2238); }
        | BY { MDBP_R(2239); }
        ;

no_braces:
          '('
          { MDBP_R(2240); }



          opt_values ')'
          { MDBP_R(2241); }






        ;

no_braces_with_names:
          '('
          { MDBP_R(2242); }



          opt_values_with_names ')'
          { MDBP_R(2243); }






        ;

opt_values:
          /* empty */ { MDBP_R(2244); }
        | values { MDBP_R(2245); }
        ;

opt_values_with_names:
          /* empty */ { MDBP_R(2246); }
        | values_with_names { MDBP_R(2247); }
        ;

values:
          values ','  expr_or_ignore_or_default
          { MDBP_R(2248); }



        | expr_or_ignore_or_default
          { MDBP_R(2249); }



        ;

values_with_names:
          values_with_names ','  remember_name expr_or_ignore_or_default remember_end
          { MDBP_R(2250); }






        | remember_name expr_or_ignore_or_default remember_end
          { MDBP_R(2251); }






        ;

expr_or_ignore:
          expr { MDBP_R(2252); }
        | IGNORE_SYM
          { MDBP_R(2253); }




        ;

expr_or_ignore_or_default:
          expr_or_ignore { MDBP_R(2254); }
        | DEFAULT
          { MDBP_R(2255); }





        ;

opt_insert_update: { MDBP_R(2256); }
          /* empty */
        | ON DUPLICATE_SYM { MDBP_R(2257); }
          KEY_SYM UPDATE_SYM 
          { MDBP_R(2258);
	    Select->parsing_place= IN_UPDATE_ON_DUP_KEY;
}
          insert_update_list
          { MDBP_R(2259);
	    Select->parsing_place= NO_MATTER;
}
        ;

update_table_list:
          table_ident opt_use_partition for_portion_of_time_clause
          opt_table_alias_clause opt_key_definition
          { MDBP_R(2260); }









        | join_table_list { MDBP_R(2261); }
        ;

/* Update rows in a table */

update:
          opt_with_clause
          UPDATE_SYM opt_optimizer_hint
          { MDBP_R(2262); }











          opt_low_priority opt_ignore update_table_list
          SET update_list
          { MDBP_R(2263); }

























          opt_where_clause opt_order_clause delete_limit_clause
          { MDBP_R(2264); }



          opt_returning
          stmt_end { MDBP_R(2265); }
        ;

update_list:
          update_list ',' update_elem { MDBP_R(2266); }
        | update_elem { MDBP_R(2267); }
        ;

update_elem:
          simple_ident_nospvar equal DEFAULT
          { MDBP_R(2268); }






        | simple_ident_nospvar equal expr_or_ignore
          { MDBP_R(2269); }



        ;

insert_update_list:
          insert_update_list ',' insert_update_elem { MDBP_R(2270); }
        | insert_update_elem { MDBP_R(2271); }
        ;

insert_update_elem:
          simple_ident_nospvar equal expr_or_ignore_or_default
          { MDBP_R(2272); }





        ;

opt_low_priority:
          /* empty */ { MDBP_R(2273); }
        | LOW_PRIORITY { MDBP_R(2274); }
        ;

/* Delete rows from a table */

delete:
          opt_with_clause
          DELETE_SYM opt_optimizer_hint
          { MDBP_R(2275); }













          delete_part2
          { MDBP_R(2276); }




          ;

opt_delete_system_time:
            /* empty */
          { MDBP_R(2277); }


          | BEFORE_SYM SYSTEM_TIME_SYM history_point
          { MDBP_R(2278); }


          ;

delete_part2:
          opt_delete_options single_multi { MDBP_R(2279); }
        | HISTORY_SYM delete_single_table opt_delete_system_time
          { MDBP_R(2280); }









          stmt_end
          { MDBP_R(2281); }
        ;

delete_single_table:
          FROM table_ident opt_table_alias_clause opt_key_definition opt_use_partition
          { MDBP_R(2282); }

























        ;

delete_single_table_for_period:
          delete_single_table opt_for_portion_of_time_clause
          { MDBP_R(2283); }



        ;

single_multi:
          delete_single_table_for_period
          opt_where_clause
          opt_order_clause
          delete_limit_clause
          opt_returning
          { MDBP_R(2284); }










          stmt_end { MDBP_R(2285); }
        | table_alias_ref_list
          { MDBP_R(2286); }









          FROM join_table_list opt_where_clause opt_order_or_limit
          { MDBP_R(2287); }







 stmt_end { MDBP_R(2288); }
        | FROM table_alias_ref_list
          { MDBP_R(2289); }









          USING join_table_list opt_where_clause opt_order_or_limit
          { MDBP_R(2290); }







 stmt_end { MDBP_R(2291); }
        ;

opt_returning:
          /* empty */
          { MDBP_R(2292); }


        | RETURNING_SYM
          { MDBP_R(2293);

































            thd->lex->current_select->parsing_place= IN_RETURNING;



}
          select_item_list
          { MDBP_R(2294);

            thd->lex->current_select->parsing_place= NO_MATTER;


}
        ;

opt_wild:
          /* empty */ { MDBP_R(2295); }
        | '.' '*' { MDBP_R(2296); }
        ;

opt_delete_options:
          /* empty */ { MDBP_R(2297); }
        | opt_delete_option opt_delete_options { MDBP_R(2298); }
        ;

opt_delete_option:
          QUICK        { MDBP_R(2299); }
        | LOW_PRIORITY { MDBP_R(2300); }
        | IGNORE_SYM   { MDBP_R(2301); }
        ;

truncate:
          TRUNCATE_SYM
          { MDBP_R(2302); }









          opt_table_sym table_name opt_lock_wait_timeout
          { MDBP_R(2303); }






          opt_truncate_table_storage_clause { MDBP_R(2304); }
        ;

opt_table_sym: { MDBP_R(2305); }
          /* empty */
        | TABLE_SYM { MDBP_R(2306); }
        ;

opt_profile_defs: { MDBP_R(2307); }
  /* empty */
  | profile_defs { MDBP_R(2308); };

profile_defs:
  profile_def { MDBP_R(2309); }
  | profile_defs ',' profile_def { MDBP_R(2310); };

profile_def:
  CPU_SYM
    { MDBP_R(2311); }


  | MEMORY_SYM
    { MDBP_R(2312); }


  | BLOCK_SYM IO_SYM
    { MDBP_R(2313); }


  | CONTEXT_SYM SWITCHES_SYM
    { MDBP_R(2314); }


  | PAGE_SYM FAULTS_SYM
    { MDBP_R(2315); }


  | IPC_SYM
    { MDBP_R(2316); }


  | SWAPS_SYM
    { MDBP_R(2317); }


  | SOURCE_SYM
    { MDBP_R(2318); }


  | ALL
    { MDBP_R(2319); }


  ;

opt_profile_args:
  /* empty */
    { MDBP_R(2320); }


  | FOR_SYM QUERY_SYM NUM
    { MDBP_R(2321); }


  ;

/* Show things */

show:
          SHOW
          { MDBP_R(2322);






            lex->current_select->parsing_place= SELECT_LIST;

}
          show_param
          { MDBP_R(2323);
            Select->parsing_place= NO_MATTER;

}
        ;

show_param:
           DATABASES wild_and_where
           { MDBP_R(2324); }
        | mrs_show_rest_metadata_status_statement { MDBP_R(5919); }
        | mrs_show_rest_metadata_schemas_statement { MDBP_R(5920); }
        | mrs_show_rest_services_statement { MDBP_R(5921); }
        | mrs_show_rest_daemons_statement { MDBP_R(5922); }
        | mrs_show_rest_schemas_statement { MDBP_R(5923); }
        | mrs_show_rest_views_statement { MDBP_R(5924); }
        | mrs_show_rest_procedures_statement { MDBP_R(5925); }
        | mrs_show_rest_functions_statement { MDBP_R(5926); }
        | mrs_show_rest_scripts_statement { MDBP_R(5927); }
        | mrs_show_rest_content_sets_statement { MDBP_R(5928); }
        | mrs_show_rest_content_files_statement { MDBP_R(5929); }
        | mrs_show_rest_auth_apps_statement { MDBP_R(5930); }
        | mrs_show_rest_auth_vendors_statement { MDBP_R(5931); }
        | mrs_show_rest_users_statement { MDBP_R(5932); }
        | mrs_show_rest_columns_statement { MDBP_R(5933); }
        | mrs_show_rest_roles_statement { MDBP_R(5934); }
        | mrs_show_rest_grants_statement { MDBP_R(5935); }
        | mrs_show_create_rest_service_statement { MDBP_R(5936); }
        | mrs_show_create_rest_schema_statement { MDBP_R(5937); }
        | mrs_show_create_rest_view_statement { MDBP_R(5938); }
        | mrs_show_create_rest_procedure_statement { MDBP_R(5939); }
        | mrs_show_create_rest_function_statement { MDBP_R(5940); }
        | mrs_show_create_rest_content_set_statement { MDBP_R(5941); }
        | mrs_show_create_rest_content_file_statement { MDBP_R(5942); }
        | mrs_show_create_rest_auth_app_statement { MDBP_R(5943); }
        | mrs_show_create_rest_role_statement { MDBP_R(5944); }
        | mrs_show_create_rest_user_statement { MDBP_R(5945); }





         | opt_full TABLES opt_db wild_and_where
           { MDBP_R(2325); }






         | opt_full TRIGGERS_SYM opt_db wild_and_where
           { MDBP_R(2326); }






         | EVENTS_SYM opt_db wild_and_where
           { MDBP_R(2327); }






         | TABLE_SYM STATUS_SYM opt_db wild_and_where
           { MDBP_R(2328); }






        | OPEN_SYM TABLES opt_db wild_and_where
          { MDBP_R(2329); }






        | PLUGINS_SYM
          { MDBP_R(2330); }





        | PLUGINS_SYM SONAME_SYM TEXT_STRING_sys
          { MDBP_R(2331); }





        | PLUGINS_SYM SONAME_SYM wild_and_where
          { MDBP_R(2332); }




        | ENGINE_SYM known_storage_engines show_engine_param
          { MDBP_R(2333); }
        | ENGINE_SYM ALL show_engine_param
          { MDBP_R(2334); }
        | opt_full COLUMNS from_or_in table_ident opt_db wild_and_where
          { MDBP_R(2335); }







        | master_or_binary LOGS_SYM
          { MDBP_R(2336); }


        | SLAVE HOSTS_SYM
          { MDBP_R(2337); }


        | BINLOG_SYM EVENTS_SYM binlog_in binlog_from
          { MDBP_R(2338); }



          opt_global_limit_clause { MDBP_R(2339); }
        | RELAYLOG_SYM optional_connection_name EVENTS_SYM binlog_in binlog_from
          { MDBP_R(2340); }



          opt_global_limit_clause optional_for_channel
          { MDBP_R(2341); }
        | keys_or_index from_or_in table_ident opt_db opt_where_clause
          { MDBP_R(2342); }







        | opt_storage ENGINES_SYM
          { MDBP_R(2343); }





        | AUTHORS_SYM
          { MDBP_R(2344); }



        | CONTRIBUTORS_SYM
          { MDBP_R(2345); }



        | PRIVILEGES
          { MDBP_R(2346); }



        | COUNT_SYM '(' '*' ')' WARNINGS
          { MDBP_R(2347); }



        | COUNT_SYM '(' '*' ')' ERRORS
          { MDBP_R(2348); }



        | WARNINGS opt_global_limit_clause
          { MDBP_R(2349); }
        | ERRORS opt_global_limit_clause
          { MDBP_R(2350); }
        | PROFILES_SYM
          { MDBP_R(2351); }
        | PROFILE_SYM opt_profile_defs opt_profile_args opt_global_limit_clause
          { MDBP_R(2352); }





        | opt_var_type STATUS_SYM wild_and_where
          { MDBP_R(2353); }






        | opt_full PROCESSLIST_SYM
          { MDBP_R(2354); }
        | opt_var_type  VARIABLES wild_and_where
          { MDBP_R(2355); }






        | charset wild_and_where
          { MDBP_R(2356); }





        | COLLATION_SYM wild_and_where
          { MDBP_R(2357); }





        | GRANTS
          { MDBP_R(2358); }





        | GRANTS FOR_SYM user_or_role clear_privileges
          { MDBP_R(2359); }




        | CREATE DATABASE opt_if_not_exists ident
          { MDBP_R(2360); }



        | CREATE TABLE_SYM table_ident
          { MDBP_R(2361); }






        | CREATE VIEW_SYM table_ident
          { MDBP_R(2362); }






        | CREATE SEQUENCE_SYM table_ident
          { MDBP_R(2363); }






        | BINLOG_SYM STATUS_SYM
          { MDBP_R(2364); }


        | MASTER_SYM STATUS_SYM
          { MDBP_R(2365); }


        | BINARY LOG_SYM STATUS_SYM
          { MDBP_R(2366); }


        | ALL SLAVES STATUS_SYM
          { MDBP_R(2367); }








        | SLAVE optional_connection_name STATUS_SYM optional_for_channel
          { MDBP_R(2368); }








        | CREATE PROCEDURE_SYM sp_name
          { MDBP_R(2369); }





        | CREATE FUNCTION_SYM sp_name
          { MDBP_R(2370); }





        | CREATE PACKAGE_MARIADB_SYM sp_name
          { MDBP_R(2371); }




        | CREATE PACKAGE_ORACLE_SYM sp_name
          { MDBP_R(2372); }




        | CREATE PACKAGE_MARIADB_SYM BODY_MARIADB_SYM sp_name
          { MDBP_R(2373); }




        | CREATE PACKAGE_ORACLE_SYM BODY_ORACLE_SYM sp_name
          { MDBP_R(2374); }




        | CREATE SERVER_SYM ident_or_text
          { MDBP_R(2375); }




        | CREATE TRIGGER_SYM sp_name
          { MDBP_R(2376); }




        | CREATE USER_SYM
          { MDBP_R(2377); }





        | CREATE USER_SYM user
          { MDBP_R(2378); }



        | PROCEDURE_SYM STATUS_SYM wild_and_where
          { MDBP_R(2379); }





        | FUNCTION_SYM STATUS_SYM wild_and_where
          { MDBP_R(2380); }





        | PACKAGE_MARIADB_SYM STATUS_SYM wild_and_where
          { MDBP_R(2381); }





        | PACKAGE_ORACLE_SYM STATUS_SYM wild_and_where
          { MDBP_R(2382); }





        | PACKAGE_MARIADB_SYM BODY_MARIADB_SYM STATUS_SYM wild_and_where
          { MDBP_R(2383); }





        | PACKAGE_ORACLE_SYM BODY_ORACLE_SYM STATUS_SYM wild_and_where
          { MDBP_R(2384); }





        | PROCEDURE_SYM CODE_SYM sp_name
          { MDBP_R(2385); }



        | FUNCTION_SYM CODE_SYM sp_name
          { MDBP_R(2386); }



        | PACKAGE_MARIADB_SYM BODY_MARIADB_SYM CODE_SYM sp_name
          { MDBP_R(2387); }




        | PACKAGE_ORACLE_SYM BODY_ORACLE_SYM CODE_SYM sp_name
          { MDBP_R(2388); }




        | CREATE EVENT_SYM sp_name
          { MDBP_R(2389); }



        | describe_command opt_format_json FOR_SYM expr
          /*
            The alternative syntax for this command is MySQL-compatible
            EXPLAIN FOR CONNECTION
          */
          { MDBP_R(2390); }






        | ANALYZE_SYM opt_format_json FOR_SYM expr
          { MDBP_R(2391); }






        | IDENT_sys remember_tok_start wild_and_where
           { MDBP_R(2392); }

















        ;

show_engine_param:
          STATUS_SYM
          { MDBP_R(2393); }
        | MUTEX_SYM
          { MDBP_R(2394); }
        | LOGS_SYM
          { MDBP_R(2395); }
        ;

master_or_binary:
          MASTER_SYM { MDBP_R(2396); }
        | BINARY { MDBP_R(2397); }
        ;

opt_storage: { MDBP_R(2398); }
          /* empty */
        | STORAGE_SYM { MDBP_R(2399); }
        ;

opt_db:
          /* empty */      { MDBP_R(2400); }
        | from_or_in ident { MDBP_R(2401); }
        ;

opt_full:
          /* empty */ { MDBP_R(2402); }
        | FULL        { MDBP_R(2403); }
        ;

from_or_in:
          FROM { MDBP_R(2404); }
        | IN_SYM { MDBP_R(2405); }
        ;

binlog_in:
          /* empty */            { MDBP_R(2406); }
        | IN_SYM TEXT_STRING_sys { MDBP_R(2407); }
        ;

binlog_from:
          /* empty */        { MDBP_R(2408); }
        | FROM ulonglong_num { MDBP_R(2409); }
        ;

wild_and_where:
          /* empty */ { MDBP_R(2410); }
        | LIKE remember_tok_start TEXT_STRING_sys
          { MDBP_R(2411); }







        | WHERE remember_tok_start search_condition
          { MDBP_R(2412); }





        ;

/* A Oracle compatible synonym for show */
describe:
          describe_command table_ident
          { MDBP_R(2413);




            lex->current_select->parsing_place= SELECT_LIST;





}
          opt_describe_column
          { MDBP_R(2414);
            Select->parsing_place= NO_MATTER;

}
        | describe_command opt_extended_describe
          { MDBP_R(2415); }
          explainable_command
          { MDBP_R(2416); }



        ;

explainable_command:
          select { MDBP_R(2417); }
        | select_into { MDBP_R(2418); }
        | insert { MDBP_R(2419); }
        | replace { MDBP_R(2420); }
        | update { MDBP_R(2421); }
        | delete { MDBP_R(2422); }
        ;

describe_command:
          DESC { MDBP_R(2423); }
        | DESCRIBE { MDBP_R(2424); }
        ;

analyze_stmt_command:
          ANALYZE_SYM opt_format_json explainable_command
          { MDBP_R(2425); }


        ;

opt_extended_describe:
          EXTENDED_SYM   { MDBP_R(2426); }
        | EXTENDED_SYM ALL
          { MDBP_R(2427); }
        | PARTITIONS_SYM { MDBP_R(2428); }
        | opt_format_json { MDBP_R(2429); }
        ;

opt_format_json:
          /* empty */ { MDBP_R(2430); }
        | FORMAT_SYM '=' ident_or_text
          { MDBP_R(2431); }








        ;

opt_describe_column:
          /* empty */ { MDBP_R(2432); }
        | text_string { MDBP_R(2433); }
        | ident
          { MDBP_R(2434); }






        ;

explain_for_connection:
          /*
            EXPLAIN FOR CONNECTION is an alternative syntax for
            SHOW EXPLAIN FOR command. It was introduced for compatibility
            with MySQL which implements EXPLAIN FOR CONNECTION command
          */
          describe_command opt_format_json
          { MDBP_R(2435);






            lex->current_select->parsing_place= SELECT_LIST;
}
          FOR_SYM CONNECTION_SYM expr
          { MDBP_R(2436);


            Select->parsing_place= NO_MATTER;






}
        ;

/* flush things */

flush:
          FLUSH_SYM opt_no_write_to_binlog
          { MDBP_R(2437); }





          flush_options { MDBP_R(2438); }
        ;

flush_options:
          table_or_tables
          { MDBP_R(2439); }








          opt_table_list opt_flush_lock
          { MDBP_R(2440); }
        | flush_options_list
          { MDBP_R(2441); }
        ;

opt_flush_lock:
          /* empty */ { MDBP_R(2442); }
        | flush_lock
        { MDBP_R(2443); }








        ;

flush_lock:
          WITH READ_SYM LOCK_SYM optional_flush_tables_arguments
          { MDBP_R(2444); }
        | FOR_SYM
          { MDBP_R(2445); }







 EXPORT_SYM { MDBP_R(2446); }
        ;

flush_options_list:
          flush_options_list ',' flush_option { MDBP_R(2447); }
        | flush_option
          { MDBP_R(2448); }
        ;

flush_option:
          ERROR_SYM LOGS_SYM
          { MDBP_R(2449); }
        | ENGINE_SYM LOGS_SYM
          { MDBP_R(2450); } 
        | GENERAL LOGS_SYM
          { MDBP_R(2451); }
        | SLOW LOGS_SYM
          { MDBP_R(2452); }
        | BINARY LOGS_SYM opt_delete_gtid_domain
          { MDBP_R(2453); }
        | RELAY LOGS_SYM optional_connection_name optional_for_channel
          { MDBP_R(2454); }






        | QUERY_SYM CACHE_SYM
          { MDBP_R(2455); }
        | HOSTS_SYM
          { MDBP_R(2456); }
        | PRIVILEGES
          { MDBP_R(2457); }
        | LOGS_SYM
          { MDBP_R(2458); }



        | STATUS_SYM
          { MDBP_R(2459); }





        | SESSION_SYM STATUS_SYM
          { MDBP_R(2460); }
        | GLOBAL_SYM STATUS_SYM
          { MDBP_R(2461); }
        | SLAVE optional_connection_name 
          { MDBP_R(2462); }






        | MASTER_SYM
          { MDBP_R(2463); }
        | RESOURCES
          { MDBP_R(2464); }
        | SSL_SYM
          { MDBP_R(2465); }
        | THREADS_SYM
          { MDBP_R(2466); }        
        | IDENT_sys remember_tok_start
           { MDBP_R(2467); }












        ;

opt_table_list:
          /* empty */  { MDBP_R(2468); }
        | table_list { MDBP_R(2469); }
        ;

backup:
        BACKUP_SYM backup_statements { MDBP_R(2470); }
	;

backup_statements:
	STAGE_SYM ident
        { MDBP_R(2471); }










	| LOCK_SYM
          { MDBP_R(2472); }





          table_ident
          { MDBP_R(2473); }






        | UNLOCK_SYM
          { MDBP_R(2474); }





        ;

opt_delete_gtid_domain:
          /* empty */ { MDBP_R(2475); }
        | DELETE_DOMAIN_ID_SYM '=' '(' delete_domain_id_list ')'
          { MDBP_R(2476); }
        ;
delete_domain_id_list: { MDBP_R(2477); }
          /* Empty */
        | delete_domain_id { MDBP_R(2478); }
        | delete_domain_id_list ',' delete_domain_id { MDBP_R(2479); }
        ;

delete_domain_id:
          ulonglong_num
          { MDBP_R(2480); }











        ;

optional_flush_tables_arguments:
          /* empty */        { MDBP_R(2481); }
        | AND_SYM DISABLE_SYM CHECKPOINT_SYM { MDBP_R(2482); } 
        ;

reset:
          RESET_SYM
          { MDBP_R(2483); }



          reset_options
          { MDBP_R(2484); }
        ;

reset_options:
          reset_options ',' reset_option { MDBP_R(2485); }
        | reset_option { MDBP_R(2486); }
        ;

reset_option:
          SLAVE               { MDBP_R(2487); }
          optional_connection_name
          slave_reset_options optional_for_channel
          { MDBP_R(2488); }
        | MASTER_SYM
          { MDBP_R(2489); }



          master_reset_options { MDBP_R(2490); }
        | QUERY_SYM CACHE_SYM { MDBP_R(2491); }
        ;

slave_reset_options:
          /* empty */ { MDBP_R(2492); }
        | ALL         { MDBP_R(2493); }
        ;

master_reset_options:
          /* empty */ { MDBP_R(2494); }
        | TO_SYM ulong_num
          { MDBP_R(2495); }


        ;

purge:
          PURGE master_or_binary LOGS_SYM TO_SYM TEXT_STRING_sys
          { MDBP_R(2496); }


        | PURGE master_or_binary LOGS_SYM BEFORE_SYM
          { MDBP_R(2497); }
          expr
          { MDBP_R(2498); }




        ;


/* kill threads */

kill:
          KILL_SYM
          { MDBP_R(2499); }








          kill_type kill_option
          { MDBP_R(2500); }


        ;

kill_type:
        /* Empty */    { MDBP_R(2501); }
        | HARD_SYM     { MDBP_R(2502); }
        | SOFT_SYM     { MDBP_R(2503); }
        ;

kill_option:
          opt_connection kill_expr { MDBP_R(2504); }
        | QUERY_SYM      kill_expr { MDBP_R(2505); }
        | QUERY_SYM ID_SYM expr
          { MDBP_R(2506); }




        ;

opt_connection:
          /* empty */    { MDBP_R(2507); }
        | CONNECTION_SYM { MDBP_R(2508); }
        ;

kill_expr:
        expr
        { MDBP_R(2509); }


        | USER_SYM user
          { MDBP_R(2510); }



        ;

shutdown:
        SHUTDOWN { MDBP_R(2511); }
        shutdown_option { MDBP_R(2512); }
        ;

shutdown_option:
        /*  Empty */    { MDBP_R(2513); }
        | WAIT_SYM FOR_SYM ALL SLAVES
        { MDBP_R(2514); }


        ;

/* change database */

use:
          USE_SYM ident
          { MDBP_R(2515); }




        ;

/* import, export of files */

load:
          LOAD data_or_xml
          { MDBP_R(2516); }












          load_data_lock opt_local INFILE TEXT_STRING_filesystem
          { MDBP_R(2517); }









          opt_duplicate INTO TABLE_SYM table_ident opt_use_partition
          { MDBP_R(2518); }











          opt_load_data_charset
          { MDBP_R(2519); }
          opt_xml_rows_identified_by
          opt_field_term opt_line_term opt_ignore_lines opt_field_or_var_spec
          opt_load_data_set_spec
          stmt_end
          { MDBP_R(2520); }


          ;

data_or_xml:
        DATA_SYM  { MDBP_R(2521); }
        | XML_SYM { MDBP_R(2522); }
        ;

opt_local:
          /* empty */ { MDBP_R(2523); }
        | LOCAL_SYM { MDBP_R(2524); }
        ;

load_data_lock:
          /* empty */ { MDBP_R(2525); }
        | CONCURRENT
          { MDBP_R(2526); }






        | LOW_PRIORITY { MDBP_R(2527); }
        ;

opt_duplicate:
          /* empty */ { MDBP_R(2528); }
        | REPLACE { MDBP_R(2529); }
        | IGNORE_SYM { MDBP_R(2530); }
        ;

opt_field_term: { MDBP_R(2531); }
          /* empty */
        | COLUMNS field_term_list { MDBP_R(2532); }
        ;

field_term_list:
          field_term_list field_term { MDBP_R(2533); }
        | field_term { MDBP_R(2534); }
        ;

field_term:
          TERMINATED BY text_string 
          { MDBP_R(2535); }



        | OPTIONALLY ENCLOSED BY text_string
          { MDBP_R(2536); }





        | ENCLOSED BY text_string
          { MDBP_R(2537); }



        | ESCAPED BY text_string
          { MDBP_R(2538); }



        ;

opt_line_term: { MDBP_R(2539); }
          /* empty */
        | LINES line_term_list { MDBP_R(2540); }
        ;

line_term_list:
          line_term_list line_term { MDBP_R(2541); }
        | line_term { MDBP_R(2542); }
        ;

line_term:
          TERMINATED BY text_string
          { MDBP_R(2543); }



        | STARTING BY text_string
          { MDBP_R(2544); }



        ;

opt_xml_rows_identified_by:
        /* empty */ { MDBP_R(2545); }
        | ROWS_SYM IDENTIFIED_SYM BY text_string
          { MDBP_R(2546); }
        ;

opt_ignore_lines: { MDBP_R(2547); }
          /* empty */
        | IGNORE_SYM NUM lines_or_rows
          { MDBP_R(2548); }



        ;

lines_or_rows:
          LINES { MDBP_R(2549); }
        | ROWS_SYM { MDBP_R(2550); }
        ;

opt_field_or_var_spec:
          /* empty */ { MDBP_R(2551); }
        | '(' fields_or_vars ')' { MDBP_R(2552); }
        | '(' ')' { MDBP_R(2553); }
        ;

fields_or_vars:
          fields_or_vars ',' field_or_var
          { MDBP_R(2554); }
        | field_or_var
          { MDBP_R(2555); }
        ;

field_or_var:
          simple_ident_nospvar { MDBP_R(2556); }
        | '@' ident_or_text
          { MDBP_R(2557); }










        ;

opt_load_data_set_spec:
          /* empty */ { MDBP_R(2558); }
        | SET load_data_set_list { MDBP_R(2559); }
        ;

load_data_set_list:
          load_data_set_list ',' load_data_set_elem { MDBP_R(2560); }
        | load_data_set_elem { MDBP_R(2561); }
        ;

load_data_set_elem:
          simple_ident_nospvar equal remember_name expr_or_ignore_or_default remember_end
          { MDBP_R(2562); }






        ;

/* Common definitions */

text_literal:
          TEXT_STRING
          { MDBP_R(2563); }



        | NCHAR_STRING
          { MDBP_R(2564); }



        | UNDERSCORE_CHARSET TEXT_STRING
          { MDBP_R(2565); }





        | text_literal TEXT_STRING_literal
          { MDBP_R(2566); }



        ;

text_string:
          TEXT_STRING_literal
          { MDBP_R(2567); }






          | hex_or_bin_String { MDBP_R(2568); }
          ;


hex_or_bin_String:
          HEX_NUM
          { MDBP_R(2569); }






        | HEX_STRING
          { MDBP_R(2570); }






        | BIN_NUM
          { MDBP_R(2571); }










        ;

param_marker:
          PARAM_MARKER
          { MDBP_R(2572); }





        | COLON_ORACLE_SYM ident_cli
          { MDBP_R(2573); }




        | COLON_ORACLE_SYM NUM
          { MDBP_R(2574); }





        ;

signed_literal:
        '+' NUM_literal { MDBP_R(2575); }
        | '-' NUM_literal
          { MDBP_R(2576); }



        ;

literal:
          text_literal { MDBP_R(2577); }
        | NUM_literal { MDBP_R(2578); }
        | temporal_literal { MDBP_R(2579); }
        | NULL_SYM
          { MDBP_R(2580); }












        | FALSE_SYM
          { MDBP_R(2581); }




        | TRUE_SYM
          { MDBP_R(2582); }




        | HEX_NUM
          { MDBP_R(2583); }




        | HEX_STRING
          { MDBP_R(2584); }




        | BIN_NUM
          { MDBP_R(2585); }




        | UNDERSCORE_CHARSET hex_or_bin_String
          { MDBP_R(2586); }


















        ;

NUM_literal:
          NUM
          { MDBP_R(2587); }








        | LONG_NUM
          { MDBP_R(2588); }








        | ULONGLONG_NUM
          { MDBP_R(2589); }




        | DECIMAL_NUM
          { MDBP_R(2590); }





        | FLOAT_NUM
          { MDBP_R(2591); }




        ;


temporal_literal:
        DATE_SYM TEXT_STRING
          { MDBP_R(2592); }





        | TIME_SYM TEXT_STRING
          { MDBP_R(2593); }





        | TIMESTAMP TEXT_STRING
          { MDBP_R(2594); }





        ;

opt_with_clause:
          /* empty */ { MDBP_R(2595); }
        | with_clause { MDBP_R(2596); }
        ;

with_clause:
          WITH opt_recursive
          { MDBP_R(2597);










             if (lex->current_select &&
                 lex->current_select->parsing_place == BEFORE_OPT_LIST)
               lex->current_select->parsing_place= NO_MATTER;
}
          with_list
          { MDBP_R(2598); }


 
        ;


opt_recursive:
 	  /*empty*/ { MDBP_R(2599); }
	| RECURSIVE_SYM { MDBP_R(2600); }
	;


with_list:
	  with_list_element { MDBP_R(2601); } 	
	| with_list ',' with_list_element { MDBP_R(2602); }
	;

with_column_list:
        opt_column_name_list { MDBP_R(2603); }
        ;

with_list_element:
          with_element_head
	  with_column_list
          AS '(' query_expression ')' opt_cycle
 	  { MDBP_R(2604); }
















	;

opt_cycle:
         /* empty */
         { MDBP_R(2605); }
         |
         CYCLE_SYM
         { MDBP_R(2606); }





         comma_separated_ident_list RESTRICT
         { MDBP_R(2607); }


         ;

opt_column_name_list:
          /* empty */
          { MDBP_R(2608); }



        | '(' comma_separated_ident_list ')'
          { MDBP_R(2609); }
        ;

ident_sys_alloc:
          ident_cli
          { MDBP_R(2610); }


        ;

comma_separated_ident_list:
          ident_sys_alloc
          { MDBP_R(2611); }




        | comma_separated_ident_list ',' ident_sys_alloc
          { MDBP_R(2612); }



        ;


with_element_head:
          ident
          { MDBP_R(2613); }






        ;


	
/**********************************************************************
** Creating different items.
**********************************************************************/

insert_ident:
          simple_ident_nospvar { MDBP_R(2614); }
        | table_wild { MDBP_R(2615); }
        ;

table_wild:
          ident '.' '*'
          { MDBP_R(2616); }



        | ident '.' ident '.' '*'
          { MDBP_R(2617); }



        ;

select_sublist_qualified_asterisk:
          ident_cli '.' '*'
          { MDBP_R(2618); }



        | ident_cli '.' ident_cli '.' '*'
          { MDBP_R(2619); }



        ;

order_ident:
          expr { MDBP_R(2620); }
        ;

simple_ident:
          ident_cli
          { MDBP_R(2621); }



        | ident_cli '.' ident_cli
          { MDBP_R(2622); }



        | '.' ident_cli '.' ident_cli
          { MDBP_R(2623); }




        | ident_cli '.' ident_cli '.' ident_cli
          { MDBP_R(2624); }



        | COLON_ORACLE_SYM ident_cli '.' ident_cli
          { MDBP_R(2625); }



        ;

simple_ident_nospvar:
          ident
          { MDBP_R(2626); }



        | ident '.' ident
          { MDBP_R(2627); }



        | COLON_ORACLE_SYM ident_cli '.' ident_cli
          { MDBP_R(2628); }



        | '.' ident '.' ident
          { MDBP_R(2629); }




        | ident '.' ident '.' ident
          { MDBP_R(2630); }



        ;

field_ident:
          ident { MDBP_R(2631); }
        | ident '.' ident '.' ident
          { MDBP_R(2632); }







        | ident '.' ident
          { MDBP_R(2633); }





        | '.' ident { MDBP_R(2634); } /* For Delphi */
        ;

table_ident:
          ident
          { MDBP_R(2635); }




        | ident '.' ident
          { MDBP_R(2636); }




        | '.' ident
          { MDBP_R(2637); }





        ;

table_ident_opt_wild:
          ident opt_wild
          { MDBP_R(2638); }




        | ident '.' ident opt_wild
          { MDBP_R(2639); }




        ;

table_ident_nodb:
          ident
          { MDBP_R(2640); }





        ;

IDENT_cli:
          IDENT { MDBP_R(2641); }
        | IDENT_QUOTED { MDBP_R(2642); }
        ;

ident_cli:
          IDENT { MDBP_R(2643); }
        | IDENT_QUOTED { MDBP_R(2644); }
        | keyword_ident { MDBP_R(2645); }
        ;

IDENT_sys:
          IDENT_cli
          { MDBP_R(2646); }



        ;

ident_cli_func:
          IDENT { MDBP_R(2647); }
        | IDENT_QUOTED { MDBP_R(2648); }
        | keyword_func_sp_var_and_label  { MDBP_R(2649); }
        | keyword_func_sp_var_not_label  { MDBP_R(2650); }
        ;

TEXT_STRING_sys:
          TEXT_STRING
          { MDBP_R(2651); }



        ;

TEXT_STRING_literal:
          TEXT_STRING
          { MDBP_R(2652); }



        ;

TEXT_STRING_filesystem:
          TEXT_STRING
          { MDBP_R(2653); }



        ;

ident_table_alias:
          IDENT_sys { MDBP_R(2654); }
        | keyword_table_alias
          { MDBP_R(2655); }



        ;

ident_cli_set_usual_case:
          IDENT_cli { MDBP_R(2656); }
        | keyword_set_usual_case { MDBP_R(2657); }
        ;

ident_sysvar_name:
          IDENT_sys { MDBP_R(2658); }
        | keyword_sysvar_name
          { MDBP_R(2659); }



        | TEXT_STRING_sys
          { MDBP_R(2660); }



        ;


ident:
          IDENT_sys { MDBP_R(2661); }
        | keyword_ident
          { MDBP_R(2662); }



        ;

label_ident:
          IDENT_sys { MDBP_R(2663); }
        | keyword_label
          { MDBP_R(2664); }



        ;

ident_or_text:
          ident           { MDBP_R(2665); }
        | TEXT_STRING_sys { MDBP_R(2666); }
        | LEX_HOSTNAME { MDBP_R(2667); }
        ;

user_maybe_role:
          ident_or_text
          { MDBP_R(2668); }









        | user_name { MDBP_R(2669); }
        | CURRENT_USER optional_braces
          { MDBP_R(2670); }




        ;

user_name:
          ident_or_text '@' ident_or_text
          { MDBP_R(2671); }















user_or_role: user_maybe_role { MDBP_R(2672); } | current_role { MDBP_R(2673); };

user: user_maybe_role
         { MDBP_R(2674); }




         ;

/* Keywords which we allow as table aliases. */
keyword_table_alias:
          keyword_data_type { MDBP_R(2675); }
        | keyword_cast_type { MDBP_R(2676); }
        | keyword_set_special_case { MDBP_R(2677); }
        | keyword_sp_block_section { MDBP_R(2678); }
        | keyword_sp_head { MDBP_R(2679); }
        | keyword_sp_var_and_label { MDBP_R(2680); }
        | keyword_sp_var_not_label { MDBP_R(2681); }
        | keyword_sysvar_type { MDBP_R(2682); }
        | keyword_verb_clause { MDBP_R(2683); }
        | FUNCTION_SYM { MDBP_R(2684); }
        | EXCEPTION_ORACLE_SYM { MDBP_R(2685); }
        | IGNORED_SYM { MDBP_R(2686); }
        ;

/* Keyword that we allow for identifiers (except SP labels) */
keyword_ident:
          keyword_data_type { MDBP_R(2687); }
        | keyword_cast_type { MDBP_R(2688); }
        | keyword_set_special_case { MDBP_R(2689); }
        | keyword_sp_block_section { MDBP_R(2690); }
        | keyword_sp_head { MDBP_R(2691); }
        | keyword_sp_var_and_label { MDBP_R(2692); }
        | keyword_sp_var_not_label { MDBP_R(2693); }
        | keyword_sysvar_type { MDBP_R(2694); }
        | keyword_verb_clause { MDBP_R(2695); }
        | FUNCTION_SYM { MDBP_R(2696); }
        | WINDOW_SYM { MDBP_R(2697); }
        | EXCEPTION_ORACLE_SYM { MDBP_R(2698); }
        | IGNORED_SYM { MDBP_R(2699); }



        | REF_SYM { MDBP_R(2700); }
        ;

keyword_sysvar_name:
          keyword_data_type { MDBP_R(2701); }
        | keyword_cast_type { MDBP_R(2702); }
        | keyword_set_special_case { MDBP_R(2703); }
        | keyword_sp_block_section { MDBP_R(2704); }
        | keyword_sp_head { MDBP_R(2705); }
        | keyword_sp_var_and_label { MDBP_R(2706); }
        | keyword_sp_var_not_label { MDBP_R(2707); }
        | keyword_verb_clause { MDBP_R(2708); }
        | FUNCTION_SYM { MDBP_R(2709); }
        | WINDOW_SYM { MDBP_R(2710); }
        | EXCEPTION_ORACLE_SYM { MDBP_R(2711); }
        | IGNORED_SYM { MDBP_R(2712); }
        | OFFSET_SYM { MDBP_R(2713); }
        ;

keyword_set_usual_case:
          keyword_data_type { MDBP_R(2714); }
        | keyword_cast_type { MDBP_R(2715); }
        | keyword_sp_block_section { MDBP_R(2716); }
        | keyword_sp_head { MDBP_R(2717); }
        | keyword_sp_var_and_label { MDBP_R(2718); }
        | keyword_sp_var_not_label { MDBP_R(2719); }
        | keyword_sysvar_type { MDBP_R(2720); }
        | keyword_verb_clause { MDBP_R(2721); }
        | FUNCTION_SYM { MDBP_R(2722); }
        | WINDOW_SYM { MDBP_R(2723); }
        | EXCEPTION_ORACLE_SYM { MDBP_R(2724); }
        | IGNORED_SYM { MDBP_R(2725); }
        | OFFSET_SYM { MDBP_R(2726); }
        ;

non_reserved_keyword_udt:
          keyword_sp_var_not_label { MDBP_R(2727); }
        | keyword_sp_head { MDBP_R(2728); }
        | keyword_verb_clause { MDBP_R(2729); }
        | keyword_set_special_case { MDBP_R(2730); }
        | keyword_sp_block_section { MDBP_R(2731); }
        | keyword_sysvar_type { MDBP_R(2732); }
        | keyword_sp_var_and_label { MDBP_R(2733); }
        | OFFSET_SYM { MDBP_R(2734); }
        ;

/*
  Keywords that we allow in Oracle-style direct assignments:
    xxx := 10;
  but do not allow in labels in the default sql_mode:
    label:
      stmt1;
      stmt2;
  TODO: check if some of them can migrate to keyword_sp_var_and_label.
*/
keyword_sp_var_not_label:
        keyword_func_sp_var_not_label { MDBP_R(2735); }
        | ASCII_SYM { MDBP_R(2736); }
        | BACKUP_SYM { MDBP_R(2737); }
        | BINLOG_SYM { MDBP_R(2738); }
        | BYTE_SYM { MDBP_R(2739); }
        | CACHE_SYM { MDBP_R(2740); }
        | CHECKSUM_SYM { MDBP_R(2741); }
        | CHECKPOINT_SYM { MDBP_R(2742); }
        | COLUMN_ADD_SYM { MDBP_R(2743); }
        | COLUMN_CREATE_SYM { MDBP_R(2744); }
        | COLUMN_DELETE_SYM { MDBP_R(2745); }
        | COLUMN_GET_SYM { MDBP_R(2746); }
        | COMMENT_SYM { MDBP_R(2747); }
        | COMPRESSED_SYM { MDBP_R(2748); }
        | DEALLOCATE_SYM { MDBP_R(2749); }
        | EXAMINED_SYM { MDBP_R(2750); }
        | EXCLUDE_SYM { MDBP_R(2751); }
        | EXECUTE_SYM { MDBP_R(2752); }
        | FLUSH_SYM { MDBP_R(2753); }
        | FOLLOWING_SYM { MDBP_R(2754); }
        | GET_SYM { MDBP_R(2755); }
        | HELP_SYM { MDBP_R(2756); }
        | HOST_SYM { MDBP_R(2757); }
        | INSTALL_SYM { MDBP_R(2758); }
        | OPTION { MDBP_R(2759); }
        | OPTIONS_SYM { MDBP_R(2760); }
        | OTHERS_MARIADB_SYM { MDBP_R(2761); }
        | OWNER_SYM { MDBP_R(2762); }
        | PARSER_SYM { MDBP_R(2763); }
        | PERIOD_SYM { MDBP_R(2764); }
        | PORT_SYM { MDBP_R(2765); }
        | PRECEDING_SYM { MDBP_R(2766); }
        | PREPARE_SYM { MDBP_R(2767); }
        | REMOVE_SYM { MDBP_R(2768); }
        | RESET_SYM { MDBP_R(2769); }
        | SECURITY_SYM { MDBP_R(2770); }
        | SERVER_SYM { MDBP_R(2771); }
        | SOCKET_SYM { MDBP_R(2772); }
        | SLAVE { MDBP_R(2773); }
        | SLAVES { MDBP_R(2774); }
        | SONAME_SYM { MDBP_R(2775); }
        | START_SYM { MDBP_R(2776); }
        | STOP_SYM { MDBP_R(2777); }
        | STORED_SYM { MDBP_R(2778); }
        | TIES_SYM { MDBP_R(2779); }
        | UNICODE_SYM { MDBP_R(2780); }
        | UNINSTALL_SYM { MDBP_R(2781); }
        | UNBOUNDED_SYM { MDBP_R(2782); }
        | WITHIN { MDBP_R(2783); }
        | WRAPPER_SYM { MDBP_R(2784); }
        | XA_SYM { MDBP_R(2785); }
        | UPGRADE_SYM { MDBP_R(2786); }
        ;

/*
  Keywords that can start optional clauses in SP or trigger declarations
  Allowed as identifiers (e.g. table, column names),
  but:
  - not allowed as SP label names
  - not allowed as variable names in Oracle-style assignments:
    xxx := 10;

  If we allowed these variables in assignments, there would be conflicts
  with SP characteristics, or verb clauses, or compound statements, e.g.:
    CREATE PROCEDURE p1 LANGUAGE ...
  would be either:
    CREATE PROCEDURE p1 LANGUAGE SQL BEGIN END;
  or
    CREATE PROCEDURE p1 LANGUAGE:=10;

  Note, these variables can still be assigned using quoted identifiers:
    `do`:= 10;
    "do":= 10; (when ANSI_QUOTES)
  or using a SET statement:
    SET do= 10;

  Note, some of these keywords are reserved keywords in Oracle.
  In case if heavy grammar conflicts are found in the future,
  we'll possibly need to make them reserved for sql_mode=ORACLE.

  TODO: Allow these variables as SP labels when sql_mode=ORACLE.
  TODO: Allow assigning of "SP characteristics" marked variables
        inside compound blocks.
  TODO: Allow "follows" and "precedes" as variables in compound blocks:
        BEGIN
          follows := 10;
        END;
        as they conflict only with non-block FOR EACH ROW statement:
          CREATE TRIGGER .. FOR EACH ROW follows:= 10;
          CREATE TRIGGER .. FOR EACH ROW FOLLOWS tr1 a:= 10;
*/
keyword_sp_head:
          CONTAINS_SYM { MDBP_R(2787); }           /* SP characteristic               */
        | LANGUAGE_SYM { MDBP_R(2788); }           /* SP characteristic               */
        | NO_SYM { MDBP_R(2789); }                 /* SP characteristic               */
        | CHARSET { MDBP_R(2790); }                /* SET CHARSET utf8;               */
        | FOLLOWS_SYM { MDBP_R(2791); }            /* Conflicts with assignment in FOR EACH */
        | PRECEDES_SYM { MDBP_R(2792); }           /* Conflicts with assignment in FOR EACH */
        ;

/*
  Keywords that start a statement.
  Generally allowed as identifiers (e.g. table, column names)
  - not allowed as SP label names
  - not allowed as variable names in Oracle-style assignments:
    xxx:=10
*/
keyword_verb_clause:
          CLOSE_SYM { MDBP_R(2793); }             /* Verb clause. Reserved in Oracle */
        | CLONE_SYM { MDBP_R(5917); }
        | CONFIGURE_SYM { MDBP_R(5918); }
        | COMMIT_SYM { MDBP_R(2794); }            /* Verb clause. Reserved in Oracle */
        | DO_SYM { MDBP_R(2795); }                /* Verb clause                     */
        | HANDLER_SYM { MDBP_R(2796); }           /* Verb clause                     */
        | OPEN_SYM { MDBP_R(2797); }              /* Verb clause. Reserved in Oracle */
        | REPAIR { MDBP_R(2798); }                /* Verb clause                     */
        | ROLLBACK_SYM { MDBP_R(2799); }          /* Verb clause. Reserved in Oracle */
        | SAVEPOINT_SYM { MDBP_R(2800); }         /* Verb clause. Reserved in Oracle */
        | SHUTDOWN { MDBP_R(2801); }              /* Verb clause                     */
        | TRUNCATE_SYM { MDBP_R(2802); }          /* Verb clause. Reserved in Oracle */
        ;

keyword_set_special_case:
          NAMES_SYM { MDBP_R(2803); }
        | AUTHORIZATION_SYM { MDBP_R(2804); }
        | ROLE_SYM { MDBP_R(2805); }
        | PASSWORD_SYM { MDBP_R(2806); }
        | PATH_SYM { MDBP_R(2807); }
        ;

keyword_sysvar_type:
          GLOBAL_SYM { MDBP_R(2808); }
        | LOCAL_SYM { MDBP_R(2809); }
        | SESSION_SYM { MDBP_R(2810); }
        ;


/*
  These keywords are generally allowed as identifiers,
  but not allowed as non-delimited SP variable names in sql_mode=ORACLE.
*/
keyword_data_type:
          BIT_SYM { MDBP_R(2811); }
        | BOOLEAN_SYM { MDBP_R(2812); }
        | BOOL_SYM { MDBP_R(2813); }
        | CLOB_MARIADB_SYM { MDBP_R(2814); }
        | CLOB_ORACLE_SYM { MDBP_R(2815); }
        | DATE_SYM           %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(2816); }
        | DATETIME { MDBP_R(2817); }
        | ENUM { MDBP_R(2818); }
        | FIXED_SYM { MDBP_R(2819); }
        | JSON_SYM { MDBP_R(2820); }
        | MEDIUM_SYM { MDBP_R(2821); }
        | NATIONAL_SYM { MDBP_R(2822); }
        | NCHAR_SYM { MDBP_R(2823); }
        | NUMBER_MARIADB_SYM { MDBP_R(2824); }
        | NUMBER_ORACLE_SYM { MDBP_R(2825); }
        | NVARCHAR_SYM { MDBP_R(2826); }
        | RAW_MARIADB_SYM { MDBP_R(2827); }
        | RAW_ORACLE_SYM { MDBP_R(2828); }
        | ROW_SYM { MDBP_R(2829); }
        | SERIAL_SYM { MDBP_R(2830); }
        | TEXT_SYM { MDBP_R(2831); }
        | TIMESTAMP          %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(2832); }
        | TIME_SYM           %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(2833); }
        | VARCHAR2_MARIADB_SYM { MDBP_R(2834); }
        | VARCHAR2_ORACLE_SYM { MDBP_R(2835); }
        | YEAR_SYM { MDBP_R(2836); }
        ;


keyword_cast_type:
          SIGNED_SYM { MDBP_R(2837); }
        ;


keyword_func_sp_var_and_label:
        ACTION { MDBP_R(2838); }
        | ACCOUNT_SYM { MDBP_R(2839); }
        | ADMIN_SYM { MDBP_R(2840); }
        | AFTER_SYM { MDBP_R(2841); }
        | AGAINST { MDBP_R(2842); }
        | AGGREGATE_SYM { MDBP_R(2843); }
        | ALGORITHM_SYM { MDBP_R(2844); }
        | ALWAYS_SYM { MDBP_R(2845); }
        | ARRAY_SYM { MDBP_R(2846); }
        | AT_SYM { MDBP_R(2847); }
        | ATOMIC_SYM { MDBP_R(2848); }
        | AUTHORS_SYM { MDBP_R(2849); }
        | AUTO_INC { MDBP_R(2850); }
        | AUTO_SYM { MDBP_R(2851); }
        | AVG_ROW_LENGTH { MDBP_R(2852); }
        | BLOCK_SYM { MDBP_R(2853); }
        | BODY_MARIADB_SYM { MDBP_R(2854); }
        | BTREE_SYM { MDBP_R(2855); }
        | CASCADED { MDBP_R(2856); }
        | CATALOG_NAME_SYM { MDBP_R(2857); }
        | CHAIN_SYM { MDBP_R(2858); }
        | CHANNEL_SYM { MDBP_R(2859); }
        | CHANGED { MDBP_R(2860); }
        | CIPHER_SYM { MDBP_R(2861); }
        | CLIENT_SYM { MDBP_R(2862); }
        | CLASS_ORIGIN_SYM { MDBP_R(2863); }
        | COALESCE { MDBP_R(2864); }
        | COLLATION_SYM { MDBP_R(2865); }
        | COLUMN_NAME_SYM { MDBP_R(2866); }
        | COLUMNS { MDBP_R(2867); }
        | COMMITTED_SYM { MDBP_R(2868); }
        | COMPACT_SYM { MDBP_R(2869); }
        | COMPLETION_SYM { MDBP_R(2870); }
        | CONCURRENT { MDBP_R(2871); }
        | CONNECTION_SYM { MDBP_R(2872); }
        | CONSISTENT_SYM { MDBP_R(2873); }
        | CONSTRAINT_CATALOG_SYM { MDBP_R(2874); }
        | CONSTRAINT_SCHEMA_SYM { MDBP_R(2875); }
        | CONSTRAINT_NAME_SYM { MDBP_R(2876); }
        | CONTEXT_SYM { MDBP_R(2877); }
        | CONTRIBUTORS_SYM { MDBP_R(2878); }
        | CURRENT_POS_SYM { MDBP_R(2879); }
        | CPU_SYM { MDBP_R(2880); }
        | CUBE_SYM { MDBP_R(2881); }
        /*
          Although a reserved keyword in SQL:2003 (and :2008),
          not reserved in MySQL per WL#2111 specification.
        */
        | CURRENT_SYM { MDBP_R(2882); }
        | CURSOR_NAME_SYM { MDBP_R(2883); }
        | CYCLE_SYM { MDBP_R(2884); }
        | DATA_SYM { MDBP_R(2885); }
        | DATABASE { MDBP_R(2886); }
        | DEFINER_SYM { MDBP_R(2887); }
        | DELAY_KEY_WRITE_SYM { MDBP_R(2888); }
        | DIAGNOSTICS_SYM { MDBP_R(2889); }
        | DISCARD { MDBP_R(2890); }
        | DIRECTORY_SYM { MDBP_R(2891); }
        | DISABLE_SYM { MDBP_R(2892); }
        | DISK_SYM { MDBP_R(2893); }
        | DUMPFILE { MDBP_R(2894); }
        | DUPLICATE_SYM { MDBP_R(2895); }
        | DYNAMIC_SYM { MDBP_R(2896); }
        | ELSEIF_ORACLE_SYM { MDBP_R(2897); }
        | ELSIF_MARIADB_SYM { MDBP_R(2898); }
        | EMPTY_SYM { MDBP_R(2899); }
        | EXPIRE_SYM { MDBP_R(2900); }
        | EXPORT_SYM { MDBP_R(2901); }
        | EXTENDED_SYM { MDBP_R(2902); }
        | EXTENT_SIZE_SYM { MDBP_R(2903); }
        | ENABLE_SYM { MDBP_R(2904); }
        | ENDS_SYM { MDBP_R(2905); }
        | ENGINE_SYM { MDBP_R(2906); }
        | ENGINES_SYM { MDBP_R(2907); }
        | ERROR_SYM { MDBP_R(2908); }
        | ERRORS { MDBP_R(2909); }
        | ESCAPE_SYM { MDBP_R(2910); }
        | EVENT_SYM { MDBP_R(2911); }
        | EVENTS_SYM { MDBP_R(2912); }
        | EVERY_SYM { MDBP_R(2913); }
        | EXCEPTION_MARIADB_SYM { MDBP_R(2914); }
        | EXCHANGE_SYM { MDBP_R(2915); }
        | EXPANSION_SYM { MDBP_R(2916); }
        | FAULTS_SYM { MDBP_R(2917); }
        | FAST_SYM { MDBP_R(2918); }
        | FEDERATED_SYM { MDBP_R(2919); }
        | FILE_SYM { MDBP_R(2920); }
        | FIRST_SYM { MDBP_R(2921); }
        | FOUND_SYM { MDBP_R(2922); }
        | FULL { MDBP_R(2923); }
        | GENERAL { MDBP_R(2924); }
        | GENERATED_SYM { MDBP_R(2925); }
        | GRANTS { MDBP_R(2926); }
        | GOTO_MARIADB_SYM { MDBP_R(2927); }
        | HASH_SYM { MDBP_R(2928); }
        | HARD_SYM { MDBP_R(2929); }
        | HISTORY_SYM { MDBP_R(2930); }
        | HOSTS_SYM { MDBP_R(2931); }
        | IDENTIFIED_SYM { MDBP_R(2932); }
        | IGNORE_SERVER_IDS_SYM { MDBP_R(2933); }
        | INCREMENT_SYM { MDBP_R(2934); }
        | IMMEDIATE_SYM { MDBP_R(2935); }
        | INVOKER_SYM { MDBP_R(2936); }
        | IMPORT { MDBP_R(2937); }
        | INDEXES { MDBP_R(2938); }
        | IO_SYM { MDBP_R(2939); }
        | IPC_SYM { MDBP_R(2940); }
        | ISOLATION { MDBP_R(2941); }
        | ISOPEN_SYM { MDBP_R(2942); }
        | ISSUER_SYM { MDBP_R(2943); }
        | INSERT_METHOD { MDBP_R(2944); }
        | INVISIBLE_SYM { MDBP_R(2945); }
        | JSON_TABLE_SYM { MDBP_R(2946); }
        | KEY_BLOCK_SIZE { MDBP_R(2947); }
        | LAST_SYM { MDBP_R(2948); }
        | LEAVES { MDBP_R(2949); }
        | LESS_SYM { MDBP_R(2950); }
        | LEVEL_SYM { MDBP_R(2951); }
        | LIST_SYM { MDBP_R(2952); }
        | LOCKED_SYM { MDBP_R(2953); }
        | LOCKS_SYM { MDBP_R(2954); }
        | LOG_SYM { MDBP_R(2955); }
        | LOGS_SYM { MDBP_R(2956); }
        | MAX_ROWS { MDBP_R(2957); }
        | MASTER_SYM { MDBP_R(2958); }
        | MASTER_HEARTBEAT_PERIOD_SYM { MDBP_R(2959); }
        | MASTER_GTID_POS_SYM { MDBP_R(2960); }
        | MASTER_HOST_SYM { MDBP_R(2961); }
        | MASTER_PORT_SYM { MDBP_R(2962); }
        | MASTER_LOG_FILE_SYM { MDBP_R(2963); }
        | MASTER_LOG_POS_SYM { MDBP_R(2964); }
        | MASTER_USER_SYM { MDBP_R(2965); }
        | MASTER_USE_GTID_SYM { MDBP_R(2966); }
        | MASTER_PASSWORD_SYM { MDBP_R(2967); }
        | MASTER_SERVER_ID_SYM { MDBP_R(2968); }
        | MASTER_CONNECT_RETRY_SYM { MDBP_R(2969); }
        | MASTER_DELAY_SYM { MDBP_R(2970); }
        | MASTER_RETRY_COUNT_SYM { MDBP_R(2971); }
        | MASTER_SSL_SYM { MDBP_R(2972); }
        | MASTER_SSL_CA_SYM { MDBP_R(2973); }
        | MASTER_SSL_CAPATH_SYM { MDBP_R(2974); }
        | MASTER_SSL_CERT_SYM { MDBP_R(2975); }
        | MASTER_SSL_CIPHER_SYM { MDBP_R(2976); }
        | MASTER_SSL_CRL_SYM { MDBP_R(2977); }
        | MASTER_SSL_CRLPATH_SYM { MDBP_R(2978); }
        | MASTER_SSL_KEY_SYM { MDBP_R(2979); }
        | MASTER_SSL_VERIFY_SERVER_CERT_SYM { MDBP_R(2980); }
        | MAX_CONNECTIONS_PER_HOUR { MDBP_R(2981); }
        | MAX_QUERIES_PER_HOUR { MDBP_R(2982); }
        | MAX_STATEMENT_TIME_SYM { MDBP_R(2983); }
        | MAX_UPDATES_PER_HOUR { MDBP_R(2984); }
        | MAX_USER_CONNECTIONS_SYM { MDBP_R(2985); }
        | MEMORY_SYM { MDBP_R(2986); }
        | MERGE_SYM { MDBP_R(2987); }
        | MESSAGE_TEXT_SYM { MDBP_R(2988); }
        | MICROSECOND_SYM { MDBP_R(2989); }
        | MIGRATE_SYM { MDBP_R(2990); }

        | MINUS_ORACLE_SYM { MDBP_R(2991); }

        | MINVALUE_SYM { MDBP_R(2992); }
        | MIN_ROWS { MDBP_R(2993); }
        | MODIFY_SYM { MDBP_R(2994); }
        | MODE_SYM { MDBP_R(2995); }
        | MONITOR_SYM { MDBP_R(2996); }
        | MUTEX_SYM { MDBP_R(2997); }
        | MYSQL_SYM { MDBP_R(2998); }
        | MYSQL_ERRNO_SYM { MDBP_R(2999); }
        | NAME_SYM { MDBP_R(3000); }
        | NESTED_SYM { MDBP_R(3001); }
        | NEVER_SYM { MDBP_R(3002); }
        | NEXT_SYM           %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(3003); }
        | NOCACHE_SYM { MDBP_R(3004); }
        | NOCYCLE_SYM { MDBP_R(3005); }
        | NOMINVALUE_SYM { MDBP_R(3006); }
        | NOMAXVALUE_SYM { MDBP_R(3007); }
        | NOCOPY_SYM { MDBP_R(3008); }
        | NOWAIT_SYM { MDBP_R(3009); }
        | NODEGROUP_SYM { MDBP_R(3010); }
        | NONE_SYM { MDBP_R(3011); }
        | NOTFOUND_SYM { MDBP_R(3012); }
        | OBJECT_SYM { MDBP_R(3013); }
        | OF_SYM { MDBP_R(3014); }
        | OLD_PASSWORD_SYM { MDBP_R(3015); }
        | ONE_SYM { MDBP_R(3016); }
        | ONLINE_SYM { MDBP_R(3017); }
        | ONLY_SYM { MDBP_R(3018); }
        | ORDINALITY_SYM { MDBP_R(3019); }
        | PACKAGE_MARIADB_SYM %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(3020); }
        | PACK_KEYS_SYM { MDBP_R(3021); }
        | PAGE_SYM { MDBP_R(3022); }
        | PARTIAL { MDBP_R(3023); }
        | PARTITIONING_SYM { MDBP_R(3024); }
        | PARTITIONS_SYM { MDBP_R(3025); }
        | PERSISTENT_SYM { MDBP_R(3026); }
        | PHASE_SYM { MDBP_R(3027); }
        | PLUGIN_SYM { MDBP_R(3028); }
        | PLUGINS_SYM { MDBP_R(3029); }
        | PRESERVE_SYM { MDBP_R(3030); }
        | PREV_SYM { MDBP_R(3031); }
        | PREVIOUS_SYM       %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(3032); }
        | PRIVILEGES { MDBP_R(3033); }
        | PROCESS { MDBP_R(3034); }
        | PROCESSLIST_SYM { MDBP_R(3035); }
        | PROFILE_SYM { MDBP_R(3036); }
        | PROFILES_SYM { MDBP_R(3037); }
        | PROXY_SYM { MDBP_R(3038); }
        | QUARTER_SYM { MDBP_R(3039); }
        | QUERY_SYM { MDBP_R(3040); }
        | QUICK { MDBP_R(3041); }
        | RAISE_MARIADB_SYM { MDBP_R(3042); }
        | READ_ONLY_SYM { MDBP_R(3043); }
        | REBUILD_SYM { MDBP_R(3044); }
        | RECOVER_SYM { MDBP_R(3045); }
        | REDUNDANT_SYM { MDBP_R(3046); }
        | RELAY { MDBP_R(3047); }
        | RELAY_LOG_FILE_SYM { MDBP_R(3048); }
        | RELAY_LOG_POS_SYM { MDBP_R(3049); }
        | RELAY_THREAD { MDBP_R(3050); }
        | RELAYLOG_SYM { MDBP_R(3051); }
        | RELOAD { MDBP_R(3052); }
        | REORGANIZE_SYM { MDBP_R(3053); }
        | REPEATABLE_SYM { MDBP_R(3054); }
        | REPLAY_SYM { MDBP_R(3055); }
        | REPLICATION { MDBP_R(3056); }
        | RESOURCES { MDBP_R(3057); }
        | RESTART_SYM { MDBP_R(3058); }
        | RESUME_SYM { MDBP_R(3059); }
        | RETURNED_SQLSTATE_SYM { MDBP_R(3060); }
        | RETURNS_SYM { MDBP_R(3061); }
        | REUSE_SYM { MDBP_R(3062); }
        | REVERSE_SYM { MDBP_R(3063); }
        | ROLLUP_SYM { MDBP_R(3064); }
        | ROUTINE_SYM { MDBP_R(3065); }
        | ROW_COUNT_SYM { MDBP_R(3066); }
        | ROWCOUNT_SYM { MDBP_R(3067); }
        | ROWTYPE_MARIADB_SYM { MDBP_R(3068); }
        | ROW_FORMAT_SYM { MDBP_R(3069); }
        | RTREE_SYM { MDBP_R(3070); }
        | SCALAR_SYM { MDBP_R(3071); }
        | SCHEDULE_SYM { MDBP_R(3072); }
        | SCHEMA_NAME_SYM { MDBP_R(3073); }
        | SEQUENCE_SYM { MDBP_R(3074); }
        | SERIALIZABLE_SYM { MDBP_R(3075); }
        | SIMPLE_SYM { MDBP_R(3076); }
        | SHARE_SYM { MDBP_R(3077); }
        | SKIP_SYM { MDBP_R(3078); }
        | SLAVE_POS_SYM { MDBP_R(3079); }
        | SLOW { MDBP_R(3080); }
        | SNAPSHOT_SYM { MDBP_R(3081); }
        | SOFT_SYM { MDBP_R(3082); }
        | SOURCE_SYM { MDBP_R(3083); }
        | SQL_CACHE_SYM { MDBP_R(3084); }
        | SQL_BUFFER_RESULT { MDBP_R(3085); }
        | SQL_NO_CACHE_SYM { MDBP_R(3086); }
        | SQL_THREAD { MDBP_R(3087); }
        | STAGE_SYM { MDBP_R(3088); }
        | STARTS_SYM { MDBP_R(3089); }
        | STATEMENT_SYM { MDBP_R(3090); }
        | STATUS_SYM { MDBP_R(3091); }
        | STORAGE_SYM { MDBP_R(3092); }
        | STRING_SYM { MDBP_R(3093); }
        | SUBCLASS_ORIGIN_SYM { MDBP_R(3094); }
        | SUBJECT_SYM { MDBP_R(3095); }
        | SUBPARTITION_SYM { MDBP_R(3096); }
        | SUBPARTITIONS_SYM { MDBP_R(3097); }
        | SUPER_SYM { MDBP_R(3098); }
        | SUSPEND_SYM { MDBP_R(3099); }
        | SWAPS_SYM { MDBP_R(3100); }
        | SWITCHES_SYM { MDBP_R(3101); }
        | SYSTEM { MDBP_R(3102); }
        | SYSTEM_TIME_SYM { MDBP_R(3103); }
        | TABLE_NAME_SYM { MDBP_R(3104); }
        | TABLES { MDBP_R(3105); }
        | TABLE_CHECKSUM_SYM { MDBP_R(3106); }
        | TABLESPACE { MDBP_R(3107); }
        | TEMPORARY { MDBP_R(3108); }
        | TEMPTABLE_SYM { MDBP_R(3109); }
        | THAN_SYM { MDBP_R(3110); }
        | TRANSACTION_SYM    %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(3111); }
        | TRANSACTIONAL_SYM { MDBP_R(3112); }
        | THREADS_SYM { MDBP_R(3113); }
        | TRIGGERS_SYM { MDBP_R(3114); }

        | TYPE_SYM           %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(3115); }

        | UNCOMMITTED_SYM { MDBP_R(3116); }
        | UNDEFINED_SYM { MDBP_R(3117); }
        | UNKNOWN_SYM { MDBP_R(3118); }
        | UNTIL_SYM { MDBP_R(3119); }
        | USE_FRM { MDBP_R(3120); }
        | VARIABLES { MDBP_R(3121); }
        | VERSIONING_SYM { MDBP_R(3122); }
        | VIEW_SYM { MDBP_R(3123); }
        | VIRTUAL_SYM { MDBP_R(3124); }
        | VISIBLE_SYM { MDBP_R(3125); }
        | VALIDATION_SYM { MDBP_R(3126); }
        | WARNINGS { MDBP_R(3127); }
        | WAIT_SYM { MDBP_R(3128); }
        | WITHOUT { MDBP_R(3129); }
        | WORK_SYM { MDBP_R(3130); }
        | X509_SYM { MDBP_R(3131); }
        | XML_SYM { MDBP_R(3132); }
        | VIA_SYM { MDBP_R(3133); }
        | WEEK_SYM { MDBP_R(3134); }
        ;

keyword_func_sp_var_not_label:
        FORMAT_SYM { MDBP_R(3135); }
        | COLUMN_CHECK_SYM { MDBP_R(3136); }
        ;
/*
  These keywords are fine for both SP variable names and SP labels.
*/
keyword_sp_var_and_label:
        keyword_func_sp_var_and_label { MDBP_R(3137); }
        | ALLOW_SYM { MDBP_R(5858); }
        | APPS_SYM { MDBP_R(5859); }
        | APP_SYM { MDBP_R(5860); }
        | AUTHENTICATION_SYM { MDBP_R(5861); }
        | AUTH_SYM { MDBP_R(5862); }
        | AUTODETECT_SYM { MDBP_R(5863); }
        | AVAILABLE_SYM { MDBP_R(5864); }
        | CLASS_SYM { MDBP_R(5865); }
        | CONTENT_SYM { MDBP_R(5866); }
        | DAEMONS_SYM { MDBP_R(5867); }
        | DAEMON_SYM { MDBP_R(5868); }
        | DEVELOPMENT_SYM { MDBP_R(5869); }
        | DISABLED_SYM { MDBP_R(5870); }
        | ENABLED_SYM { MDBP_R(5871); }
        | ENDPOINTS_SYM { MDBP_R(5872); }
        | EXTENDS_SYM { MDBP_R(5873); }
        | FEED_SYM { MDBP_R(5874); }
        | FILES_SYM { MDBP_R(5875); }
        | FILTER_SYM { MDBP_R(5876); }
        | FUNCTIONS_SYM { MDBP_R(5877); }
        | HIERARCHY_SYM { MDBP_R(5878); }
        | HTTPS_SYM { MDBP_R(5879); }
        | HTTP_SYM { MDBP_R(5880); }
        | INCLUDING_SYM { MDBP_R(5881); }
        | ITEMS_SYM { MDBP_R(5882); }
        | ITEM_SYM { MDBP_R(5883); }
        | MANAGEMENT_SYM { MDBP_R(5884); }
        | MAPPING_SYM { MDBP_R(5885); }
        | MARIADB_SYM { MDBP_R(5886); }
        | MEDIA_SYM { MDBP_R(5887); }
        | METADATA_SYM { MDBP_R(5888); }
        | MRS_SYM { MDBP_R(5889); }
        | NEW_SYM { MDBP_R(5890); }
        | PARAMETERS_SYM { MDBP_R(5891); }
        | PER_SYM { MDBP_R(5892); }
        | PRIVATE_SYM { MDBP_R(5893); }
        | PROCEDURES_SYM { MDBP_R(5894); }
        | PROTOCOL_SYM { MDBP_R(5895); }
        | PUBLISHED_SYM { MDBP_R(5896); }
        | REDIRECTION_SYM { MDBP_R(5897); }
        | REGISTER_SYM { MDBP_R(5898); }
        | REQUEST_SYM { MDBP_R(5899); }
        | REQUIRED_SYM { MDBP_R(5900); }
        | REST_SYM { MDBP_R(5901); }
        | RESULT_SYM { MDBP_R(5902); }
        | ROLES_SYM { MDBP_R(5903); }
        | SCRIPTS_SYM { MDBP_R(5904); }
        | SECRET_SYM { MDBP_R(5905); }
        | SERVICES_SYM { MDBP_R(5906); }
        | SERVICE_SYM { MDBP_R(5907); }
        | SETS_SYM { MDBP_R(5908); }
        | STATIC_SYM { MDBP_R(5909); }
        | TYPESCRIPT_SYM { MDBP_R(5910); }
        | UNPUBLISHED_SYM { MDBP_R(5911); }
        | URL_SYM { MDBP_R(5912); }
        | USERS_SYM { MDBP_R(5913); }
        | VENDORS_SYM { MDBP_R(5914); }
        | VENDOR_SYM { MDBP_R(5915); }
        | VIEWS_SYM { MDBP_R(5916); }
        | ADDDATE_SYM { MDBP_R(3138); }
        | ANY_SYM { MDBP_R(3139); }
        | AVG_SYM { MDBP_R(3140); }
        | CODE_SYM { MDBP_R(3141); }
        | DAY_SYM { MDBP_R(3142); }
        | GET_FORMAT { MDBP_R(3143); }
        | HOUR_SYM { MDBP_R(3144); }
        | ID_SYM { MDBP_R(3145); }
        | LAST_VALUE { MDBP_R(3146); }
        | LASTVAL_SYM { MDBP_R(3147); }
        | MINUTE_SYM { MDBP_R(3148); }
        | MONTH_SYM { MDBP_R(3149); }
        | NEXTVAL_SYM { MDBP_R(3150); }
        | NUMTODSINTERVAL_SYM { MDBP_R(3151); }
        | NUMTOYMINTERVAL_SYM { MDBP_R(3152); }
        | OLD_VALUE_SYM { MDBP_R(3153); }
        | OVERLAPS_SYM { MDBP_R(3154); }
        | RECORD_SYM { MDBP_R(3155); }

        | ROWNUM_SYM { MDBP_R(3156); }

        | SECOND_SYM { MDBP_R(3157); }
        | SETVAL_SYM { MDBP_R(3158); }
        | SOUNDS_SYM { MDBP_R(3159); }
        | SUBDATE_SYM { MDBP_R(3160); }

        | SYSDATE { MDBP_R(3161); }

        | TRIM_ORACLE { MDBP_R(3162); }
        | TIMESTAMP_ADD { MDBP_R(3163); }
        | TIMESTAMP_DIFF { MDBP_R(3164); }
        | USER_SYM           %prec PREC_BELOW_CONTRACTION_TOKEN2 { MDBP_R(3165); }
        | VALUE_SYM { MDBP_R(3166); }
        | WEIGHT_STRING_SYM { MDBP_R(3167); }
        | SESSION_USER_SYM { MDBP_R(3168); }
        ;


reserved_keyword_udt_not_param_type:
          ADD { MDBP_R(3169); }
        | ALL { MDBP_R(3170); }
        | ALTER { MDBP_R(3171); }
        | ANALYZE_SYM { MDBP_R(3172); }
        | AND_SYM { MDBP_R(3173); }
        | AS { MDBP_R(3174); }
        | ASC { MDBP_R(3175); }
        | ASENSITIVE_SYM { MDBP_R(3176); }
        | BEFORE_SYM { MDBP_R(3177); }
        | BETWEEN_SYM { MDBP_R(3178); }
        | BIT_AND { MDBP_R(3179); }
        | BIT_OR { MDBP_R(3180); }
        | BIT_XOR { MDBP_R(3181); }
        | BODY_ORACLE_SYM { MDBP_R(3182); }
        | BOTH { MDBP_R(3183); }
        | BY { MDBP_R(3184); }
        | CALL_SYM { MDBP_R(3185); }
        | CASCADE { MDBP_R(3186); }
        | CASE_SYM { MDBP_R(3187); }
        | CAST_SYM { MDBP_R(3188); }
        | CHANGE { MDBP_R(3189); }
        | CHECK_SYM { MDBP_R(3190); }
        | COLLATE_SYM { MDBP_R(3191); }
        | CONSTRAINT { MDBP_R(3192); }
        | CONTINUE_MARIADB_SYM { MDBP_R(3193); }
        | CONTINUE_ORACLE_SYM { MDBP_R(3194); }
        | CONVERT_SYM { MDBP_R(3195); }
        | COUNT_SYM { MDBP_R(3196); }
        | CREATE { MDBP_R(3197); }
        | CROSS { MDBP_R(3198); }
        | CUME_DIST_SYM { MDBP_R(3199); }
        | CURDATE { MDBP_R(3200); }
        | CURRENT_PATH { MDBP_R(3201); }
        | CURRENT_USER { MDBP_R(3202); }
        | CURRENT_ROLE { MDBP_R(3203); }
        | CURTIME { MDBP_R(3204); }
        | DATABASES { MDBP_R(3205); }
        | DATE_ADD_INTERVAL { MDBP_R(3206); }
        | DATE_SUB_INTERVAL { MDBP_R(3207); }
        | DAY_HOUR_SYM { MDBP_R(3208); }
        | DAY_MICROSECOND_SYM { MDBP_R(3209); }
        | DAY_MINUTE_SYM { MDBP_R(3210); }
        | DAY_SECOND_SYM { MDBP_R(3211); }
        | DECLARE_MARIADB_SYM { MDBP_R(3212); }
        | DECLARE_ORACLE_SYM { MDBP_R(3213); }
        | DEFAULT { MDBP_R(3214); }
        | DELETE_DOMAIN_ID_SYM { MDBP_R(3215); }
        | DELETE_SYM { MDBP_R(3216); }
        | DENSE_RANK_SYM { MDBP_R(3217); }
        | DESC { MDBP_R(3218); }
        | DESCRIBE { MDBP_R(3219); }
        | DETERMINISTIC_SYM { MDBP_R(3220); }
        | DISTINCT { MDBP_R(3221); }
        | DIV_SYM { MDBP_R(3222); }
        | DO_DOMAIN_IDS_SYM { MDBP_R(3223); }
        | DROP { MDBP_R(3224); }
        | DUAL_SYM { MDBP_R(3225); }
        | EACH_SYM { MDBP_R(3226); }
        | ELSE { MDBP_R(3227); }
        | ELSEIF_MARIADB_SYM { MDBP_R(3228); }
        | ELSIF_ORACLE_SYM { MDBP_R(3229); }
        | ENCLOSED { MDBP_R(3230); }
        | ESCAPED { MDBP_R(3231); }
        | EXCEPT_SYM { MDBP_R(3232); }
        | EXISTS { MDBP_R(3233); }
        | EXTRACT_SYM { MDBP_R(3234); }
        | FALSE_SYM { MDBP_R(3235); }
        | FETCH_SYM { MDBP_R(3236); }
        | FIRST_VALUE_SYM { MDBP_R(3237); }
        | FOREIGN { MDBP_R(3238); }
        | FROM { MDBP_R(3239); }
        | FULLTEXT_SYM { MDBP_R(3240); }
        | GOTO_ORACLE_SYM { MDBP_R(3241); }
        | GRANT { MDBP_R(3242); }
        | GROUP_SYM { MDBP_R(3243); }
        | GROUP_CONCAT_SYM { MDBP_R(3244); }
        | LAG_SYM { MDBP_R(3245); }
        | LEAD_SYM { MDBP_R(3246); }
        | HAVING { MDBP_R(3247); }
        | HOUR_MICROSECOND_SYM { MDBP_R(3248); }
        | HOUR_MINUTE_SYM { MDBP_R(3249); }
        | HOUR_SECOND_SYM { MDBP_R(3250); }
        | IF_SYM { MDBP_R(3251); }
        | IGNORE_DOMAIN_IDS_SYM { MDBP_R(3252); }
        | IGNORE_SYM { MDBP_R(3253); }
        | IGNORED_SYM { MDBP_R(3254); }
        | INDEX_SYM { MDBP_R(3255); }
        | INFILE { MDBP_R(3256); }
        | INNER_SYM { MDBP_R(3257); }
        | INSENSITIVE_SYM { MDBP_R(3258); }
        | INSERT { MDBP_R(3259); }
        | INTERSECT_SYM { MDBP_R(3260); }
        | INTERVAL_SYM { MDBP_R(3261); }
        | INTO { MDBP_R(3262); }
        | IS { MDBP_R(3263); }
        | ITERATE_SYM { MDBP_R(3264); }
        | JOIN_SYM { MDBP_R(3265); }
        | KEYS { MDBP_R(3266); }
        | KEY_SYM { MDBP_R(3267); }
        | KILL_SYM { MDBP_R(3268); }
        | LEADING { MDBP_R(3269); }
        | LEAVE_SYM { MDBP_R(3270); }
        | LEFT { MDBP_R(3271); }
        | LIKE { MDBP_R(3272); }
        | LIMIT { MDBP_R(3273); }
        | LINEAR_SYM { MDBP_R(3274); }
        | LINES { MDBP_R(3275); }
        | LOAD { MDBP_R(3276); }
        | LOCATOR_SYM { MDBP_R(3277); }
        | LOCK_SYM { MDBP_R(3278); }
        | LOOP_SYM { MDBP_R(3279); }
        | LOW_PRIORITY { MDBP_R(3280); }
        | MATCH { MDBP_R(3281); }
        | MAX_SYM { MDBP_R(3282); }
        | MAXVALUE_SYM { MDBP_R(3283); }
        | MEDIAN_SYM { MDBP_R(3284); }
        | MINUTE_MICROSECOND_SYM { MDBP_R(3285); }
        | MINUTE_SECOND_SYM { MDBP_R(3286); }
        | MIN_SYM { MDBP_R(3287); }



        | MODIFIES_SYM { MDBP_R(3288); }
        | MOD_SYM { MDBP_R(3289); }
        | NATURAL { MDBP_R(3290); }
        | NEG { MDBP_R(3291); }
        | NOT_SYM { MDBP_R(3292); }
        | NOW_SYM { MDBP_R(3293); }
        | NO_WRITE_TO_BINLOG { MDBP_R(3294); }
        | NTILE_SYM { MDBP_R(3295); }
        | NULL_SYM { MDBP_R(3296); }
        | NTH_VALUE_SYM { MDBP_R(3297); }
        | ON { MDBP_R(3298); }
        | OPTIMIZE { MDBP_R(3299); }
        | OPTIONALLY { MDBP_R(3300); }
        | ORDER_SYM { MDBP_R(3301); }
        | OR_SYM { MDBP_R(3302); }
        | OTHERS_ORACLE_SYM { MDBP_R(3303); }
        | OUTER { MDBP_R(3304); }
        | OUTFILE { MDBP_R(3305); }
        | OVER_SYM { MDBP_R(3306); }
        | PACKAGE_ORACLE_SYM { MDBP_R(3307); }
        | PAGE_CHECKSUM_SYM { MDBP_R(3308); }
        | PARSE_VCOL_EXPR_SYM { MDBP_R(3309); }
        | PARTITION_SYM { MDBP_R(3310); }
        | PERCENT_RANK_SYM { MDBP_R(3311); }
        | PERCENTILE_CONT_SYM { MDBP_R(3312); }
        | PERCENTILE_DISC_SYM { MDBP_R(3313); }
        | PORTION_SYM { MDBP_R(3314); }
        | POSITION_SYM { MDBP_R(3315); }
        | PRECISION { MDBP_R(3316); }
        | PRIMARY_SYM { MDBP_R(3317); }
        | PROCEDURE_SYM { MDBP_R(3318); }
        | PURGE { MDBP_R(3319); }
        | RAISE_ORACLE_SYM { MDBP_R(3320); }
        | RANGE_SYM { MDBP_R(3321); }
        | RANK_SYM { MDBP_R(3322); }
        | READS_SYM { MDBP_R(3323); }
        | READ_SYM { MDBP_R(3324); }
        | READ_WRITE_SYM { MDBP_R(3325); }
        | RECURSIVE_SYM { MDBP_R(3326); }
        | REF_SYM { MDBP_R(3327); }
        | REF_SYSTEM_ID_SYM { MDBP_R(3328); }
        | REFERENCES { MDBP_R(3329); }
        | REGEXP { MDBP_R(3330); }
        | RELEASE_SYM { MDBP_R(3331); }
        | RENAME { MDBP_R(3332); }
        | REPEAT_SYM { MDBP_R(3333); }
        | REPLACE { MDBP_R(3334); }
        | REQUIRE_SYM { MDBP_R(3335); }
        | RESIGNAL_SYM { MDBP_R(3336); }
        | RESTRICT { MDBP_R(3337); }
        | RETURNING_SYM { MDBP_R(3338); }
        | RETURN_MARIADB_SYM { MDBP_R(3339); }
        | RETURN_ORACLE_SYM { MDBP_R(3340); }
        | REVOKE { MDBP_R(3341); }
        | RIGHT { MDBP_R(3342); }
        | ROWS_SYM { MDBP_R(3343); }
        | ROWTYPE_ORACLE_SYM { MDBP_R(3344); }
        | ROW_NUMBER_SYM { MDBP_R(3345); }
        | SECOND_MICROSECOND_SYM { MDBP_R(3346); }
        | SELECT_SYM { MDBP_R(3347); }
        | SENSITIVE_SYM { MDBP_R(3348); }
        | SEPARATOR_SYM { MDBP_R(3349); }
        | SHOW { MDBP_R(3350); }
        | SIGNAL_SYM { MDBP_R(3351); }
        | SPATIAL_SYM { MDBP_R(3352); }
        | SPECIFIC_SYM { MDBP_R(3353); }
        | SQLEXCEPTION_SYM { MDBP_R(3354); }
        | SQLSTATE_SYM { MDBP_R(3355); }
        | SQLWARNING_SYM { MDBP_R(3356); }
        | SQL_BIG_RESULT { MDBP_R(3357); }
        | SQL_SMALL_RESULT { MDBP_R(3358); }
        | SQL_SYM { MDBP_R(3359); }
        | SSL_SYM { MDBP_R(3360); }
        | STARTING { MDBP_R(3361); }
        | STATS_AUTO_RECALC_SYM { MDBP_R(3362); }
        | STATS_PERSISTENT_SYM { MDBP_R(3363); }
        | STATS_SAMPLE_PAGES_SYM { MDBP_R(3364); }
        | STDDEV_SAMP_SYM { MDBP_R(3365); }
        | STD_SYM { MDBP_R(3366); }
        | STRAIGHT_JOIN { MDBP_R(3367); }
        | SUBSTRING { MDBP_R(3368); }
        | SUM_SYM { MDBP_R(3369); }
        | TABLE_SYM { MDBP_R(3370); }
        | TERMINATED { MDBP_R(3371); }
        | THEN_SYM { MDBP_R(3372); }
        | TO_SYM { MDBP_R(3373); }
        | TRAILING { MDBP_R(3374); }
        | TRIGGER_SYM { MDBP_R(3375); }
        | TRIM { MDBP_R(3376); }
        | TRUE_SYM { MDBP_R(3377); }
        | UNDO_SYM { MDBP_R(3378); }
        | UNION_SYM { MDBP_R(3379); }
        | UNIQUE_SYM { MDBP_R(3380); }
        | UNLOCK_SYM { MDBP_R(3381); }
        | UPDATE_SYM { MDBP_R(3382); }
        | USAGE { MDBP_R(3383); }
        | USE_SYM { MDBP_R(3384); }
        | USING { MDBP_R(3385); }
        | UTC_DATE_SYM { MDBP_R(3386); }
        | UTC_TIMESTAMP_SYM { MDBP_R(3387); }
        | UTC_TIME_SYM { MDBP_R(3388); }
        | VALUES { MDBP_R(3389); }
        | VALUES_IN_SYM { MDBP_R(3390); }
        | VALUES_LESS_SYM { MDBP_R(3391); }
        | VARIANCE_SYM { MDBP_R(3392); }
        | VARYING { MDBP_R(3393); }
        | VAR_SAMP_SYM { MDBP_R(3394); }
        | VECTOR_SYM { MDBP_R(3395); }
        | WHEN_SYM { MDBP_R(3396); }
        | WHERE { MDBP_R(3397); }
        | WHILE_SYM { MDBP_R(3398); }
        | WITH { MDBP_R(3399); }
        | XOR { MDBP_R(3400); }
        | YEAR_MONTH_SYM { MDBP_R(3401); }
        | ZEROFILL { MDBP_R(3402); }
        ;

/*
  SQLCOM_SET_OPTION statement.

  Note that to avoid shift/reduce conflicts, we have separate rules for the
  first option listed in the statement.
*/

set:
          SET
          { MDBP_R(3403); }



          set_param
          { MDBP_R(3404); }



        ;

set_param:
          option_value_no_option_type { MDBP_R(3405); }
        | option_value_no_option_type ',' option_value_list { MDBP_R(3406); }
        | TRANSACTION_SYM
          { MDBP_R(3407); }




          transaction_characteristics
          { MDBP_R(3408); }



        | SESSION_SYM AUTHORIZATION_SYM user_name
          { MDBP_R(3409); }












        | option_type
          { MDBP_R(3410); }


          start_option_value_list_following_option_type { MDBP_R(3411); }
        | STATEMENT_SYM
          set_stmt_option_list
          { MDBP_R(3412); }








          FOR_SYM directly_executable_statement { MDBP_R(3413); }
        | PATH_SYM
          { MDBP_R(3414); }








          set_expr_or_default
          { MDBP_R(3415); }







        ;

set_stmt_option_list:
       /*
         Only system variables can be used here. If this condition is changed
         please check careful code under lex->option_type == OPT_STATEMENT
         condition on wrong type casts.
       */
          set_stmt_option { MDBP_R(3416); }
        | set_stmt_option_list ',' set_stmt_option { MDBP_R(3417); }
        ;

/* Start of option value list, option_type was given */
start_option_value_list_following_option_type:
          option_value_following_option_type { MDBP_R(3418); }
        | option_value_following_option_type ',' option_value_list { MDBP_R(3419); }
        | TRANSACTION_SYM
          { MDBP_R(3420); }



          transaction_characteristics
          { MDBP_R(3421); }



        ;

/* Repeating list of option values after first option value. */
option_value_list:
          option_value { MDBP_R(3422); }
        | option_value_list ',' option_value { MDBP_R(3423); }
        ;

/* Wrapper around option values following the first option value in the stmt. */
option_value:
          option_type
          { MDBP_R(3424); }


          option_value_following_option_type { MDBP_R(3425); }
        | option_value_no_option_type { MDBP_R(3426); }
        ;

option_type:
          GLOBAL_SYM  { MDBP_R(3427); }
        | LOCAL_SYM   { MDBP_R(3428); }
        | SESSION_SYM { MDBP_R(3429); }
        ;

opt_var_type:
          /* empty */ { MDBP_R(3430); }
        | GLOBAL_SYM  { MDBP_R(3431); }
        | LOCAL_SYM   { MDBP_R(3432); }
        | SESSION_SYM { MDBP_R(3433); }
        ;

opt_var_ident_type:
          /* empty */     { MDBP_R(3434); }
        | GLOBAL_SYM '.'  { MDBP_R(3435); }
        | LOCAL_SYM '.'   { MDBP_R(3436); }
        | SESSION_SYM '.' { MDBP_R(3437); }
        ;

/*
  SET STATEMENT options do not need their own LEX or Query_arena.
  Let's put them to the main ones.
*/
set_stmt_option:
          ident_cli equal
          { MDBP_R(3438); }



          set_expr_or_default
          { MDBP_R(3439); }







        | ident_cli '.' ident equal
          { MDBP_R(3440); }



          set_expr_or_default
          { MDBP_R(3441); }







        | DEFAULT '.' ident equal
          { MDBP_R(3442); }



          set_expr_or_default
          { MDBP_R(3443); }





        ;


/* Option values with preceding option_type. */
option_value_following_option_type:
          ident_cli equal
          { MDBP_R(3444); }



          set_expr_or_default
          { MDBP_R(3445); }







        | ident_cli '.' ident equal
          { MDBP_R(3446); }



          set_expr_or_default
          { MDBP_R(3447); }







        | DEFAULT '.' ident equal
          { MDBP_R(3448); }



          set_expr_or_default
          { MDBP_R(3449); }





        ;

/* Option values without preceding option_type. */
option_value_no_option_type:
          ident_cli_set_usual_case equal
          { MDBP_R(3450); }



          set_expr_or_default
          { MDBP_R(3451); }







        | ident_cli_set_usual_case '.' ident equal
          { MDBP_R(3452); }



          set_expr_or_default
          { MDBP_R(3453); }







        | DEFAULT '.' ident equal
          { MDBP_R(3454); }



          set_expr_or_default
          { MDBP_R(3455); }






        | '@' ident_or_text equal
          { MDBP_R(3456); }









          remember_cpp_ptr expr remember_end
          { MDBP_R(3457); }












        | '@' '@' opt_var_ident_type ident_sysvar_name equal
          { MDBP_R(3458); }



          set_expr_or_default
          { MDBP_R(3459); }




        | '@' '@' opt_var_ident_type ident_sysvar_name '.' ident equal
          { MDBP_R(3460); }



          set_expr_or_default
          { MDBP_R(3461); }





        | '@' '@' opt_var_ident_type DEFAULT '.' ident equal
          { MDBP_R(3462); }



          set_expr_or_default
          { MDBP_R(3463); }




        | charset old_or_new_charset_name_or_default
          { MDBP_R(3464); }
















        | NAMES_SYM equal expr
          { MDBP_R(3465); }









        | NAMES_SYM charset_name_or_default
          { MDBP_R(3466); }





        | NAMES_SYM charset_name_or_default
                    COLLATE_SYM collation_name_or_default
          { MDBP_R(3467); }



        | DEFAULT ROLE_SYM grant_role
          { MDBP_R(3468); }




















        | DEFAULT ROLE_SYM grant_role FOR_SYM user
          { MDBP_R(3469); }














        | ROLE_SYM role_name
          { MDBP_R(3470); }









        | ROLE_SYM equal
          { MDBP_R(3471); }



          set_expr_or_default
          { MDBP_R(3472); }







        | PASSWORD_SYM equal
          { MDBP_R(3473); }



          text_or_password
          { MDBP_R(3474); }




        | PASSWORD_SYM FOR_SYM
          { MDBP_R(3475); }



          user equal text_or_password
          { MDBP_R(3476); }




        ;

transaction_characteristics:
          transaction_access_mode { MDBP_R(3477); }
        | isolation_level { MDBP_R(3478); }
        | transaction_access_mode ',' isolation_level { MDBP_R(3479); }
        | isolation_level ',' transaction_access_mode { MDBP_R(3480); }
        ;

transaction_access_mode:
          transaction_access_mode_types
          { MDBP_R(3481); }














        ;

isolation_level:
          ISOLATION LEVEL_SYM isolation_types
          { MDBP_R(3482); }













        ;

transaction_access_mode_types:
          READ_SYM ONLY_SYM { MDBP_R(3483); }
        | READ_SYM WRITE_SYM { MDBP_R(3484); }
        ;

isolation_types:
          READ_SYM UNCOMMITTED_SYM { MDBP_R(3485); }
        | READ_SYM COMMITTED_SYM   { MDBP_R(3486); }
        | REPEATABLE_SYM READ_SYM  { MDBP_R(3487); }
        | SERIALIZABLE_SYM         { MDBP_R(3488); }
        ;


text_or_password:
          TEXT_STRING
          { MDBP_R(3489); }



        | PASSWORD_SYM '(' TEXT_STRING ')'
          { MDBP_R(3490); }



        | OLD_PASSWORD_SYM '(' TEXT_STRING ')'
          { MDBP_R(3491); }







        ;

set_expr_or_default:
          remember_cpp_ptr expr remember_end
          { MDBP_R(3492); }











        | remember_cpp_ptr set_expr_misc remember_end
          { MDBP_R(3493); }




        | remember_cpp_ptr DEFAULT remember_end
          { MDBP_R(3494); }


        ;

set_expr_misc:
          ON     { MDBP_R(3495); }
        | ALL    { MDBP_R(3496); }
        | BINARY { MDBP_R(3497); }
        ;

/* Lock function */

lock:
          LOCK_SYM table_or_tables
          { MDBP_R(3498); }






          table_lock_list opt_lock_wait_timeout
          { MDBP_R(3499); }
        ;

opt_lock_wait_timeout:
        /* empty */
        { MDBP_R(3500); }
        | WAIT_SYM ulong_num
        { MDBP_R(3501); }




        | NOWAIT_SYM
        { MDBP_R(3502); }




      ;

table_or_tables:
          TABLE_SYM        { MDBP_R(3503); }
        | TABLES           { MDBP_R(3504); }
        ;

table_lock_list:
          table_lock { MDBP_R(3505); }
        | table_lock_list ',' table_lock { MDBP_R(3506); }
        ;

table_lock:
          table_ident opt_table_alias_clause lock_option
          { MDBP_R(3507); }














        ;

lock_option:
          READ_SYM               { MDBP_R(3508); }
        | WRITE_SYM              { MDBP_R(3509); }
        | WRITE_SYM CONCURRENT
          { MDBP_R(3510); }



        | LOW_PRIORITY WRITE_SYM { MDBP_R(3511); }
        | READ_SYM LOCAL_SYM     { MDBP_R(3512); }
        ;

unlock:
          UNLOCK_SYM
          { MDBP_R(3513); }






          table_or_tables
          { MDBP_R(3514); }
        ;

/*
** Handler: direct access to ISAM functions
*/

handler:
          HANDLER_SYM
          { MDBP_R(3515); }



          handler_tail
          { MDBP_R(3516); }


        ;

handler_tail:
          table_ident OPEN_SYM opt_table_alias_clause
          { MDBP_R(3517); }







        | table_ident_nodb CLOSE_SYM
          { MDBP_R(3518); }







        | table_ident_nodb READ_SYM
          { MDBP_R(3519); }
















          handler_read_or_scan opt_where_clause opt_global_limit_clause
          { MDBP_R(3520); }




















        ;

handler_read_or_scan:
          handler_scan_function       { MDBP_R(3521); }
        | ident handler_rkey_function { MDBP_R(3522); }
        ;

handler_scan_function:
          FIRST_SYM { MDBP_R(3523); }
        | NEXT_SYM  { MDBP_R(3524); }
        ;

handler_rkey_function:
          FIRST_SYM { MDBP_R(3525); }
        | NEXT_SYM  { MDBP_R(3526); }
        | PREV_SYM  { MDBP_R(3527); }
        | LAST_SYM  { MDBP_R(3528); }
        | handler_rkey_mode
          { MDBP_R(3529); }






          '(' values ')'
          { MDBP_R(3530); }
        ;

handler_rkey_mode:
          '='     { MDBP_R(3531); }
        | GE     { MDBP_R(3532); }
        | LE     { MDBP_R(3533); }
        | '>' { MDBP_R(3534); }
        | '<'     { MDBP_R(3535); }
        ;

/* GRANT / REVOKE */

revoke:
          REVOKE clear_privileges revoke_privileges_or_proxy
          { MDBP_R(3536); }

        |
          REVOKE DENY clear_privileges revoke_privileges
          { MDBP_R(3537); }


        | REVOKE clear_privileges ALL opt_privileges ',' GRANT OPTION FROM user_and_role_list
          { MDBP_R(3538); }


        | REVOKE clear_privileges revoke_role
          { MDBP_R(3539); }
        ;

revoke_role:
          admin_option_for_role FROM user_and_role_list
          { MDBP_R(3540); }




        ;

revoke_privileges_or_proxy:
          revoke_privileges { MDBP_R(3541); }
        | PROXY_SYM ON user FROM user_list
          { MDBP_R(3542); }



        ;

revoke_privileges:
          grant_privileges ON opt_table grant_ident FROM user_and_role_list
          { MDBP_R(3543); }



        | grant_privileges ON sp_handler grant_ident FROM user_and_role_list
          { MDBP_R(3544); }



        ;

admin_option_for_role:
        ADMIN_SYM OPTION FOR_SYM grant_role
        { MDBP_R(3545); }
      | grant_role
        { MDBP_R(3546); }
      ;

deny:
          DENY clear_privileges grant_privileges_command
          { MDBP_R(3547); }


        ;
grant:
          GRANT clear_privileges grant_command
          { MDBP_R(3548); }

        | GRANT clear_privileges grant_role_command
          { MDBP_R(3549); }







        ;

grant_command:
          grant_privileges_command { MDBP_R(3550); }
        | PROXY_SYM ON user TO_SYM grant_list opt_grant_option
          { MDBP_R(3551); }



        ;

grant_privileges_command:
          grant_privileges ON opt_table grant_ident TO_SYM grant_list
          opt_require_clause opt_grant_options
          { MDBP_R(3552); }



        | grant_privileges ON sp_handler grant_ident TO_SYM grant_list
          opt_require_clause opt_grant_options
          { MDBP_R(3553); }



        ;

grant_role_command:
          grant_role TO_SYM grant_list opt_with_admin_option
          { MDBP_R(3554); }






        ;

opt_with_admin:
          /* nothing */               { MDBP_R(3555); }
        | WITH ADMIN_SYM user_or_role { MDBP_R(3556); }
        ;

opt_with_admin_option:
          /* nothing */               { MDBP_R(3557); }
        | WITH ADMIN_SYM OPTION       { MDBP_R(3558); }
        ;

role_list:
          grant_role
          { MDBP_R(3559); }



        | role_list ',' grant_role
          { MDBP_R(3560); }



        ;

current_role:
          CURRENT_ROLE optional_braces
          { MDBP_R(3561); }




          ;


role_name: ident_or_text
           { MDBP_R(3562); }

















           ;

grant_role: role_name { MDBP_R(3563); } | current_role { MDBP_R(3564); } ;

opt_table:
          /* Empty */        %prec PREC_BELOW_SP_OBJECT_TYPE { MDBP_R(3565); }
        | TABLE_SYM { MDBP_R(3566); }
        ;

grant_privileges:
          object_privilege_list { MDBP_R(3567); }
        | ALL opt_privileges
          { MDBP_R(3568); }



        ;

opt_privileges: { MDBP_R(3569); }
          /* empty */
        | PRIVILEGES { MDBP_R(3570); }
        ;

object_privilege_list:
          object_privilege
          { MDBP_R(3571); }



        | column_list_privilege
          { MDBP_R(3572); }





        | object_privilege_list ',' object_privilege
          { MDBP_R(3573); }


        | object_privilege_list ',' column_list_privilege
          { MDBP_R(3574); }




        ;

column_list_privilege:
          column_privilege '(' comma_separated_ident_list ')'
          { MDBP_R(3575); }


        ;

column_privilege:
          SELECT_SYM              { MDBP_R(3576); }
        | INSERT                  { MDBP_R(3577); }
        | UPDATE_SYM              { MDBP_R(3578); }
        | REFERENCES              { MDBP_R(3579); }
        ;

object_privilege:
          SELECT_SYM              { MDBP_R(3580); }
        | INSERT                  { MDBP_R(3581); }
        | UPDATE_SYM              { MDBP_R(3582); }
        | REFERENCES              { MDBP_R(3583); }
        | DELETE_SYM              { MDBP_R(3584); }
        | USAGE                   { MDBP_R(3585); }
        | INDEX_SYM               { MDBP_R(3586); }
        | ALTER                   { MDBP_R(3587); }
        | CREATE                  { MDBP_R(3588); }
        | DROP                    { MDBP_R(3589); }
        | EXECUTE_SYM             { MDBP_R(3590); }
        | RELOAD                  { MDBP_R(3591); }
        | SHUTDOWN                { MDBP_R(3592); }
        | PROCESS                 { MDBP_R(3593); }
        | FILE_SYM                { MDBP_R(3594); }
        | GRANT OPTION            { MDBP_R(3595); }
        | SHOW DATABASES          { MDBP_R(3596); }
        | SUPER_SYM               { MDBP_R(3597); }
        | CREATE TEMPORARY TABLES { MDBP_R(3598); }
        | LOCK_SYM TABLES         { MDBP_R(3599); }
        | REPLICATION SLAVE       { MDBP_R(3600); }
        | REPLICATION CLIENT_SYM  { MDBP_R(3601); }
        | CREATE VIEW_SYM         { MDBP_R(3602); }
        | SHOW VIEW_SYM           { MDBP_R(3603); }
        | CREATE ROUTINE_SYM      { MDBP_R(3604); }
        | ALTER ROUTINE_SYM       { MDBP_R(3605); }
        | CREATE USER_SYM         { MDBP_R(3606); }
        | EVENT_SYM               { MDBP_R(3607); }
        | TRIGGER_SYM             { MDBP_R(3608); }
        | CREATE TABLESPACE       { MDBP_R(3609); }
        | DELETE_SYM HISTORY_SYM  { MDBP_R(3610); }
        | SET USER_SYM            { MDBP_R(3611); }
        | FEDERATED_SYM ADMIN_SYM { MDBP_R(3612); }
        | CONNECTION_SYM ADMIN_SYM         { MDBP_R(3613); }
        | READ_SYM ONLY_SYM ADMIN_SYM      { MDBP_R(3614); }
        | READ_ONLY_SYM ADMIN_SYM          { MDBP_R(3615); }
        | BINLOG_SYM MONITOR_SYM           { MDBP_R(3616); }
        | BINLOG_SYM ADMIN_SYM             { MDBP_R(3617); }
        | BINLOG_SYM REPLAY_SYM            { MDBP_R(3618); }
        | REPLICATION MASTER_SYM ADMIN_SYM { MDBP_R(3619); }
        | REPLICATION SLAVE ADMIN_SYM      { MDBP_R(3620); }
        | SLAVE MONITOR_SYM                { MDBP_R(3621); }
        | SHOW CREATE ROUTINE_SYM          { MDBP_R(3622); }
        ;

opt_and:
          /* empty */ { MDBP_R(3623); }
        | AND_SYM { MDBP_R(3624); }
        ;

require_list:
          require_list_element opt_and require_list { MDBP_R(3625); }
        | require_list_element { MDBP_R(3626); }
        ;

require_list_element:
          SUBJECT_SYM TEXT_STRING
          { MDBP_R(3627); }





        | ISSUER_SYM TEXT_STRING
          { MDBP_R(3628); }





        | CIPHER_SYM TEXT_STRING
          { MDBP_R(3629); }





        ;

grant_ident:
          '*'
          { MDBP_R(3630); }







        | ident '.' '*'
          { MDBP_R(3631); }




        | '*' '.' '*'
          { MDBP_R(3632); }





        | table_ident
          { MDBP_R(3633); }



        ;

user_list:
          user
          { MDBP_R(3634); }



        | user_list ',' user
          { MDBP_R(3635); }



        ;

grant_list:
          grant_user
          { MDBP_R(3636); }



        | grant_list ',' grant_user
          { MDBP_R(3637); }



        ;

user_and_role_list:
          user_or_role
          { MDBP_R(3638); }



        | user_and_role_list ',' user_or_role
          { MDBP_R(3639); }



        ;

via_or_with: VIA_SYM { MDBP_R(3640); } | WITH { MDBP_R(3641); } ;
using_or_as: USING { MDBP_R(3642); } | AS { MDBP_R(3643); } ;

grant_user:
          user IDENTIFIED_SYM BY TEXT_STRING
          { MDBP_R(3644); }




        | user IDENTIFIED_SYM BY PASSWORD_SYM TEXT_STRING
          { MDBP_R(3645); }




        | user IDENTIFIED_SYM via_or_with auth_expression
          { MDBP_R(3646); }



        | user_or_role
          { MDBP_R(3647); }


        ;

auth_expression:
          auth_token OR_SYM auth_expression
          { MDBP_R(3648); }




        | auth_token
          { MDBP_R(3649); }


        ;

auth_token:
          ident_or_text opt_auth_str
        { MDBP_R(3650); }



        ;

opt_auth_str:
        /* empty */
        { MDBP_R(3651); }



      | using_or_as TEXT_STRING_sys
        { MDBP_R(3652); }




      | using_or_as PASSWORD_SYM '(' TEXT_STRING ')'
        { MDBP_R(3653); }




      ;

opt_require_clause: { MDBP_R(3654); }
          /* empty */
        | REQUIRE_SYM require_list
          { MDBP_R(3655); }


        | REQUIRE_SYM SSL_SYM
          { MDBP_R(3656); }


        | REQUIRE_SYM X509_SYM
          { MDBP_R(3657); }


        | REQUIRE_SYM NONE_SYM
          { MDBP_R(3658); }


        ;

resource_option:
        MAX_QUERIES_PER_HOUR ulong_num
          { MDBP_R(3659); }



        | MAX_UPDATES_PER_HOUR ulong_num
          { MDBP_R(3660); }



        | MAX_CONNECTIONS_PER_HOUR ulong_num
          { MDBP_R(3661); }



        | MAX_USER_CONNECTIONS_SYM int_num
          { MDBP_R(3662); }



        | MAX_STATEMENT_TIME_SYM NUM_literal
          { MDBP_R(3663); }



        ;

resource_option_list:
	  resource_option_list resource_option { MDBP_R(3664); }
	| resource_option { MDBP_R(3665); }
        ;

opt_resource_options:
	  /* empty */ { MDBP_R(3666); }
	| WITH resource_option_list { MDBP_R(3667); }
        ;


opt_grant_options:
          /* empty */            { MDBP_R(3668); }
        | WITH grant_option_list { MDBP_R(3669); }
        ;

opt_grant_option:
          /* empty */       { MDBP_R(3670); }
        | WITH GRANT OPTION { MDBP_R(3671); }
        ;

grant_option_list:
          grant_option_list grant_option { MDBP_R(3672); }
        | grant_option { MDBP_R(3673); }
        ;

grant_option:
          GRANT OPTION    { MDBP_R(3674); }
	| resource_option { MDBP_R(3675); }
        ;

begin_stmt_mariadb:
          BEGIN_MARIADB_SYM
          { MDBP_R(3676); }




          opt_work { MDBP_R(3677); }
          ;

compound_statement:
          sp_proc_stmt_compound_ok
          { MDBP_R(3678); }




        ;

opt_not:
        /* nothing */  { MDBP_R(3679); }
        | not          { MDBP_R(3680); }
        ;

opt_work:
          /* empty */ { MDBP_R(3681); }
        | WORK_SYM  { MDBP_R(3682); }
        ;

opt_chain:
          /* empty */
          { MDBP_R(3683); }
        | AND_SYM NO_SYM CHAIN_SYM { MDBP_R(3684); }
        | AND_SYM CHAIN_SYM        { MDBP_R(3685); }
        ;

opt_release:
          /* empty */
          { MDBP_R(3686); }
        | RELEASE_SYM        { MDBP_R(3687); }
        | NO_SYM RELEASE_SYM { MDBP_R(3688); }
        ;

commit:
          COMMIT_SYM opt_work opt_chain opt_release
          { MDBP_R(3689); }







        ;

rollback:
          ROLLBACK_SYM opt_work opt_chain opt_release
          { MDBP_R(3690); }







        | ROLLBACK_SYM opt_work TO_SYM SAVEPOINT_SYM ident
          { MDBP_R(3691); }




        | ROLLBACK_SYM opt_work TO_SYM ident
          { MDBP_R(3692); }




        ;

savepoint:
          SAVEPOINT_SYM ident
          { MDBP_R(3693); }




        ;

release:
          RELEASE_SYM SAVEPOINT_SYM ident
          { MDBP_R(3694); }




        ;

/*
   UNIONS : glue selects together
*/

unit_type_decl:
          UNION_SYM union_option
          { MDBP_R(3695); }
        | INTERSECT_SYM union_option
          { MDBP_R(3696); }
        | EXCEPT_SYM union_option
          { MDBP_R(3697); }
        ;

/*
  Start a UNION, for non-top level query expressions.
*/
union_option:
          /* empty */ { MDBP_R(3698); }
        | DISTINCT  { MDBP_R(3699); }
        | ALL       { MDBP_R(3700); }
        ;

query_expression_option:
          STRAIGHT_JOIN { MDBP_R(3701); }
        | HIGH_PRIORITY
          { MDBP_R(3702); }




        | DISTINCT         { MDBP_R(3703); }
        | UNIQUE_SYM       { MDBP_R(3704); }
        | SQL_SMALL_RESULT { MDBP_R(3705); }
        | SQL_BIG_RESULT   { MDBP_R(3706); }
        | SQL_BUFFER_RESULT { MDBP_R(3707); }
        | SQL_CALC_FOUND_ROWS { MDBP_R(3708); }
        | ALL { MDBP_R(3709); }
        ;

/**************************************************************************

 DEFINER clause support.

**************************************************************************/

definer_opt:
          no_definer { MDBP_R(3710); }
        | definer { MDBP_R(3711); }
        ;

no_definer:
          /* empty */
          { MDBP_R(3712); }









        ;

definer:
          DEFINER_SYM '=' user_or_role
          { MDBP_R(3713); }



        ;

/**************************************************************************

 CREATE VIEW statement parts.

**************************************************************************/

view_algorithm:
          ALGORITHM_SYM '=' UNDEFINED_SYM { MDBP_R(3714); }
        | ALGORITHM_SYM '=' MERGE_SYM     { MDBP_R(3715); }
        | ALGORITHM_SYM '=' TEMPTABLE_SYM { MDBP_R(3716); }
        ;

opt_view_suid:
          /* empty */                      { MDBP_R(3717); }
        | view_suid                        { MDBP_R(3718); }
        ;

view_suid:
          SQL_SYM SECURITY_SYM DEFINER_SYM { MDBP_R(3719); }
        | SQL_SYM SECURITY_SYM INVOKER_SYM { MDBP_R(3720); }
        ;

view_list_opt:
          /* empty */
          { MDBP_R(3721); }
        | '(' view_list ')' { MDBP_R(3722); }
        ;

view_list:
          ident 
          { MDBP_R(3723); }




        | view_list ',' ident
          { MDBP_R(3724); }




        ;

view_select:
          { MDBP_R(3725); }




          query_expression
          view_check_option
          { MDBP_R(3726); }



        ;

view_check_option:
          /* empty */                     { MDBP_R(3727); }
        | WITH CHECK_SYM OPTION           { MDBP_R(3728); }
        | WITH CASCADED CHECK_SYM OPTION  { MDBP_R(3729); }
        | WITH LOCAL_SYM CHECK_SYM OPTION { MDBP_R(3730); }
        ;

/**************************************************************************

 CREATE TRIGGER statement parts.

**************************************************************************/

trigger_action_order:
            FOLLOWS_SYM
            { MDBP_R(3731); }
          | PRECEDES_SYM
            { MDBP_R(3732); }
          ;

trigger_follows_precedes_clause:
            /* empty */
            { MDBP_R(3733); }



          |
            trigger_action_order ident_or_text
            { MDBP_R(3734); }



          ;

opt_on_update_cols:
            /* empty */
            { MDBP_R(3735); }


          | OF_SYM on_update_cols
            { MDBP_R(3736); }






          ;

on_update_cols:
          ident
          { MDBP_R(3737); }















        | on_update_cols ',' ident
          { MDBP_R(3738); }









	;

trigger_tail:
          remember_name
          opt_if_not_exists
          { MDBP_R(3739); }



          sp_name
          trg_action_time
          trg_events
          opt_on_update_cols
          ON
          remember_name /* $9 */
          { MDBP_R(3740); }


          table_ident /* $11 */
          FOR_SYM
          remember_name /* $13 */
          { MDBP_R(3741); }


          EACH_SYM
          ROW_SYM
          { MDBP_R(3742); }


          trigger_follows_precedes_clause /* $18 */
          { MDBP_R(3743); }



















          sp_proc_stmt /* $20 */ force_lookahead /* $21 */
          { MDBP_R(3744); }

















        ;

/**************************************************************************

 CREATE FUNCTION | PROCEDURE statements parts.

**************************************************************************/


create_package_chistic:
          COMMENT_SYM TEXT_STRING_sys
          { MDBP_R(3745); }
        | sp_suid
          { MDBP_R(3746); }
        ;

create_package_chistics:
          create_package_chistic { MDBP_R(3747); }
        | create_package_chistics create_package_chistic { MDBP_R(3748); }
        ;

opt_create_package_chistics:
          /*empty*/ { MDBP_R(3749); }
        | create_package_chistics { MDBP_R(3750); }
        ;

opt_create_package_chistics_init:
          { MDBP_R(3751); }
          opt_create_package_chistics { MDBP_R(3752); }
        ;

/*************************************************************************/

xa:
          XA_SYM begin_or_start xid opt_join_or_resume
          { MDBP_R(3753); }


        | XA_SYM END xid opt_suspend
          { MDBP_R(3754); }


        | XA_SYM PREPARE_SYM xid
          { MDBP_R(3755); }


        | XA_SYM COMMIT_SYM xid opt_one_phase
          { MDBP_R(3756); }


        | XA_SYM ROLLBACK_SYM xid
          { MDBP_R(3757); }


        | XA_SYM RECOVER_SYM opt_format_xid
          { MDBP_R(3758); }



        ;

opt_format_xid:
         /* empty */ { MDBP_R(3759); }
        | FORMAT_SYM '=' ident_or_text
          { MDBP_R(3760); }











        ;

xid:
          text_string
          { MDBP_R(3761); }





          | text_string ',' text_string
          { MDBP_R(3762); }





          | text_string ',' text_string ',' ulong_num
          { MDBP_R(3763); }








        ;

begin_or_start:
          BEGIN_MARIADB_SYM { MDBP_R(3764); }
        | BEGIN_ORACLE_SYM { MDBP_R(3765); }
        | START_SYM { MDBP_R(3766); }
        ;

opt_join_or_resume:
          /* nothing */ { MDBP_R(3767); }
        | JOIN_SYM      { MDBP_R(3768); }
        | RESUME_SYM    { MDBP_R(3769); }
        ;

opt_one_phase:
          /* nothing */     { MDBP_R(3770); }
        | ONE_SYM PHASE_SYM { MDBP_R(3771); }
        ;

opt_suspend:
          /* nothing */
          { MDBP_R(3772); }
        | SUSPEND_SYM
          { MDBP_R(3773); }
          opt_migrate { MDBP_R(3774); }
        ;

opt_migrate:
          /* nothing */       { MDBP_R(3775); }
        | FOR_SYM MIGRATE_SYM { MDBP_R(3776); }
        ;

install:
          INSTALL_SYM PLUGIN_SYM opt_if_not_exists ident SONAME_SYM TEXT_STRING_sys
          { MDBP_R(3777); }



        | INSTALL_SYM SONAME_SYM TEXT_STRING_sys
          { MDBP_R(3778); }


        ;

uninstall:
          UNINSTALL_SYM PLUGIN_SYM opt_if_exists ident
          { MDBP_R(3779); }



        | UNINSTALL_SYM SONAME_SYM opt_if_exists TEXT_STRING_sys
          { MDBP_R(3780); }



        ;

/* Avoid compiler warning from yy_*.cc where yyerrlab1 is not used */
keep_gcc_happy:
          IMPOSSIBLE_ACTION
          { MDBP_R(3781); }


        ;

_empty: { MDBP_R(3782); }
          /* Empty */
        ;




statement:
          verb_clause { MDBP_R(3783); }
        ;

sp_statement:
          statement { MDBP_R(3784); }
        ;

sp_if_then_statements:
          sp_proc_stmts1 { MDBP_R(3785); }
        ;

sp_case_then_statements:
          sp_proc_stmts1 { MDBP_R(3786); }
        ;

sp_tail_is:
          /*empty*/ { MDBP_R(3787); }
        ;

sp_package_function_body:
          sp_proc_stmt_in_returns_clause { MDBP_R(3788); }
        ;

sp_package_procedure_body:
          sp_proc_stmt { MDBP_R(3789); }
        ;

opt_trailing_sp_name:
          /*empty*/ { MDBP_R(3790); }
        ;

opt_package_routine_end_name:
          /*empty*/       { MDBP_R(3791); }
        | FORCE_LOOKAHEAD { MDBP_R(3792); }
        ;

sf_parameters:
          sp_parenthesized_fdparam_list { MDBP_R(3793); }
        ;

sp_parameters:
          sp_parenthesized_pdparam_list { MDBP_R(3794); }
        ;

sf_returned_type_clause:
          RETURNS_SYM
          { MDBP_R(3795); }



          sf_return_type { MDBP_R(3796); }
        ;

sf_return_type:
          field_type_all_with_typedefs
          { MDBP_R(3797); }



        | ROW_SYM row_type_body
          { MDBP_R(3798); }



        | ROW_SYM TYPE_SYM OF_SYM ident
          { MDBP_R(3799); }




        | ROW_SYM TYPE_SYM OF_SYM ident '.' ident
          { MDBP_R(3800); }




        | TYPE_SYM OF_SYM ident '.' ident
          { MDBP_R(3801); }




        | TYPE_SYM OF_SYM ident '.' ident '.' ident
          { MDBP_R(3802); }




        ;

package_implementation_item_declaration:
          DECLARE_MARIADB_SYM sp_decl_variable_list ';' { MDBP_R(3803); }
        ;

//  Inside CREATE PACKAGE BODY, package-wide items (e.g. variables)
//  must be declared before routine definitions.

package_implementation_declare_section_list:
          package_implementation_declare_section_list1 %prec PREC_BELOW_SP_OBJECT_TYPE { MDBP_R(3804); }
        | package_implementation_declare_section_list2 %prec PREC_BELOW_SP_OBJECT_TYPE { MDBP_R(3805); }
        | package_implementation_declare_section_list1
          package_implementation_declare_section_list2 %prec PREC_BELOW_SP_OBJECT_TYPE
          { MDBP_R(3806); }
        ;

package_implementation_declare_section:
          package_implementation_declare_section_list { MDBP_R(3807); }
        ;

package_implementation_executable_section:
          sp_proc_stmts END
          { MDBP_R(3808); }


        ;


reserved_keyword_udt_param_type:
          INOUT_SYM { MDBP_R(3809); }
        | IN_SYM { MDBP_R(3810); }
        | OUT_SYM { MDBP_R(3811); }
        ;

reserved_keyword_udt:
          reserved_keyword_udt_not_param_type { MDBP_R(3812); }
        | reserved_keyword_udt_param_type { MDBP_R(3813); }
        ;

// Keywords that start an SP block section
keyword_sp_block_section:
          BEGIN_MARIADB_SYM { MDBP_R(3814); }
        | END { MDBP_R(3815); }
        ;

// Keywords that we allow for labels in SPs.
// Should not include keywords that start a statement or SP characteristics.
keyword_label:
          keyword_data_type { MDBP_R(3816); }
        | keyword_set_special_case { MDBP_R(3817); }
        | keyword_sp_var_and_label { MDBP_R(3818); }
        | keyword_sysvar_type { MDBP_R(3819); }
        | FUNCTION_SYM { MDBP_R(3820); }
        | EXCEPTION_ORACLE_SYM { MDBP_R(3821); }
        | IGNORED_SYM { MDBP_R(3822); }
        ;

keyword_sp_decl:
          keyword_data_type { MDBP_R(3823); }
        | keyword_cast_type { MDBP_R(3824); }
        | keyword_set_special_case { MDBP_R(3825); }
        | keyword_sp_block_section { MDBP_R(3826); }
        | keyword_sp_head { MDBP_R(3827); }
        | keyword_sp_var_and_label { MDBP_R(3828); }
        | keyword_sp_var_not_label { MDBP_R(3829); }
        | keyword_sysvar_type { MDBP_R(3830); }
        | keyword_verb_clause { MDBP_R(3831); }
        | FUNCTION_SYM { MDBP_R(3832); }
        | WINDOW_SYM { MDBP_R(3833); }
        | IGNORED_SYM { MDBP_R(3834); }
        ;

opt_truncate_table_storage_clause:
          _empty { MDBP_R(3835); }
        ;


ident_for_loop_index:
          ident { MDBP_R(3836); }
        ;

row_field_name:
          ident
          { MDBP_R(3837); }



        ;

while_body:
          expr_lex DO_SYM
          { MDBP_R(3838); }



          sp_proc_stmts1 END WHILE_SYM
          { MDBP_R(3839); }



        ;

for_loop_statements:
          DO_SYM sp_proc_stmts1 END FOR_SYM
          { MDBP_R(3840); }
        ;

sp_label:
          label_ident ':' { MDBP_R(3841); }
        ;

sp_control_label:
          sp_label { MDBP_R(3842); }
        ;

sp_block_label:
          sp_label
          { MDBP_R(3843); }




        ;

sp_opt_default:
          _empty       { MDBP_R(3844); }
        | DEFAULT remember_cpp_ptr expr remember_end
          { MDBP_R(3845); }











        ;

sp_decl_variable_list_anchored:
          sp_decl_idents_init_vars
          TYPE_SYM OF_SYM optionally_qualified_column_ident
          sp_opt_default
          { MDBP_R(3846); }





        | sp_decl_idents_init_vars
          ROW_SYM TYPE_SYM OF_SYM optionally_qualified_column_ident
          sp_opt_default
          { MDBP_R(3847); }





        ;

sp_param_name_and_mode:
          sp_parameter_type sp_param_name
          { MDBP_R(3848); }



        | sp_param_name { MDBP_R(3849); }
        ;

sp_param_init_vars:
          sp_param_name_and_mode_init_vars field_type_all_with_typedefs
          { MDBP_R(3850); }



        | sp_param_name_and_mode_init_vars ROW_SYM row_type_body
          { MDBP_R(3851); }



        | sp_param_anchored { MDBP_R(3852); }
        ;

sp_param_anchored:
          sp_param_name_and_mode_init_vars TYPE_SYM OF_SYM ident '.' ident
          { MDBP_R(3853); }





        | sp_param_name_and_mode_init_vars TYPE_SYM OF_SYM ident '.' ident '.' ident
          { MDBP_R(3854); }




        | sp_param_name_and_mode_init_vars ROW_SYM TYPE_SYM OF_SYM ident
          { MDBP_R(3855); }



        | sp_param_name_and_mode_init_vars ROW_SYM TYPE_SYM OF_SYM ident '.' ident
          { MDBP_R(3856); }



        ;


sf_c_chistics_and_body_standalone:
          sp_c_chistics
          { MDBP_R(3857); }



          sp_proc_stmt_in_returns_clause force_lookahead
          { MDBP_R(3858); }



        ;

sp_tail_standalone:
          sp_name
          { MDBP_R(3859); }





          sp_parenthesized_pdparam_list
          sp_c_chistics
          { MDBP_R(3860); }



          sp_proc_stmt force_lookahead
          { MDBP_R(3861); }



        ;


sp_decls:
          _empty
          { MDBP_R(3862); }


        | sp_decls sp_decl ';'
          { MDBP_R(3863); }







        ;

sp_decl:
          DECLARE_MARIADB_SYM sp_decl_body { MDBP_R(3864); }
        ;


sp_decl_body:
          sp_decl_variable_list { MDBP_R(3865); }
        | sp_decl_ident CONDITION_SYM FOR_SYM sp_cond
          { MDBP_R(3866); }







        | sp_decl_handler { MDBP_R(3867); }
        | sp_decl_ident CURSOR_SYM
          { MDBP_R(3868); }


          opt_parenthesized_cursor_formal_parameters
          FOR_SYM sp_cursor_stmt
          { MDBP_R(3869); }








        ;



//  ps_proc_stmt_in_returns_clause is a statement that is allowed
//  in the RETURNS clause of a stored function definition directly,
//  without the BEGIN..END  block.
//  It should not include any syntax structures starting with '(', to avoid
//  shift/reduce conflicts with the rule "field_type" and its sub-rules
//  that scan an optional length, like CHAR(1) or YEAR(4).
//  See MDEV-9166.

sp_proc_stmt_in_returns_clause:
          sp_proc_stmt_return { MDBP_R(3870); }
        | sp_labeled_block { MDBP_R(3871); }
        | sp_unlabeled_block { MDBP_R(3872); }
        | sp_labeled_control { MDBP_R(3873); }
        | sp_proc_stmt_compound_ok { MDBP_R(3874); }
        ;

sp_proc_stmt:
          sp_proc_stmt_in_returns_clause { MDBP_R(3875); }
        | sp_proc_stmt_statement { MDBP_R(3876); }
        | sp_proc_stmt_continue_oracle { MDBP_R(3877); }
        | sp_proc_stmt_exit_oracle { MDBP_R(3878); }
        | sp_proc_stmt_leave { MDBP_R(3879); }
        | sp_proc_stmt_iterate { MDBP_R(3880); }
        | sp_proc_stmt_goto_oracle { MDBP_R(3881); }
        | sp_proc_stmt_with_cursor { MDBP_R(3882); }
        ;

sp_proc_stmt_compound_ok:
          sp_proc_stmt_if { MDBP_R(3883); }
        | case_stmt_specification { MDBP_R(3884); }
        | sp_unlabeled_block_not_atomic { MDBP_R(3885); }
        | sp_unlabeled_control { MDBP_R(3886); }
        ;


sp_labeled_block:
          sp_block_label
          BEGIN_MARIADB_SYM
          { MDBP_R(3887); }


          sp_decls
          sp_proc_stmts
          END
          sp_opt_label
          { MDBP_R(3888); }



        ;

sp_unlabeled_block:
          BEGIN_MARIADB_SYM
          { MDBP_R(3889); }


          sp_decls
          sp_proc_stmts
          END
          { MDBP_R(3890); }



        ;

sp_unlabeled_block_not_atomic:
          BEGIN_MARIADB_SYM not ATOMIC_SYM // TODO: BEGIN ATOMIC (not -> opt_not)
          { MDBP_R(3891); }




          sp_decls
          sp_proc_stmts
          END
          { MDBP_R(3892); }



        ;























































































































































































































































































































































































































































































































































package_implementation_declare_section_list1:
          package_implementation_item_declaration { MDBP_R(3893); }
        | package_implementation_declare_section_list1
          package_implementation_item_declaration
          { MDBP_R(3894); }
        ;

package_implementation_declare_section_list2:
          package_implementation_routine_definition { MDBP_R(3895); }
        | package_implementation_declare_section_list2
          package_implementation_routine_definition
          { MDBP_R(3896); }
        ;


package_specification_function:
          remember_lex ident
          { MDBP_R(3897); }






          sf_parameters
          sf_returned_type_clause
          sp_c_chistics
          { MDBP_R(3898); }





        ;

package_specification_procedure:
          remember_lex ident
          { MDBP_R(3899); }






          sp_parameters
          sp_c_chistics
          { MDBP_R(3900); }





        ;


package_implementation_routine_definition:
          FUNCTION_SYM package_specification_function
                       package_implementation_function_body   ';'
          { MDBP_R(3901); }






        | PROCEDURE_SYM package_specification_procedure
                        package_implementation_procedure_body ';'
          { MDBP_R(3902); }






        | package_specification_routine { MDBP_R(3903); }
        ;


package_implementation_function_body:
          sp_tail_is remember_lex
          { MDBP_R(3904); }







          sp_package_function_body opt_package_routine_end_name
          { MDBP_R(3905); }





        ;

package_implementation_procedure_body:
          sp_tail_is remember_lex
          { MDBP_R(3906); }







          sp_package_procedure_body opt_package_routine_end_name
          { MDBP_R(3907); }





        ;


opt_package_specification_element_list:
          _empty { MDBP_R(3908); }
        | package_specification_element_list { MDBP_R(3909); }
        ;

package_specification_element_list:
          package_specification_element { MDBP_R(3910); }
        | package_specification_element_list package_specification_element { MDBP_R(3911); }
        ;

package_specification_routine:
          FUNCTION_SYM package_specification_function ';'
          { MDBP_R(3912); }





        | PROCEDURE_SYM package_specification_procedure ';'
          { MDBP_R(3913); }





        ;

package_specification_element:
          package_specification_routine { MDBP_R(3914); }



        ;















































































































create_routine:
          create_or_replace definer_opt PROCEDURE_SYM opt_if_not_exists
          { MDBP_R(3915); }



          sp_tail_standalone
          { MDBP_R(3916); }


        | create_or_replace definer opt_aggregate FUNCTION_SYM opt_if_not_exists
          sp_name
          { MDBP_R(3917); }



          sf_parameters
          sf_returned_type_clause
          sf_c_chistics_and_body_standalone
          opt_trailing_sp_name
          { MDBP_R(3918); }



        | create_or_replace no_definer opt_aggregate FUNCTION_SYM opt_if_not_exists
          sp_name
          { MDBP_R(3919); }



          sf_parameters
          sf_returned_type_clause
          sf_c_chistics_and_body_standalone
          opt_trailing_sp_name
          { MDBP_R(3920); }



        | create_or_replace no_definer opt_aggregate FUNCTION_SYM opt_if_not_exists
          ident RETURNS_SYM udf_type SONAME_SYM TEXT_STRING_sys
          { MDBP_R(3921); }




        | create_or_replace definer_opt sp_handler_package_spec
          opt_if_not_exists sp_name opt_create_package_chistics_init
          { MDBP_R(3922); }
















          sp_tail_is
          opt_package_specification_element_list END
          remember_end_opt opt_trailing_sp_name
          { MDBP_R(3923); }











        | create_or_replace definer_opt sp_handler_package_body
          opt_if_not_exists sp_name opt_create_package_chistics_init
          { MDBP_R(3924); }









          sp_tail_is
          package_implementation_declare_section
          package_implementation_executable_section
          { MDBP_R(3925); }




          remember_end_opt opt_trailing_sp_name
          { MDBP_R(3926); }



        ;




































































































































































































































































































































/**
  @} (end of group Parser)
*/


/* ---- MariaDB REST Service (REST SQL) statements ---------------------
   Merged from MariaDB Shell's mysqlshdk/libs/mrs/parser.yy by
   tools/mrs_merge.py; see UPSTREAM.md. */

mrs_statement:
          mrs_configure_rest_metadata_statement { MDBP_R(3927); }
        | mrs_create_rest_service_statement { MDBP_R(3928); }
        | mrs_create_rest_schema_statement { MDBP_R(3929); }
        | mrs_create_rest_view_statement { MDBP_R(3930); }
        | mrs_create_rest_procedure_statement { MDBP_R(3931); }
        | mrs_create_rest_function_statement { MDBP_R(3932); }
        | mrs_create_rest_content_set_statement { MDBP_R(3933); }
        | mrs_create_rest_content_file_statement { MDBP_R(3934); }
        | mrs_create_rest_auth_app_statement { MDBP_R(3935); }
        | mrs_create_rest_role_statement { MDBP_R(3936); }
        | mrs_create_rest_user_statement { MDBP_R(3937); }
        | mrs_clone_rest_service_statement { MDBP_R(3938); }
        | mrs_alter_rest_service_statement { MDBP_R(3939); }
        | mrs_alter_rest_schema_statement { MDBP_R(3940); }
        | mrs_alter_rest_view_statement { MDBP_R(3941); }
        | mrs_alter_rest_procedure_statement { MDBP_R(3942); }
        | mrs_alter_rest_function_statement { MDBP_R(3943); }
        | mrs_alter_rest_content_set_statement { MDBP_R(3944); }
        | mrs_alter_rest_auth_app_statement { MDBP_R(3945); }
        | mrs_alter_rest_user_statement { MDBP_R(3946); }
        | mrs_drop_rest_service_statement { MDBP_R(3947); }
        | mrs_drop_rest_schema_statement { MDBP_R(3948); }
        | mrs_drop_rest_view_statement { MDBP_R(3949); }
        | mrs_drop_rest_procedure_statement { MDBP_R(3950); }
        | mrs_drop_rest_function_statement { MDBP_R(3951); }
        | mrs_drop_rest_content_set_statement { MDBP_R(3952); }
        | mrs_drop_rest_content_file_statement { MDBP_R(3953); }
        | mrs_drop_rest_auth_app_statement { MDBP_R(3954); }
        | mrs_drop_rest_user_statement { MDBP_R(3955); }
        | mrs_drop_rest_role_statement { MDBP_R(3956); }
        | mrs_grant_rest_role_statement { MDBP_R(3957); }
        | mrs_grant_rest_privilege_statement { MDBP_R(3958); }
        | mrs_revoke_rest_privilege_statement { MDBP_R(3959); }
        | mrs_revoke_rest_role_statement { MDBP_R(3960); }
        | mrs_use_statement { MDBP_R(3961); }
        | mrs_drop_rest_daemon_statement { MDBP_R(3962); }
        ;

mrs_enabled_disabled:
          ENABLED_SYM { MDBP_R(3963); }
        | DISABLED_SYM { MDBP_R(3964); }
        ;

mrs_enabled_disabled_private:
          ENABLED_SYM { MDBP_R(3965); }
        | DISABLED_SYM { MDBP_R(3966); }
        | PRIVATE_SYM { MDBP_R(3967); }
        ;

mrs_quoted_text_or_default:
          TEXT_STRING { MDBP_R(3968); }
        | DEFAULT { MDBP_R(3969); }
        ;

mrs_json_options:
          OPTIONS_SYM mrs_json_value { MDBP_R(3970); }
        | MERGE_SYM OPTIONS_SYM mrs_json_value { MDBP_R(3971); }
        ;

mrs_metadata:
          METADATA_SYM mrs_json_value { MDBP_R(3972); }
        ;

mrs_comments:
          COMMENT_SYM TEXT_STRING { MDBP_R(3973); }
        ;

mrs_opt_comments:
          /* empty */ { MDBP_R(3974); }
        | mrs_comments { MDBP_R(3975); }
        ;

mrs_authentication_required:
          AUTHENTICATION_SYM REQUIRED_SYM { MDBP_R(3976); }
        | AUTHENTICATION_SYM NOT_SYM REQUIRED_SYM { MDBP_R(3977); }
        ;

mrs_items_per_page:
          ITEMS_SYM PER_SYM PAGE_SYM mrs_int_number { MDBP_R(3978); }
        ;

mrs_service_schema_selector:
          DATABASE mrs_schema_request_path { MDBP_R(3979); }
        | SERVICE_SYM mrs_service_request_path DATABASE mrs_schema_request_path { MDBP_R(3980); }
        ;

mrs_opt_on_service_schema_selector:
          /* empty */ { MDBP_R(3981); }
        | ON mrs_service_schema_selector { MDBP_R(3982); }
        ;

mrs_opt_from_service_schema_selector:
          /* empty */ { MDBP_R(3983); }
        | FROM mrs_service_schema_selector { MDBP_R(3984); }
        ;

mrs_opt_on_from_service_schema_selector:
          /* empty */ { MDBP_R(3985); }
        | ON mrs_service_schema_selector { MDBP_R(3986); }
        | FROM mrs_service_schema_selector { MDBP_R(3987); }
        ;

mrs_service_ref:
          SERVICE_SYM mrs_service_request_path { MDBP_R(3988); }
        | mrs_service_request_path { MDBP_R(3989); }
        ;

mrs_role_service:
          ON ANY_SYM SERVICE_SYM { MDBP_R(3990); }
        | ON mrs_service_ref { MDBP_R(3991); }
        ;

mrs_opt_role_service:
          /* empty */ { MDBP_R(3992); }
        | mrs_role_service { MDBP_R(3993); }
        ;

mrs_opt_on_from_role_service:
          /* empty */ { MDBP_R(3994); }
        | mrs_role_service { MDBP_R(3995); }
        | FROM ANY_SYM SERVICE_SYM { MDBP_R(3996); }
        | FROM mrs_service_ref { MDBP_R(3997); }
        ;

mrs_opt_on_service:
          /* empty */ { MDBP_R(3998); }
        | ON mrs_service_ref { MDBP_R(3999); }
        ;

mrs_opt_from_service:
          /* empty */ { MDBP_R(4000); }
        | FROM mrs_service_ref { MDBP_R(4001); }
        ;

mrs_opt_on_from_service:
          /* empty */ { MDBP_R(4002); }
        | mrs_on_or_from mrs_service_ref { MDBP_R(4003); }
        ;

mrs_opt_service_request_path:
          /* empty */ { MDBP_R(4004); }
        | mrs_service_ref { MDBP_R(4005); }
        ;

mrs_opt_if_not_exists:
          /* empty */ { MDBP_R(4006); }
        | IF_SYM NOT_SYM EXISTS { MDBP_R(4007); }
        ;

mrs_opt_if_exists:
          /* empty */ { MDBP_R(4008); }
        | IF_SYM EXISTS { MDBP_R(4009); }
        ;

mrs_configure_rest_metadata_statement:
          CONFIGURE_SYM REST_SYM METADATA_SYM mrs_rest_metadata_options { MDBP_R(4010); }
        ;

mrs_rest_metadata_options:
          /* empty */ { MDBP_R(4011); }
        | mrs_rest_metadata_options mrs_metadata_schema { MDBP_R(4012); }
        | mrs_rest_metadata_options mrs_enabled_disabled { MDBP_R(4013); }
        | mrs_rest_metadata_options mrs_json_options { MDBP_R(4014); }
        | mrs_rest_metadata_options UPDATE_SYM { MDBP_R(4015); }
        | mrs_rest_metadata_options UPDATE_SYM IF_SYM AVAILABLE_SYM { MDBP_R(4016); }
        ;

mrs_create_rest_service_statement:
          mrs_create_rest_service_prefix mrs_service_request_path mrs_rest_service_options { MDBP_R(4017); }
        ;

mrs_create_rest_service_prefix:
          CREATE OR_SYM REPLACE REST_SYM SERVICE_SYM { MDBP_R(4018); }
        | CREATE REST_SYM SERVICE_SYM mrs_opt_if_not_exists { MDBP_R(4019); }
        ;

mrs_rest_service_options:
          /* empty */ { MDBP_R(4020); }
        | mrs_rest_service_options mrs_enabled_disabled { MDBP_R(4021); }
        | mrs_rest_service_options mrs_published_unpublished { MDBP_R(4022); }
        | mrs_rest_service_options mrs_rest_protocol { MDBP_R(4023); }
        | mrs_rest_service_options mrs_rest_authentication { MDBP_R(4024); }
        | mrs_rest_service_options mrs_json_options { MDBP_R(4025); }
        | mrs_rest_service_options mrs_comments { MDBP_R(4026); }
        | mrs_rest_service_options mrs_metadata { MDBP_R(4027); }
        | mrs_rest_service_options mrs_add_auth_app { MDBP_R(4028); }
        | mrs_rest_service_options mrs_remove_auth_app { MDBP_R(4029); }
        ;

mrs_published_unpublished:
          PUBLISHED_SYM { MDBP_R(4030); }
        | UNPUBLISHED_SYM { MDBP_R(4031); }
        ;

mrs_rest_protocol:
          PROTOCOL_SYM HTTP_SYM { MDBP_R(4032); }
        | PROTOCOL_SYM HTTPS_SYM { MDBP_R(4033); }
        ;

mrs_rest_authentication:
          AUTHENTICATION_SYM { MDBP_R(4034); }
        | mrs_rest_authentication PATH_SYM mrs_quoted_text_or_default { MDBP_R(4035); }
        | mrs_rest_authentication REDIRECTION_SYM mrs_quoted_text_or_default { MDBP_R(4036); }
        | mrs_rest_authentication VALIDATION_SYM mrs_quoted_text_or_default { MDBP_R(4037); }
        | mrs_rest_authentication PAGE_SYM CONTENT_SYM mrs_quoted_text_or_default { MDBP_R(4038); }
        ;

mrs_add_auth_app:
          ADD AUTH_SYM APP_SYM mrs_auth_app_name mrs_opt_if_exists { MDBP_R(4039); }
        ;

mrs_remove_auth_app:
          REMOVE_SYM AUTH_SYM APP_SYM mrs_auth_app_name mrs_opt_if_exists { MDBP_R(4040); }
        ;

mrs_create_rest_schema_statement:
          mrs_create_rest_schema_prefix mrs_opt_schema_request_path mrs_opt_on_service FROM mrs_identifier mrs_rest_schema_options { MDBP_R(4041); }
        ;

mrs_create_rest_schema_prefix:
          CREATE OR_SYM REPLACE REST_SYM DATABASE { MDBP_R(4042); }
        | CREATE REST_SYM DATABASE mrs_opt_if_not_exists { MDBP_R(4043); }
        ;

mrs_opt_schema_request_path:
          /* empty */ { MDBP_R(4044); }
        | mrs_schema_request_path { MDBP_R(4045); }
        ;

mrs_rest_schema_options:
          /* empty */ { MDBP_R(4046); }
        | mrs_rest_schema_options mrs_enabled_disabled_private { MDBP_R(4047); }
        | mrs_rest_schema_options mrs_authentication_required { MDBP_R(4048); }
        | mrs_rest_schema_options mrs_items_per_page { MDBP_R(4049); }
        | mrs_rest_schema_options mrs_json_options { MDBP_R(4050); }
        | mrs_rest_schema_options mrs_comments { MDBP_R(4051); }
        | mrs_rest_schema_options mrs_metadata { MDBP_R(4052); }
        ;

mrs_create_rest_view_statement:
          mrs_create_rest_view_prefix mrs_view_request_path mrs_opt_on_service_schema_selector AS mrs_qualified_identifier mrs_opt_class mrs_opt_graphql_crud_options mrs_opt_graphql_obj mrs_rest_object_options { MDBP_R(4053); }
        ;

mrs_create_rest_view_prefix:
          CREATE OR_SYM REPLACE REST_SYM mrs_opt_data_mapping VIEW_SYM { MDBP_R(4054); }
        | CREATE REST_SYM mrs_opt_data_mapping VIEW_SYM mrs_opt_if_not_exists { MDBP_R(4055); }
        ;

mrs_opt_data_mapping:
          /* empty */ { MDBP_R(4056); }
        | DATA_SYM { MDBP_R(4057); }
        | MAPPING_SYM { MDBP_R(4058); }
        | DATA_SYM MAPPING_SYM { MDBP_R(4059); }
        ;

mrs_opt_class:
          /* empty */ { MDBP_R(4060); }
        | CLASS_SYM mrs_rest_object_name { MDBP_R(4061); }
        ;

mrs_rest_object_options:
          /* empty */ { MDBP_R(4062); }
        | mrs_rest_object_options mrs_enabled_disabled_private { MDBP_R(4063); }
        | mrs_rest_object_options mrs_authentication_required { MDBP_R(4064); }
        | mrs_rest_object_options mrs_items_per_page { MDBP_R(4065); }
        | mrs_rest_object_options mrs_json_options { MDBP_R(4066); }
        | mrs_rest_object_options mrs_comments { MDBP_R(4067); }
        | mrs_rest_object_options mrs_metadata { MDBP_R(4068); }
        | mrs_rest_object_options MEDIA_SYM TYPE_SYM TEXT_STRING { MDBP_R(4069); }
        | mrs_rest_object_options MEDIA_SYM TYPE_SYM AUTODETECT_SYM { MDBP_R(4070); }
        | mrs_rest_object_options mrs_rest_view_format { MDBP_R(4071); }
        | mrs_rest_object_options AUTHENTICATION_SYM PROCEDURE_SYM mrs_qualified_identifier { MDBP_R(4072); }
        ;

mrs_rest_view_format:
          FORMAT_SYM FEED_SYM { MDBP_R(4073); }
        | FORMAT_SYM ITEM_SYM { MDBP_R(4074); }
        | FORMAT_SYM MEDIA_SYM { MDBP_R(4075); }
        ;

mrs_create_rest_procedure_statement:
          mrs_create_rest_procedure_prefix mrs_procedure_request_path mrs_opt_on_service_schema_selector AS mrs_qualified_identifier mrs_opt_force mrs_opt_rest_parameters mrs_rest_results mrs_rest_object_options { MDBP_R(4076); }
        ;

mrs_create_rest_procedure_prefix:
          CREATE OR_SYM REPLACE REST_SYM PROCEDURE_SYM { MDBP_R(4077); }
        | CREATE REST_SYM PROCEDURE_SYM mrs_opt_if_not_exists { MDBP_R(4078); }
        ;

mrs_opt_force:
          /* empty */ { MDBP_R(4079); }
        | FORCE_SYM { MDBP_R(4080); }
        ;

mrs_opt_rest_parameters:
          /* empty */ { MDBP_R(4081); }
        | PARAMETERS_SYM mrs_graphql_obj { MDBP_R(4082); }
        | PARAMETERS_SYM mrs_rest_object_name mrs_graphql_obj { MDBP_R(4083); }
        ;

mrs_rest_results:
          /* empty */ { MDBP_R(4084); }
        | mrs_rest_results mrs_rest_result { MDBP_R(4085); }
        ;

mrs_rest_result:
          RESULT_SYM mrs_graphql_obj { MDBP_R(4086); }
        | RESULT_SYM mrs_rest_result_name mrs_graphql_obj { MDBP_R(4087); }
        ;

mrs_create_rest_function_statement:
          mrs_create_rest_function_prefix mrs_function_request_path mrs_opt_on_service_schema_selector AS mrs_qualified_identifier mrs_opt_force mrs_opt_rest_parameters mrs_opt_rest_result mrs_rest_object_options { MDBP_R(4088); }
        ;

mrs_create_rest_function_prefix:
          CREATE OR_SYM REPLACE REST_SYM FUNCTION_SYM { MDBP_R(4089); }
        | CREATE REST_SYM FUNCTION_SYM mrs_opt_if_not_exists { MDBP_R(4090); }
        ;

mrs_opt_rest_result:
          /* empty */ { MDBP_R(4091); }
        | mrs_rest_result { MDBP_R(4092); }
        ;

mrs_create_rest_content_set_statement:
          mrs_create_rest_content_set_prefix mrs_content_set_request_path mrs_opt_on_service mrs_rest_content_set_options { MDBP_R(4093); }
        ;

mrs_create_rest_content_set_prefix:
          CREATE OR_SYM REPLACE REST_SYM CONTENT_SYM SET { MDBP_R(4094); }
        | CREATE REST_SYM CONTENT_SYM SET mrs_opt_if_not_exists { MDBP_R(4095); }
        ;

mrs_rest_content_set_options:
          /* empty */ { MDBP_R(4096); }
        | mrs_rest_content_set_options mrs_enabled_disabled_private { MDBP_R(4097); }
        | mrs_rest_content_set_options mrs_authentication_required { MDBP_R(4098); }
        | mrs_rest_content_set_options mrs_json_options { MDBP_R(4099); }
        | mrs_rest_content_set_options mrs_comments { MDBP_R(4100); }
        ;

mrs_alter_rest_content_set_options:
          /* empty */ { MDBP_R(4101); }
        | mrs_alter_rest_content_set_options mrs_enabled_disabled_private { MDBP_R(4102); }
        | mrs_alter_rest_content_set_options mrs_authentication_required { MDBP_R(4103); }
        | mrs_alter_rest_content_set_options mrs_json_options { MDBP_R(4104); }
        | mrs_alter_rest_content_set_options mrs_comments { MDBP_R(4105); }
        | mrs_alter_rest_content_set_options LOAD SCRIPTS_SYM { MDBP_R(4106); }
        | mrs_alter_rest_content_set_options LOAD TYPESCRIPT_SYM SCRIPTS_SYM { MDBP_R(4107); }
        ;

mrs_create_rest_content_file_statement:
          mrs_create_rest_content_file_prefix mrs_content_file_request_path ON mrs_opt_service_request_path CONTENT_SYM SET mrs_content_set_request_path mrs_opt_binary CONTENT_SYM TEXT_STRING mrs_rest_content_file_options { MDBP_R(4108); }
        ;

mrs_opt_binary:
          /* empty */ { MDBP_R(4109); }
        | BINARY { MDBP_R(4110); }
        ;

mrs_create_rest_content_file_prefix:
          CREATE OR_SYM REPLACE REST_SYM CONTENT_SYM FILE_SYM { MDBP_R(4111); }
        | CREATE REST_SYM CONTENT_SYM FILE_SYM mrs_opt_if_not_exists { MDBP_R(4112); }
        ;

mrs_rest_content_file_options:
          /* empty */ { MDBP_R(4113); }
        | mrs_rest_content_file_options mrs_enabled_disabled_private { MDBP_R(4114); }
        | mrs_rest_content_file_options mrs_authentication_required { MDBP_R(4115); }
        | mrs_rest_content_file_options mrs_json_options { MDBP_R(4116); }
        ;

mrs_create_rest_auth_app_statement:
          mrs_create_rest_auth_app_prefix mrs_auth_app_name VENDOR_SYM mrs_vendor mrs_rest_auth_app_options { MDBP_R(4117); }
        ;

mrs_create_rest_auth_app_prefix:
          CREATE OR_SYM REPLACE REST_SYM mrs_auth_or_authentication APP_SYM { MDBP_R(4118); }
        | CREATE REST_SYM mrs_auth_or_authentication APP_SYM mrs_opt_if_not_exists { MDBP_R(4119); }
        ;

mrs_auth_or_authentication:
          AUTH_SYM { MDBP_R(4120); }
        | AUTHENTICATION_SYM { MDBP_R(4121); }
        ;

mrs_vendor:
          MRS_SYM { MDBP_R(4122); }
        | MARIADB_SYM { MDBP_R(4123); }
        | mrs_vendor_name { MDBP_R(4124); }
        ;

mrs_auth_app_name:
          mrs_text_or_identifier { MDBP_R(4125); }
        ;

mrs_vendor_name:
          mrs_text_or_identifier { MDBP_R(4126); }
        ;

mrs_rest_auth_app_options:
          /* empty */ { MDBP_R(4127); }
        | mrs_rest_auth_app_options mrs_enabled_disabled { MDBP_R(4128); }
        | mrs_rest_auth_app_options mrs_comments { MDBP_R(4129); }
        | mrs_rest_auth_app_options mrs_allow_new_users_to_register { MDBP_R(4130); }
        | mrs_rest_auth_app_options DEFAULT ROLE_SYM mrs_text_or_identifier { MDBP_R(4131); }
        | mrs_rest_auth_app_options mrs_app_or_client ID_SYM TEXT_STRING { MDBP_R(4132); }
        | mrs_rest_auth_app_options mrs_app_or_client SECRET_SYM TEXT_STRING { MDBP_R(4133); }
        | mrs_rest_auth_app_options URL_SYM TEXT_STRING { MDBP_R(4134); }
        ;

mrs_app_or_client:
          APP_SYM { MDBP_R(4135); }
        | CLIENT_SYM { MDBP_R(4136); }
        ;

mrs_allow_new_users_to_register:
          ALLOW_SYM NEW_SYM USERS_SYM mrs_opt_to_register { MDBP_R(4137); }
        | DO_SYM NOT_SYM ALLOW_SYM NEW_SYM USERS_SYM mrs_opt_to_register { MDBP_R(4138); }
        ;

mrs_opt_to_register:
          /* empty */ { MDBP_R(4139); }
        | TO_SYM REGISTER_SYM { MDBP_R(4140); }
        ;

mrs_create_rest_user_statement:
          mrs_create_rest_user_prefix mrs_user_name '@' mrs_auth_app_name mrs_opt_identified_by mrs_user_options { MDBP_R(4141); }
        ;

mrs_create_rest_user_prefix:
          CREATE OR_SYM REPLACE REST_SYM USER_SYM { MDBP_R(4142); }
        | CREATE REST_SYM USER_SYM mrs_opt_if_not_exists { MDBP_R(4143); }
        ;

mrs_opt_identified_by:
          /* empty */ { MDBP_R(4144); }
        | IDENTIFIED_SYM BY mrs_user_password { MDBP_R(4145); }
        ;

mrs_user_name:
          mrs_text_or_identifier { MDBP_R(4146); }
        ;

mrs_user_password:
          TEXT_STRING { MDBP_R(4147); }
        ;

mrs_user_options:
          /* empty */ { MDBP_R(4148); }
        | mrs_user_options ACCOUNT_SYM LOCK_SYM { MDBP_R(4149); }
        | mrs_user_options ACCOUNT_SYM UNLOCK_SYM { MDBP_R(4150); }
        | mrs_user_options APP_SYM OPTIONS_SYM mrs_json_value { MDBP_R(4151); }
        | mrs_user_options mrs_json_options { MDBP_R(4152); }
        ;

mrs_create_rest_role_statement:
          mrs_create_rest_role_prefix mrs_role_name mrs_opt_extends mrs_opt_role_service mrs_rest_role_options { MDBP_R(4153); }
        ;

mrs_create_rest_role_prefix:
          CREATE OR_SYM REPLACE REST_SYM ROLE_SYM { MDBP_R(4154); }
        | CREATE REST_SYM ROLE_SYM mrs_opt_if_not_exists { MDBP_R(4155); }
        ;

mrs_opt_extends:
          /* empty */ { MDBP_R(4156); }
        | EXTENDS_SYM mrs_parent_role_name { MDBP_R(4157); }
        ;

mrs_rest_role_options:
          /* empty */ { MDBP_R(4158); }
        | mrs_rest_role_options mrs_json_options { MDBP_R(4159); }
        | mrs_rest_role_options mrs_comments { MDBP_R(4160); }
        ;

mrs_parent_role_name:
          mrs_text_or_identifier { MDBP_R(4161); }
        ;

mrs_role_name:
          mrs_text_or_identifier { MDBP_R(4162); }
        ;

mrs_clone_rest_service_statement:
          CLONE_SYM REST_SYM SERVICE_SYM mrs_service_request_path NEW_SYM REQUEST_SYM PATH_SYM mrs_new_service_request_path { MDBP_R(4163); }
        ;

mrs_alter_rest_service_statement:
          ALTER REST_SYM SERVICE_SYM mrs_service_request_path mrs_rest_service_options { MDBP_R(4164); }
        | ALTER REST_SYM SERVICE_SYM mrs_service_request_path NEW_SYM REQUEST_SYM PATH_SYM mrs_new_service_request_path mrs_rest_service_options { MDBP_R(4165); }
        ;

mrs_alter_rest_schema_statement:
          ALTER REST_SYM DATABASE mrs_opt_schema_request_path mrs_opt_on_service mrs_opt_new_request_path mrs_opt_from_schema_name mrs_rest_schema_options { MDBP_R(4166); }
        ;

mrs_opt_new_request_path:
          /* empty */ { MDBP_R(4167); }
        | NEW_SYM REQUEST_SYM PATH_SYM mrs_request_path_identifier { MDBP_R(4168); }
        ;

mrs_opt_from_schema_name:
          /* empty */ { MDBP_R(4169); }
        | FROM mrs_identifier { MDBP_R(4170); }
        ;

mrs_alter_rest_view_statement:
          ALTER REST_SYM mrs_opt_data_mapping VIEW_SYM mrs_view_request_path mrs_opt_on_service_schema_selector mrs_opt_new_request_path mrs_opt_alter_view_class mrs_rest_object_options { MDBP_R(4171); }
        ;

mrs_opt_alter_view_class:
          /* empty */ { MDBP_R(4172); }
        | CLASS_SYM mrs_rest_object_name mrs_opt_graphql_crud_options mrs_opt_graphql_obj { MDBP_R(4173); }
        ;

mrs_alter_rest_procedure_statement:
          ALTER REST_SYM PROCEDURE_SYM mrs_procedure_request_path mrs_opt_on_service_schema_selector mrs_opt_new_request_path mrs_opt_rest_parameters mrs_rest_results mrs_rest_object_options { MDBP_R(4174); }
        ;

mrs_alter_rest_function_statement:
          ALTER REST_SYM FUNCTION_SYM mrs_function_request_path mrs_opt_on_service_schema_selector mrs_opt_new_request_path mrs_opt_rest_parameters mrs_rest_results mrs_rest_object_options { MDBP_R(4175); }
        ;

mrs_alter_rest_content_set_statement:
          ALTER REST_SYM CONTENT_SYM SET mrs_content_set_request_path mrs_opt_on_service mrs_opt_new_request_path mrs_alter_rest_content_set_options { MDBP_R(4176); }
        ;

mrs_alter_rest_auth_app_statement:
          ALTER REST_SYM mrs_auth_or_authentication APP_SYM mrs_auth_app_name mrs_opt_new_service_name mrs_rest_auth_app_options { MDBP_R(4177); }
        ;

mrs_opt_new_service_name:
          /* empty */ { MDBP_R(4178); }
        | NEW_SYM NAME_SYM mrs_new_auth_app_name { MDBP_R(4179); }
        ;

mrs_new_auth_app_name:
          mrs_text_or_identifier { MDBP_R(4180); }
        ;

mrs_alter_rest_user_statement:
          ALTER REST_SYM USER_SYM mrs_user_name '@' mrs_auth_app_name mrs_opt_identified_by mrs_user_options { MDBP_R(4181); }
        ;

mrs_drop_rest_service_statement:
          DROP REST_SYM SERVICE_SYM mrs_opt_if_exists mrs_service_request_path { MDBP_R(4182); }
        ;

mrs_drop_rest_schema_statement:
          DROP REST_SYM DATABASE mrs_opt_if_exists mrs_schema_request_path mrs_opt_from_service { MDBP_R(4183); }
        ;

mrs_drop_rest_view_statement:
          DROP REST_SYM mrs_opt_data_mapping VIEW_SYM mrs_opt_if_exists mrs_view_request_path mrs_opt_from_service_schema_selector { MDBP_R(4184); }
        ;

mrs_drop_rest_procedure_statement:
          DROP REST_SYM PROCEDURE_SYM mrs_opt_if_exists mrs_procedure_request_path mrs_opt_from_service_schema_selector { MDBP_R(4185); }
        ;

mrs_drop_rest_function_statement:
          DROP REST_SYM FUNCTION_SYM mrs_opt_if_exists mrs_function_request_path mrs_opt_from_service_schema_selector { MDBP_R(4186); }
        ;

mrs_drop_rest_content_set_statement:
          DROP REST_SYM CONTENT_SYM SET mrs_opt_if_exists mrs_content_set_request_path mrs_opt_from_service { MDBP_R(4187); }
        ;

mrs_drop_rest_content_file_statement:
          DROP REST_SYM CONTENT_SYM FILE_SYM mrs_opt_if_exists mrs_content_file_request_path FROM mrs_opt_service_request_path CONTENT_SYM SET mrs_content_set_request_path { MDBP_R(4188); }
        ;

mrs_drop_rest_auth_app_statement:
          DROP REST_SYM mrs_auth_or_authentication APP_SYM mrs_opt_if_exists mrs_auth_app_name { MDBP_R(4189); }
        ;

mrs_drop_rest_user_statement:
          DROP REST_SYM USER_SYM mrs_opt_if_exists mrs_user_name '@' mrs_auth_app_name { MDBP_R(4190); }
        ;

mrs_drop_rest_role_statement:
          DROP REST_SYM ROLE_SYM mrs_opt_if_exists mrs_role_name mrs_opt_role_service { MDBP_R(4191); }
        ;

mrs_grant_rest_privilege_statement:
          GRANT clear_privileges REST_SYM mrs_privilege_list mrs_privilege_target TO_SYM mrs_role_name mrs_opt_role_service { MDBP_R(4192); }
        ;

mrs_privilege_target:
          /* empty */ { MDBP_R(4193); }
        | ON mrs_service_request_path_wildcard { MDBP_R(4194); }
        | ON SERVICE_SYM mrs_service_request_path_wildcard { MDBP_R(4195); }
        | ON SERVICE_SYM mrs_service_request_path_wildcard DATABASE mrs_schema_request_path_wildcard mrs_opt_object_wildcard { MDBP_R(4196); }
        | ON DATABASE mrs_schema_request_path_wildcard mrs_opt_object_wildcard { MDBP_R(4197); }
        ;

mrs_opt_object_wildcard:
          /* empty */ { MDBP_R(4198); }
        | OBJECT_SYM mrs_object_request_path_wildcard { MDBP_R(4199); }
        ;

mrs_privilege_list:
          mrs_privilege_name { MDBP_R(4200); }
        | mrs_privilege_list ',' mrs_privilege_name { MDBP_R(4201); }
        ;

mrs_privilege_name:
          CREATE { MDBP_R(4202); }
        | READ_SYM { MDBP_R(4203); }
        | UPDATE_SYM { MDBP_R(4204); }
        | DELETE_SYM { MDBP_R(4205); }
        ;

mrs_grant_rest_role_statement:
          GRANT clear_privileges REST_SYM ROLE_SYM mrs_role_name mrs_opt_role_service TO_SYM mrs_user_name '@' mrs_auth_app_name mrs_opt_comments { MDBP_R(4206); }
        ;

mrs_revoke_rest_privilege_statement:
          REVOKE clear_privileges REST_SYM mrs_privilege_list mrs_privilege_target FROM mrs_role_name mrs_opt_role_service { MDBP_R(4207); }
        ;

mrs_revoke_rest_role_statement:
          REVOKE clear_privileges REST_SYM ROLE_SYM mrs_role_name mrs_opt_role_service FROM mrs_user_name '@' mrs_auth_app_name { MDBP_R(4208); }
        ;

mrs_use_statement:
          USE_SYM REST_SYM mrs_service_and_schema_request_paths { MDBP_R(4209); }
        | USE_SYM REST_SYM METADATA_SYM mrs_metadata_schema { MDBP_R(4210); }
        ;

mrs_metadata_schema:
          DATABASE mrs_identifier { MDBP_R(4211); }
        ;

mrs_service_and_schema_request_paths:
          SERVICE_SYM mrs_service_request_path { MDBP_R(4212); }
        | mrs_service_schema_selector { MDBP_R(4213); }
        ;

mrs_show_rest_metadata_status_statement:
          REST_SYM METADATA_SYM STATUS_SYM mrs_opt_output_format { MDBP_R(4214); }
        | REST_SYM STATUS_SYM mrs_opt_output_format { MDBP_R(4215); }
        ;

mrs_show_rest_metadata_schemas_statement:
          REST_SYM METADATA_SYM DATABASES mrs_opt_output_format { MDBP_R(4216); }
        ;

mrs_show_rest_services_statement:
          REST_SYM SERVICES_SYM mrs_opt_for_auth_app mrs_opt_output_format { MDBP_R(4217); }
        | REST_SYM SERVICES_SYM FOR_SYM DAEMON_SYM mrs_daemon_id mrs_opt_output_format { MDBP_R(4218); }
        ;

mrs_show_rest_daemons_statement:
          REST_SYM DAEMONS_SYM mrs_opt_output_format { MDBP_R(4219); }
        ;

mrs_drop_rest_daemon_statement:
          DROP REST_SYM DAEMON_SYM mrs_opt_if_exists mrs_daemon_id { MDBP_R(4220); }
        ;

mrs_daemon_id:
          TEXT_STRING { MDBP_R(4221); }
        ;

mrs_opt_for_auth_app:
          /* empty */ { MDBP_R(4222); }
        | FOR_SYM AUTH_SYM APP_SYM mrs_auth_app_name { MDBP_R(4223); }
        ;

mrs_show_rest_schemas_statement:
          REST_SYM DATABASES mrs_opt_on_from_service mrs_opt_output_format { MDBP_R(4224); }
        ;

mrs_show_rest_views_statement:
          REST_SYM mrs_opt_data_mapping VIEWS_SYM mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4225); }
        ;

mrs_show_rest_procedures_statement:
          REST_SYM PROCEDURES_SYM mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4226); }
        ;

mrs_show_rest_functions_statement:
          REST_SYM FUNCTIONS_SYM mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4227); }
        ;

mrs_show_rest_scripts_statement:
          REST_SYM SCRIPTS_SYM mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4228); }
        ;

mrs_show_rest_content_sets_statement:
          REST_SYM CONTENT_SYM SETS_SYM mrs_opt_on_from_service mrs_opt_output_format { MDBP_R(4229); }
        ;

mrs_show_rest_content_files_statement:
          REST_SYM CONTENT_SYM FILES_SYM mrs_on_or_from mrs_opt_service_request_path CONTENT_SYM SET mrs_content_set_request_path mrs_opt_output_format { MDBP_R(4230); }
        ;

mrs_on_or_from:
          ON { MDBP_R(4231); }
        | FROM { MDBP_R(4232); }
        ;

mrs_show_rest_auth_apps_statement:
          REST_SYM AUTH_SYM APPS_SYM mrs_opt_on_from_role_service mrs_opt_output_format { MDBP_R(4233); }
        ;

mrs_show_rest_auth_vendors_statement:
          REST_SYM AUTH_SYM VENDORS_SYM mrs_opt_output_format { MDBP_R(4234); }
        ;

mrs_show_rest_users_statement:
          REST_SYM USERS_SYM mrs_opt_on_from_service mrs_opt_for_auth_app mrs_opt_output_format { MDBP_R(4235); }
        ;

mrs_show_rest_columns_statement:
          REST_SYM COLUMNS mrs_from_or_in mrs_opt_columns_source mrs_qualified_identifier mrs_opt_output_format { MDBP_R(4236); }
        ;

mrs_from_or_in:
          FROM { MDBP_R(4237); }
        | IN_SYM { MDBP_R(4238); }
        ;

mrs_opt_columns_source:
          /* empty */ { MDBP_R(4239); }
        | TABLE_SYM { MDBP_R(4240); }
        | VIEW_SYM { MDBP_R(4241); }
        | PROCEDURE_SYM { MDBP_R(4242); }
        | FUNCTION_SYM { MDBP_R(4243); }
        ;

mrs_show_rest_roles_statement:
          REST_SYM ROLES_SYM mrs_opt_on_from_role_service mrs_opt_output_format { MDBP_R(4244); }
        | REST_SYM ROLES_SYM mrs_opt_on_from_role_service FOR_SYM '@' mrs_auth_app_name mrs_opt_output_format { MDBP_R(4245); }
        | REST_SYM ROLES_SYM mrs_opt_on_from_role_service FOR_SYM mrs_user_name '@' mrs_auth_app_name mrs_opt_output_format { MDBP_R(4246); }
        ;

mrs_show_rest_grants_statement:
          REST_SYM GRANTS FOR_SYM mrs_role_name mrs_opt_on_from_role_service { MDBP_R(4247); }
        ;

mrs_show_create_rest_service_statement:
          CREATE REST_SYM SERVICE_SYM mrs_opt_including_endpoints mrs_opt_output_format { MDBP_R(4248); }
        | CREATE REST_SYM SERVICE_SYM mrs_service_request_path mrs_opt_including_endpoints mrs_opt_output_format { MDBP_R(4249); }
        ;

mrs_opt_including_endpoints:
          /* empty */ { MDBP_R(4250); }
        | INCLUDING_SYM mrs_endpoint_selection ENDPOINTS_SYM { MDBP_R(4251); }
        ;

mrs_opt_output_format:
          /* empty */ { MDBP_R(4252); }
        | FORMAT_SYM '=' JSON_SYM { MDBP_R(4253); }
        | FORMAT_SYM '=' mrs_text_or_identifier { MDBP_R(4254); }
        ;

mrs_show_create_rest_schema_statement:
          CREATE REST_SYM DATABASE mrs_opt_schema_request_path mrs_opt_on_from_service mrs_opt_output_format { MDBP_R(4255); }
        ;

mrs_show_create_rest_view_statement:
          CREATE REST_SYM mrs_opt_data_mapping VIEW_SYM mrs_view_request_path mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4256); }
        ;

mrs_show_create_rest_procedure_statement:
          CREATE REST_SYM PROCEDURE_SYM mrs_procedure_request_path mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4257); }
        ;

mrs_show_create_rest_function_statement:
          CREATE REST_SYM FUNCTION_SYM mrs_function_request_path mrs_opt_on_from_service_schema_selector mrs_opt_output_format { MDBP_R(4258); }
        ;

mrs_show_create_rest_content_set_statement:
          CREATE REST_SYM CONTENT_SYM SET mrs_content_set_request_path mrs_opt_on_from_service mrs_opt_output_format { MDBP_R(4259); }
        ;

mrs_show_create_rest_content_file_statement:
          CREATE REST_SYM CONTENT_SYM FILE_SYM mrs_content_file_request_path mrs_on_or_from mrs_opt_service_request_path CONTENT_SYM SET mrs_content_set_request_path mrs_opt_output_format { MDBP_R(4260); }
        ;

mrs_show_create_rest_auth_app_statement:
          CREATE REST_SYM AUTH_SYM APP_SYM mrs_auth_app_name mrs_opt_output_format { MDBP_R(4261); }
        ;

mrs_show_create_rest_role_statement:
          CREATE REST_SYM ROLE_SYM mrs_role_name mrs_opt_role_service mrs_opt_output_format { MDBP_R(4262); }
        ;

mrs_show_create_rest_user_statement:
          CREATE REST_SYM USER_SYM mrs_user_name '@' mrs_auth_app_name mrs_opt_output_format { MDBP_R(4263); }
        ;

mrs_endpoint_selection:
          DATABASE { MDBP_R(4264); }
        | DATABASE AND_SYM STATIC_SYM { MDBP_R(4265); }
        | DATABASE AND_SYM STATIC_SYM AND_SYM DYNAMIC_SYM { MDBP_R(4266); }
        | ALL { MDBP_R(4267); }
        ;

mrs_service_request_path:
          mrs_request_path_identifier { MDBP_R(4268); }
        | mrs_service_developers_identifier mrs_request_path_identifier { MDBP_R(4269); }
        ;

mrs_new_service_request_path:
          mrs_service_request_path { MDBP_R(4270); }
        ;

mrs_service_request_path_wildcard:
          mrs_request_path_identifier_with_wildcard { MDBP_R(4271); }
        ;

mrs_schema_request_path:
          mrs_request_path_identifier { MDBP_R(4272); }
        ;

mrs_schema_request_path_wildcard:
          mrs_request_path_identifier_with_wildcard { MDBP_R(4273); }
        ;

mrs_view_request_path:
          mrs_request_path_identifier { MDBP_R(4274); }
        ;

mrs_rest_object_name:
          mrs_identifier { MDBP_R(4275); }
        ;

mrs_rest_result_name:
          mrs_identifier { MDBP_R(4276); }
        ;

mrs_object_request_path_wildcard:
          mrs_request_path_identifier_with_wildcard { MDBP_R(4277); }
        ;

mrs_procedure_request_path:
          mrs_request_path_identifier { MDBP_R(4278); }
        ;

mrs_function_request_path:
          mrs_request_path_identifier { MDBP_R(4279); }
        ;

mrs_content_set_request_path:
          mrs_request_path_identifier { MDBP_R(4280); }
        ;

mrs_content_file_request_path:
          mrs_request_path_identifier { MDBP_R(4281); }
        ;

mrs_service_developer_identifier:
          mrs_text_or_identifier { MDBP_R(4282); }
        ;

mrs_service_developers_identifier:
          mrs_service_developer_list '@' LEX_HOSTNAME { MDBP_R(4283); }
        ;

mrs_service_developer_list:
          mrs_service_developer_identifier { MDBP_R(4284); }
        | mrs_service_developer_list ',' mrs_service_developer_identifier { MDBP_R(4285); }
        ;

mrs_request_path_identifier:
          mrs_request_path { MDBP_R(4286); }
        | IDENT_QUOTED { MDBP_R(4287); }
        | TEXT_STRING { MDBP_R(4288); }
        ;

mrs_request_path_identifier_with_wildcard:
          mrs_request_path { MDBP_R(4289); }
        | IDENT_QUOTED { MDBP_R(4290); }
        | TEXT_STRING { MDBP_R(4291); }
        ;

mrs_json_obj:
          '{' mrs_json_pairs '}' { MDBP_R(4292); }
        | '{' '}' { MDBP_R(4293); }
        ;

mrs_json_pairs:
          mrs_json_pair { MDBP_R(4294); }
        | mrs_json_pairs ',' mrs_json_pair { MDBP_R(4295); }
        ;

mrs_json_pair:
          TEXT_STRING ':' mrs_json_value { MDBP_R(4296); }
        ;

mrs_json_arr:
          '[' mrs_json_values ']' { MDBP_R(4297); }
        | '[' ']' { MDBP_R(4298); }
        ;

mrs_json_values:
          mrs_json_value { MDBP_R(4299); }
        | mrs_json_values ',' mrs_json_value { MDBP_R(4300); }
        ;

mrs_json_value:
          TEXT_STRING { MDBP_R(4301); }
        | mrs_json_number { MDBP_R(4302); }
        | '-' mrs_json_number { MDBP_R(4303); }
        | '+' mrs_json_number { MDBP_R(4304); }
        | mrs_json_obj { MDBP_R(4305); }
        | mrs_json_arr { MDBP_R(4306); }
        | TRUE_SYM { MDBP_R(4307); }
        | FALSE_SYM { MDBP_R(4308); }
        | NULL_SYM { MDBP_R(4309); }
        ;

mrs_json_number:
          mrs_int_number { MDBP_R(4310); }
        | DECIMAL_NUM { MDBP_R(4311); }
        | FLOAT_NUM { MDBP_R(4312); }
        ;

mrs_graphql_obj:
          '{' mrs_graphql_pairs '}' { MDBP_R(4313); }
        | '{' '}' { MDBP_R(4314); }
        ;

mrs_opt_graphql_obj:
          /* empty */ { MDBP_R(4315); }
        | mrs_graphql_obj { MDBP_R(4316); }
        ;

mrs_graphql_pairs:
          mrs_graphql_pair { MDBP_R(4317); }
        | mrs_graphql_pairs ',' mrs_graphql_pair { MDBP_R(4318); }
        ;

mrs_graphql_crud_options:
          mrs_annotation { MDBP_R(4319); }
        | mrs_graphql_crud_options mrs_annotation { MDBP_R(4320); }
        ;

mrs_opt_graphql_crud_options:
          /* empty */ { MDBP_R(4321); }
        | mrs_graphql_crud_options { MDBP_R(4322); }
        ;

mrs_graphql_pair:
          mrs_graphql_pair_key ':' mrs_graphql_pair_value mrs_graphql_annotations mrs_opt_graphql_value_json_schema mrs_opt_graphql_obj { MDBP_R(4323); }
        ;

mrs_graphql_datatype_text:
          TEXT_STRING { MDBP_R(4324); }
        | mrs_identifier { MDBP_R(4325); }
        ;

mrs_opt_graphql_value_json_schema:
          /* empty */ { MDBP_R(4326); }
        | JSON_SYM DATABASE mrs_json_value { MDBP_R(4327); }
        ;

mrs_graphql_allowed_keyword:
          CREATE { MDBP_R(4328); }
        | OR_SYM { MDBP_R(4329); }
        | REPLACE { MDBP_R(4330); }
        | ALTER { MDBP_R(4331); }
        | SHOW { MDBP_R(4332); }
        | STATUS_SYM { MDBP_R(4333); }
        | NEW_SYM { MDBP_R(4334); }
        | ON { MDBP_R(4335); }
        | FROM { MDBP_R(4336); }
        | IN_SYM { MDBP_R(4337); }
        | DATABASES { MDBP_R(4338); }
        | DATABASE { MDBP_R(4339); }
        | JSON_SYM { MDBP_R(4340); }
        | VIEW_SYM { MDBP_R(4341); }
        | PROCEDURE_SYM { MDBP_R(4342); }
        | FUNCTION_SYM { MDBP_R(4343); }
        | DROP { MDBP_R(4344); }
        | USE_SYM { MDBP_R(4345); }
        | AS { MDBP_R(4346); }
        | FILTER_SYM { MDBP_R(4347); }
        | AUTHENTICATION_SYM { MDBP_R(4348); }
        | PATH_SYM { MDBP_R(4349); }
        | VALIDATION_SYM { MDBP_R(4350); }
        | DEFAULT { MDBP_R(4351); }
        | USER_SYM { MDBP_R(4352); }
        | OPTIONS_SYM { MDBP_R(4353); }
        | IF_SYM { MDBP_R(4354); }
        | NOT_SYM { MDBP_R(4355); }
        | EXISTS { MDBP_R(4356); }
        | PAGE_SYM { MDBP_R(4357); }
        | HOST_SYM { MDBP_R(4358); }
        | TYPE_SYM { MDBP_R(4359); }
        | FORMAT_SYM { MDBP_R(4360); }
        | UPDATE_SYM { MDBP_R(4361); }
        | NULL_SYM { MDBP_R(4362); }
        | TRUE_SYM { MDBP_R(4363); }
        | FALSE_SYM { MDBP_R(4364); }
        | SET { MDBP_R(4365); }
        | IDENTIFIED_SYM { MDBP_R(4366); }
        | BY { MDBP_R(4367); }
        | ROLE_SYM { MDBP_R(4368); }
        | TO_SYM { MDBP_R(4369); }
        | CLONE_SYM { MDBP_R(4370); }
        | FILE_SYM { MDBP_R(4371); }
        | BINARY { MDBP_R(4372); }
        | DATA_SYM { MDBP_R(4373); }
        | LOAD { MDBP_R(4374); }
        | GRANT { MDBP_R(4375); }
        | READ_SYM { MDBP_R(4376); }
        | DELETE_SYM { MDBP_R(4377); }
        | GROUP_SYM { MDBP_R(4378); }
        | REVOKE { MDBP_R(4379); }
        | ACCOUNT_SYM { MDBP_R(4380); }
        | LOCK_SYM { MDBP_R(4381); }
        | UNLOCK_SYM { MDBP_R(4382); }
        | GRANTS { MDBP_R(4383); }
        | FOR_SYM { MDBP_R(4384); }
        | LEVEL_SYM { MDBP_R(4385); }
        | ANY_SYM { MDBP_R(4386); }
        | CLIENT_SYM { MDBP_R(4387); }
        | URL_SYM { MDBP_R(4388); }
        | NAME_SYM { MDBP_R(4389); }
        | DO_SYM { MDBP_R(4390); }
        | CONFIGURE_SYM { MDBP_R(4391); }
        | REST_SYM { MDBP_R(4392); }
        | METADATA_SYM { MDBP_R(4393); }
        | SERVICES_SYM { MDBP_R(4394); }
        | SERVICE_SYM { MDBP_R(4395); }
        | VIEWS_SYM { MDBP_R(4396); }
        | PROCEDURES_SYM { MDBP_R(4397); }
        | PARAMETERS_SYM { MDBP_R(4398); }
        | FUNCTIONS_SYM { MDBP_R(4399); }
        | RESULT_SYM { MDBP_R(4400); }
        | ENABLED_SYM { MDBP_R(4401); }
        | PUBLISHED_SYM { MDBP_R(4402); }
        | DISABLED_SYM { MDBP_R(4403); }
        | PRIVATE_SYM { MDBP_R(4404); }
        | UNPUBLISHED_SYM { MDBP_R(4405); }
        | PROTOCOL_SYM { MDBP_R(4406); }
        | HTTP_SYM { MDBP_R(4407); }
        | HTTPS_SYM { MDBP_R(4408); }
        | COMMENT_SYM { MDBP_R(4409); }
        | REQUEST_SYM { MDBP_R(4410); }
        | REDIRECTION_SYM { MDBP_R(4411); }
        | MANAGEMENT_SYM { MDBP_R(4412); }
        | AVAILABLE_SYM { MDBP_R(4413); }
        | REQUIRED_SYM { MDBP_R(4414); }
        | ITEMS_SYM { MDBP_R(4415); }
        | PER_SYM { MDBP_R(4416); }
        | CONTENT_SYM { MDBP_R(4417); }
        | MEDIA_SYM { MDBP_R(4418); }
        | AUTODETECT_SYM { MDBP_R(4419); }
        | FEED_SYM { MDBP_R(4420); }
        | ITEM_SYM { MDBP_R(4421); }
        | SETS_SYM { MDBP_R(4422); }
        | AUTH_SYM { MDBP_R(4423); }
        | APPS_SYM { MDBP_R(4424); }
        | APP_SYM { MDBP_R(4425); }
        | ID_SYM { MDBP_R(4426); }
        | SECRET_SYM { MDBP_R(4427); }
        | VENDOR_SYM { MDBP_R(4428); }
        | MRS_SYM { MDBP_R(4429); }
        | MARIADB_SYM { MDBP_R(4430); }
        | USERS_SYM { MDBP_R(4431); }
        | ALLOW_SYM { MDBP_R(4432); }
        | REGISTER_SYM { MDBP_R(4433); }
        | CLASS_SYM { MDBP_R(4434); }
        | DEVELOPMENT_SYM { MDBP_R(4435); }
        | SCRIPTS_SYM { MDBP_R(4436); }
        | MAPPING_SYM { MDBP_R(4437); }
        | TYPESCRIPT_SYM { MDBP_R(4438); }
        | ROLES_SYM { MDBP_R(4439); }
        | EXTENDS_SYM { MDBP_R(4440); }
        | OBJECT_SYM { MDBP_R(4441); }
        | HIERARCHY_SYM { MDBP_R(4442); }
        | TABLE_SYM { MDBP_R(4443); }
        ;

mrs_graphql_pair_key:
          TEXT_STRING { MDBP_R(4444); }
        | mrs_identifier { MDBP_R(4445); }
        | mrs_graphql_allowed_keyword { MDBP_R(4446); }
        ;

mrs_graphql_pair_value:
          mrs_qualified_identifier { MDBP_R(4447); }
        | mrs_graphql_allowed_keyword { MDBP_R(4448); }
        ;

mrs_graphql_annotations:
          /* empty */ { MDBP_R(4449); }
        | mrs_graphql_annotations mrs_graphql_annotation { MDBP_R(4450); }
        ;

mrs_graphql_annotation:
          mrs_annotation { MDBP_R(4451); }
        | mrs_annotation '(' mrs_graphql_datatype_text ')' { MDBP_R(4452); }
        ;

mrs_identifier:
          IDENT_sys { MDBP_R(4453); }
        | ACTION { MDBP_R(4454); }
        | ADDDATE_SYM { MDBP_R(4455); }
        | ADMIN_SYM { MDBP_R(4456); }
        | AFTER_SYM { MDBP_R(4457); }
        | AGAINST { MDBP_R(4458); }
        | AGGREGATE_SYM { MDBP_R(4459); }
        | ALGORITHM_SYM { MDBP_R(4460); }
        | ALWAYS_SYM { MDBP_R(4461); }
        | ANALYZE_SYM { MDBP_R(4462); }
        | ARRAY_SYM { MDBP_R(4463); }
        | ASC { MDBP_R(4464); }
        | ASCII_SYM { MDBP_R(4465); }
        | ASENSITIVE_SYM { MDBP_R(4466); }
        | ATOMIC_SYM { MDBP_R(4467); }
        | AT_SYM { MDBP_R(4468); }
        | AUTHORIZATION_SYM { MDBP_R(4469); }
        | AUTHORS_SYM { MDBP_R(4470); }
        | AUTO_INC { MDBP_R(4471); }
        | AUTO_SYM { MDBP_R(4472); }
        | AVG_ROW_LENGTH { MDBP_R(4473); }
        | AVG_SYM { MDBP_R(4474); }
        | BACKUP_SYM { MDBP_R(4475); }
        | BEFORE_SYM { MDBP_R(4476); }
        | BEGIN_MARIADB_SYM { MDBP_R(4477); }
        | BETWEEN_SYM { MDBP_R(4478); }
        | BIGINT { MDBP_R(4479); }
        | BINLOG_SYM { MDBP_R(4480); }
        | BIT_AND { MDBP_R(4481); }
        | BIT_OR { MDBP_R(4482); }
        | BIT_SYM { MDBP_R(4483); }
        | BIT_XOR { MDBP_R(4484); }
        | BLOB_MARIADB_SYM { MDBP_R(4485); }
        | BLOCK_SYM { MDBP_R(4486); }
        | BODY_MARIADB_SYM { MDBP_R(4487); }
        | BOOLEAN_SYM { MDBP_R(4488); }
        | BOOL_SYM { MDBP_R(4489); }
        | BOTH { MDBP_R(4490); }
        | BTREE_SYM { MDBP_R(4491); }
        | BYTE_SYM { MDBP_R(4492); }
        | CACHE_SYM { MDBP_R(4493); }
        | CALL_SYM { MDBP_R(4494); }
        | CASCADE { MDBP_R(4495); }
        | CASCADED { MDBP_R(4496); }
        | CASE_SYM { MDBP_R(4497); }
        | CAST_SYM { MDBP_R(4498); }
        | CATALOG_NAME_SYM { MDBP_R(4499); }
        | CHAIN_SYM { MDBP_R(4500); }
        | CHANGE { MDBP_R(4501); }
        | CHANGED { MDBP_R(4502); }
        | CHANNEL_SYM { MDBP_R(4503); }
        | CHARSET { MDBP_R(4504); }
        | CHAR_SYM { MDBP_R(4505); }
        | CHECKPOINT_SYM { MDBP_R(4506); }
        | CHECKSUM_SYM { MDBP_R(4507); }
        | CHECK_SYM { MDBP_R(4508); }
        | CIPHER_SYM { MDBP_R(4509); }
        | CLASS_ORIGIN_SYM { MDBP_R(4510); }
        | CLOB_MARIADB_SYM { MDBP_R(4511); }
        | CLOSE_SYM { MDBP_R(4512); }
        | COALESCE { MDBP_R(4513); }
        | CODE_SYM { MDBP_R(4514); }
        | COLLATE_SYM { MDBP_R(4515); }
        | COLLATION_SYM { MDBP_R(4516); }
        | COLUMNS { MDBP_R(4517); }
        | COLUMN_ADD_SYM { MDBP_R(4518); }
        | COLUMN_CHECK_SYM { MDBP_R(4519); }
        | COLUMN_CREATE_SYM { MDBP_R(4520); }
        | COLUMN_DELETE_SYM { MDBP_R(4521); }
        | COLUMN_GET_SYM { MDBP_R(4522); }
        | COLUMN_NAME_SYM { MDBP_R(4523); }
        | COLUMN_SYM { MDBP_R(4524); }
        | COMMITTED_SYM { MDBP_R(4525); }
        | COMMIT_SYM { MDBP_R(4526); }
        | COMPACT_SYM { MDBP_R(4527); }
        | COMPLETION_SYM { MDBP_R(4528); }
        | COMPRESSED_SYM { MDBP_R(4529); }
        | CONCURRENT { MDBP_R(4530); }
        | CONDITION_SYM { MDBP_R(4531); }
        | CONNECTION_SYM { MDBP_R(4532); }
        | CONSISTENT_SYM { MDBP_R(4533); }
        | CONSTRAINT { MDBP_R(4534); }
        | CONSTRAINT_CATALOG_SYM { MDBP_R(4535); }
        | CONSTRAINT_NAME_SYM { MDBP_R(4536); }
        | CONSTRAINT_SCHEMA_SYM { MDBP_R(4537); }
        | CONTAINS_SYM { MDBP_R(4538); }
        | CONTEXT_SYM { MDBP_R(4539); }
        | CONTINUE_MARIADB_SYM { MDBP_R(4540); }
        | CONTRIBUTORS_SYM { MDBP_R(4541); }
        | CONVERSION_SYM { MDBP_R(4542); }
        | CONVERT_SYM { MDBP_R(4543); }
        | COUNT_SYM { MDBP_R(4544); }
        | CPU_SYM { MDBP_R(4545); }
        | CROSS { MDBP_R(4546); }
        | CUBE_SYM { MDBP_R(4547); }
        | CUME_DIST_SYM { MDBP_R(4548); }
        | CURDATE { MDBP_R(4549); }
        | CURRENT_PATH { MDBP_R(4550); }
        | CURRENT_POS_SYM { MDBP_R(4551); }
        | CURRENT_ROLE { MDBP_R(4552); }
        | CURRENT_SYM { MDBP_R(4553); }
        | CURRENT_USER { MDBP_R(4554); }
        | CURSOR_NAME_SYM { MDBP_R(4555); }
        | CURSOR_SYM { MDBP_R(4556); }
        | CURTIME { MDBP_R(4557); }
        | CYCLE_SYM { MDBP_R(4558); }
        | DAEMONS_SYM { MDBP_R(4559); }
        | DAEMON_SYM { MDBP_R(4560); }
        | DATETIME { MDBP_R(4561); }
        | DATE_ADD_INTERVAL { MDBP_R(4562); }
        | DATE_SUB_INTERVAL { MDBP_R(4563); }
        | DATE_SYM { MDBP_R(4564); }
        | DAY_HOUR_SYM { MDBP_R(4565); }
        | DAY_MICROSECOND_SYM { MDBP_R(4566); }
        | DAY_MINUTE_SYM { MDBP_R(4567); }
        | DAY_SECOND_SYM { MDBP_R(4568); }
        | DAY_SYM { MDBP_R(4569); }
        | DEALLOCATE_SYM { MDBP_R(4570); }
        | DECIMAL_SYM { MDBP_R(4571); }
        | DECLARE_MARIADB_SYM { MDBP_R(4572); }
        | DEFINER_SYM { MDBP_R(4573); }
        | DELAYED_SYM { MDBP_R(4574); }
        | DELAY_KEY_WRITE_SYM { MDBP_R(4575); }
        | DELETE_DOMAIN_ID_SYM { MDBP_R(4576); }
        | DENSE_RANK_SYM { MDBP_R(4577); }
        | DENY { MDBP_R(4578); }
        | DESC { MDBP_R(4579); }
        | DESCRIBE { MDBP_R(4580); }
        | DETERMINISTIC_SYM { MDBP_R(4581); }
        | DIAGNOSTICS_SYM { MDBP_R(4582); }
        | DIRECTORY_SYM { MDBP_R(4583); }
        | DISABLE_SYM { MDBP_R(4584); }
        | DISCARD { MDBP_R(4585); }
        | DISK_SYM { MDBP_R(4586); }
        | DISTINCT { MDBP_R(4587); }
        | DIV_SYM { MDBP_R(4588); }
        | DOUBLE_SYM { MDBP_R(4589); }
        | DO_DOMAIN_IDS_SYM { MDBP_R(4590); }
        | DUAL_SYM { MDBP_R(4591); }
        | DUMPFILE { MDBP_R(4592); }
        | DUPLICATE_SYM { MDBP_R(4593); }
        | EACH_SYM { MDBP_R(4594); }
        | ELSE { MDBP_R(4595); }
        | ELSEIF_MARIADB_SYM { MDBP_R(4596); }
        | ELSIF_MARIADB_SYM { MDBP_R(4597); }
        | EMPTY_SYM { MDBP_R(4598); }
        | ENABLE_SYM { MDBP_R(4599); }
        | ENCLOSED { MDBP_R(4600); }
        | END { MDBP_R(4601); }
        | ENDS_SYM { MDBP_R(4602); }
        | ENGINES_SYM { MDBP_R(4603); }
        | ENGINE_SYM { MDBP_R(4604); }
        | ENUM { MDBP_R(4605); }
        | ERRORS { MDBP_R(4606); }
        | ERROR_SYM { MDBP_R(4607); }
        | ESCAPED { MDBP_R(4608); }
        | ESCAPE_SYM { MDBP_R(4609); }
        | EVENTS_SYM { MDBP_R(4610); }
        | EVENT_SYM { MDBP_R(4611); }
        | EVERY_SYM { MDBP_R(4612); }
        | EXAMINED_SYM { MDBP_R(4613); }
        | EXCEPTION_MARIADB_SYM { MDBP_R(4614); }
        | EXCEPT_SYM { MDBP_R(4615); }
        | EXCHANGE_SYM { MDBP_R(4616); }
        | EXCLUDE_SYM { MDBP_R(4617); }
        | EXECUTE_SYM { MDBP_R(4618); }
        | EXIT_MARIADB_SYM { MDBP_R(4619); }
        | EXPANSION_SYM { MDBP_R(4620); }
        | EXPIRE_SYM { MDBP_R(4621); }
        | EXPORT_SYM { MDBP_R(4622); }
        | EXTENDED_SYM { MDBP_R(4623); }
        | EXTENT_SIZE_SYM { MDBP_R(4624); }
        | EXTRACT_SYM { MDBP_R(4625); }
        | FAST_SYM { MDBP_R(4626); }
        | FAULTS_SYM { MDBP_R(4627); }
        | FEDERATED_SYM { MDBP_R(4628); }
        | FETCH_SYM { MDBP_R(4629); }
        | FILES_SYM { MDBP_R(4630); }
        | FIRST_SYM { MDBP_R(4631); }
        | FIRST_VALUE_SYM { MDBP_R(4632); }
        | FIXED_SYM { MDBP_R(4633); }
        | FLOAT_SYM { MDBP_R(4634); }
        | FLUSH_SYM { MDBP_R(4635); }
        | FOLLOWING_SYM { MDBP_R(4636); }
        | FOLLOWS_SYM { MDBP_R(4637); }
        | FOREIGN { MDBP_R(4638); }
        | FOUND_SYM { MDBP_R(4639); }
        | FULL { MDBP_R(4640); }
        | FULLTEXT_SYM { MDBP_R(4641); }
        | GENERAL { MDBP_R(4642); }
        | GENERATED_SYM { MDBP_R(4643); }
        | GET_FORMAT { MDBP_R(4644); }
        | GET_SYM { MDBP_R(4645); }
        | GLOBAL_SYM { MDBP_R(4646); }
        | GOTO_MARIADB_SYM { MDBP_R(4647); }
        | GROUP_CONCAT_SYM { MDBP_R(4648); }
        | HANDLER_SYM { MDBP_R(4649); }
        | HARD_SYM { MDBP_R(4650); }
        | HASH_SYM { MDBP_R(4651); }
        | HAVING { MDBP_R(4652); }
        | HELP_SYM { MDBP_R(4653); }
        | HIGH_PRIORITY { MDBP_R(4654); }
        | HISTORY_SYM { MDBP_R(4655); }
        | HOSTS_SYM { MDBP_R(4656); }
        | HOUR_MICROSECOND_SYM { MDBP_R(4657); }
        | HOUR_MINUTE_SYM { MDBP_R(4658); }
        | HOUR_SECOND_SYM { MDBP_R(4659); }
        | HOUR_SYM { MDBP_R(4660); }
        | IGNORED_SYM { MDBP_R(4661); }
        | IGNORE_DOMAIN_IDS_SYM { MDBP_R(4662); }
        | IGNORE_SERVER_IDS_SYM { MDBP_R(4663); }
        | IGNORE_SYM { MDBP_R(4664); }
        | IMMEDIATE_SYM { MDBP_R(4665); }
        | IMPORT { MDBP_R(4666); }
        | INCREMENT_SYM { MDBP_R(4667); }
        | INDEXES { MDBP_R(4668); }
        | INDEX_SYM { MDBP_R(4669); }
        | INFILE { MDBP_R(4670); }
        | INNER_SYM { MDBP_R(4671); }
        | INOUT_SYM { MDBP_R(4672); }
        | INSENSITIVE_SYM { MDBP_R(4673); }
        | INSERT { MDBP_R(4674); }
        | INSERT_METHOD { MDBP_R(4675); }
        | INSTALL_SYM { MDBP_R(4676); }
        | INTERSECT_SYM { MDBP_R(4677); }
        | INTERVAL_SYM { MDBP_R(4678); }
        | INTO { MDBP_R(4679); }
        | INT_SYM { MDBP_R(4680); }
        | INVISIBLE_SYM { MDBP_R(4681); }
        | INVOKER_SYM { MDBP_R(4682); }
        | IO_SYM { MDBP_R(4683); }
        | IPC_SYM { MDBP_R(4684); }
        | IS { MDBP_R(4685); }
        | ISOLATION { MDBP_R(4686); }
        | ISOPEN_SYM { MDBP_R(4687); }
        | ISSUER_SYM { MDBP_R(4688); }
        | ITERATE_SYM { MDBP_R(4689); }
        | JOIN_SYM { MDBP_R(4690); }
        | JSON_ARRAYAGG_SYM { MDBP_R(4691); }
        | JSON_OBJECTAGG_SYM { MDBP_R(4692); }
        | JSON_TABLE_SYM { MDBP_R(4693); }
        | KEYS { MDBP_R(4694); }
        | KEY_BLOCK_SIZE { MDBP_R(4695); }
        | KEY_SYM { MDBP_R(4696); }
        | KILL_SYM { MDBP_R(4697); }
        | LAG_SYM { MDBP_R(4698); }
        | LANGUAGE_SYM { MDBP_R(4699); }
        | LASTVAL_SYM { MDBP_R(4700); }
        | LAST_SYM { MDBP_R(4701); }
        | LAST_VALUE { MDBP_R(4702); }
        | LEADING { MDBP_R(4703); }
        | LEAD_SYM { MDBP_R(4704); }
        | LEAVES { MDBP_R(4705); }
        | LEAVE_SYM { MDBP_R(4706); }
        | LEFT { MDBP_R(4707); }
        | LESS_SYM { MDBP_R(4708); }
        | LIKE { MDBP_R(4709); }
        | LIMIT { MDBP_R(4710); }
        | LINEAR_SYM { MDBP_R(4711); }
        | LINES { MDBP_R(4712); }
        | LIST_SYM { MDBP_R(4713); }
        | LOCALTIMESTAMP { MDBP_R(4714); }
        | LOCAL_SYM { MDBP_R(4715); }
        | LOCKED_SYM { MDBP_R(4716); }
        | LOCKS_SYM { MDBP_R(4717); }
        | LOGS_SYM { MDBP_R(4718); }
        | LOG_SYM { MDBP_R(4719); }
        | LONGBLOB { MDBP_R(4720); }
        | LONGTEXT { MDBP_R(4721); }
        | LONG_SYM { MDBP_R(4722); }
        | LOOP_SYM { MDBP_R(4723); }
        | LOW_PRIORITY { MDBP_R(4724); }
        | MASTER_CONNECT_RETRY_SYM { MDBP_R(4725); }
        | MASTER_DELAY_SYM { MDBP_R(4726); }
        | MASTER_DEMOTE_TO_SLAVE_SYM { MDBP_R(4727); }
        | MASTER_GTID_POS_SYM { MDBP_R(4728); }
        | MASTER_HEARTBEAT_PERIOD_SYM { MDBP_R(4729); }
        | MASTER_HOST_SYM { MDBP_R(4730); }
        | MASTER_LOG_FILE_SYM { MDBP_R(4731); }
        | MASTER_LOG_POS_SYM { MDBP_R(4732); }
        | MASTER_PASSWORD_SYM { MDBP_R(4733); }
        | MASTER_PORT_SYM { MDBP_R(4734); }
        | MASTER_RETRY_COUNT_SYM { MDBP_R(4735); }
        | MASTER_SERVER_ID_SYM { MDBP_R(4736); }
        | MASTER_SSL_CAPATH_SYM { MDBP_R(4737); }
        | MASTER_SSL_CA_SYM { MDBP_R(4738); }
        | MASTER_SSL_CERT_SYM { MDBP_R(4739); }
        | MASTER_SSL_CIPHER_SYM { MDBP_R(4740); }
        | MASTER_SSL_CRLPATH_SYM { MDBP_R(4741); }
        | MASTER_SSL_CRL_SYM { MDBP_R(4742); }
        | MASTER_SSL_KEY_SYM { MDBP_R(4743); }
        | MASTER_SSL_SYM { MDBP_R(4744); }
        | MASTER_SSL_VERIFY_SERVER_CERT_SYM { MDBP_R(4745); }
        | MASTER_SYM { MDBP_R(4746); }
        | MASTER_USER_SYM { MDBP_R(4747); }
        | MASTER_USE_GTID_SYM { MDBP_R(4748); }
        | MATCH { MDBP_R(4749); }
        | MAXVALUE_SYM { MDBP_R(4750); }
        | MAX_CONNECTIONS_PER_HOUR { MDBP_R(4751); }
        | MAX_QUERIES_PER_HOUR { MDBP_R(4752); }
        | MAX_ROWS { MDBP_R(4753); }
        | MAX_STATEMENT_TIME_SYM { MDBP_R(4754); }
        | MAX_SYM { MDBP_R(4755); }
        | MAX_UPDATES_PER_HOUR { MDBP_R(4756); }
        | MAX_USER_CONNECTIONS_SYM { MDBP_R(4757); }
        | MEDIAN_SYM { MDBP_R(4758); }
        | MEDIUMBLOB { MDBP_R(4759); }
        | MEDIUMINT { MDBP_R(4760); }
        | MEDIUMTEXT { MDBP_R(4761); }
        | MEDIUM_SYM { MDBP_R(4762); }
        | MEMORY_SYM { MDBP_R(4763); }
        | MESSAGE_TEXT_SYM { MDBP_R(4764); }
        | MICROSECOND_SYM { MDBP_R(4765); }
        | MIGRATE_SYM { MDBP_R(4766); }
        | MINUS_ORACLE_SYM { MDBP_R(4767); }
        | MINUTE_MICROSECOND_SYM { MDBP_R(4768); }
        | MINUTE_SECOND_SYM { MDBP_R(4769); }
        | MINUTE_SYM { MDBP_R(4770); }
        | MINVALUE_SYM { MDBP_R(4771); }
        | MIN_ROWS { MDBP_R(4772); }
        | MIN_SYM { MDBP_R(4773); }
        | MODE_SYM { MDBP_R(4774); }
        | MODIFIES_SYM { MDBP_R(4775); }
        | MODIFY_SYM { MDBP_R(4776); }
        | MOD_SYM { MDBP_R(4777); }
        | MONITOR_SYM { MDBP_R(4778); }
        | MONTH_SYM { MDBP_R(4779); }
        | MUTEX_SYM { MDBP_R(4780); }
        | MYSQL_ERRNO_SYM { MDBP_R(4781); }
        | MYSQL_SYM { MDBP_R(4782); }
        | NAMES_SYM { MDBP_R(4783); }
        | NATIONAL_SYM { MDBP_R(4784); }
        | NATURAL { MDBP_R(4785); }
        | NCHAR_SYM { MDBP_R(4786); }
        | NESTED_SYM { MDBP_R(4787); }
        | NEVER_SYM { MDBP_R(4788); }
        | NEXTVAL_SYM { MDBP_R(4789); }
        | NEXT_SYM { MDBP_R(4790); }
        | NOCACHE_SYM { MDBP_R(4791); }
        | NOCOPY_SYM { MDBP_R(4792); }
        | NOCYCLE_SYM { MDBP_R(4793); }
        | NODEGROUP_SYM { MDBP_R(4794); }
        | NOMAXVALUE_SYM { MDBP_R(4795); }
        | NOMINVALUE_SYM { MDBP_R(4796); }
        | NONE_SYM { MDBP_R(4797); }
        | NOTFOUND_SYM { MDBP_R(4798); }
        | NOWAIT_SYM { MDBP_R(4799); }
        | NOW_SYM { MDBP_R(4800); }
        | NO_SYM { MDBP_R(4801); }
        | NO_WRITE_TO_BINLOG { MDBP_R(4802); }
        | NTH_VALUE_SYM { MDBP_R(4803); }
        | NTILE_SYM { MDBP_R(4804); }
        | NUMBER_MARIADB_SYM { MDBP_R(4805); }
        | NUMERIC_SYM { MDBP_R(4806); }
        | NUMTODSINTERVAL_SYM { MDBP_R(4807); }
        | NUMTOYMINTERVAL_SYM { MDBP_R(4808); }
        | NVARCHAR_SYM { MDBP_R(4809); }
        | OFFSET_SYM { MDBP_R(4810); }
        | OF_SYM { MDBP_R(4811); }
        | OLD_PASSWORD_SYM { MDBP_R(4812); }
        | OLD_VALUE_SYM { MDBP_R(4813); }
        | ONE_SYM { MDBP_R(4814); }
        | ONLINE_SYM { MDBP_R(4815); }
        | ONLY_SYM { MDBP_R(4816); }
        | OPEN_SYM { MDBP_R(4817); }
        | OPTIMIZE { MDBP_R(4818); }
        | OPTION { MDBP_R(4819); }
        | OPTIONALLY { MDBP_R(4820); }
        | ORDER_SYM { MDBP_R(4821); }
        | ORDINALITY_SYM { MDBP_R(4822); }
        | OTHERS_MARIADB_SYM { MDBP_R(4823); }
        | OUTER { MDBP_R(4824); }
        | OUTFILE { MDBP_R(4825); }
        | OUT_SYM { MDBP_R(4826); }
        | OVERLAPS_SYM { MDBP_R(4827); }
        | OVER_SYM { MDBP_R(4828); }
        | OWNER_SYM { MDBP_R(4829); }
        | PACKAGE_MARIADB_SYM { MDBP_R(4830); }
        | PACK_KEYS_SYM { MDBP_R(4831); }
        | PAGE_CHECKSUM_SYM { MDBP_R(4832); }
        | PARSER_SYM { MDBP_R(4833); }
        | PARSE_VCOL_EXPR_SYM { MDBP_R(4834); }
        | PARTIAL { MDBP_R(4835); }
        | PARTITIONING_SYM { MDBP_R(4836); }
        | PARTITIONS_SYM { MDBP_R(4837); }
        | PARTITION_SYM { MDBP_R(4838); }
        | PASSWORD_SYM { MDBP_R(4839); }
        | PERCENTILE_CONT_SYM { MDBP_R(4840); }
        | PERCENTILE_DISC_SYM { MDBP_R(4841); }
        | PERCENT_RANK_SYM { MDBP_R(4842); }
        | PERIOD_SYM { MDBP_R(4843); }
        | PERSISTENT_SYM { MDBP_R(4844); }
        | PHASE_SYM { MDBP_R(4845); }
        | PLUGINS_SYM { MDBP_R(4846); }
        | PLUGIN_SYM { MDBP_R(4847); }
        | PORTION_SYM { MDBP_R(4848); }
        | PORT_SYM { MDBP_R(4849); }
        | POSITION_SYM { MDBP_R(4850); }
        | PRECEDES_SYM { MDBP_R(4851); }
        | PRECEDING_SYM { MDBP_R(4852); }
        | PRECISION { MDBP_R(4853); }
        | PREPARE_SYM { MDBP_R(4854); }
        | PRESERVE_SYM { MDBP_R(4855); }
        | PREVIOUS_SYM { MDBP_R(4856); }
        | PREV_SYM { MDBP_R(4857); }
        | PRIMARY_SYM { MDBP_R(4858); }
        | PRIVILEGES { MDBP_R(4859); }
        | PROCESSLIST_SYM { MDBP_R(4860); }
        | PROFILES_SYM { MDBP_R(4861); }
        | PROFILE_SYM { MDBP_R(4862); }
        | PROXY_SYM { MDBP_R(4863); }
        | PURGE { MDBP_R(4864); }
        | QUARTER_SYM { MDBP_R(4865); }
        | QUERY_SYM { MDBP_R(4866); }
        | QUICK { MDBP_R(4867); }
        | RAISE_MARIADB_SYM { MDBP_R(4868); }
        | RANGE_SYM { MDBP_R(4869); }
        | RANK_SYM { MDBP_R(4870); }
        | RAW_MARIADB_SYM { MDBP_R(4871); }
        | READS_SYM { MDBP_R(4872); }
        | READ_ONLY_SYM { MDBP_R(4873); }
        | READ_WRITE_SYM { MDBP_R(4874); }
        | REAL { MDBP_R(4875); }
        | REBUILD_SYM { MDBP_R(4876); }
        | RECORD_SYM { MDBP_R(4877); }
        | RECOVER_SYM { MDBP_R(4878); }
        | RECURSIVE_SYM { MDBP_R(4879); }
        | REDUNDANT_SYM { MDBP_R(4880); }
        | REFERENCES { MDBP_R(4881); }
        | REF_SYM { MDBP_R(4882); }
        | REF_SYSTEM_ID_SYM { MDBP_R(4883); }
        | REGEXP { MDBP_R(4884); }
        | RELAY { MDBP_R(4885); }
        | RELAYLOG_SYM { MDBP_R(4886); }
        | RELAY_LOG_FILE_SYM { MDBP_R(4887); }
        | RELAY_LOG_POS_SYM { MDBP_R(4888); }
        | RELAY_THREAD { MDBP_R(4889); }
        | RELEASE_SYM { MDBP_R(4890); }
        | RELOAD { MDBP_R(4891); }
        | RENAME { MDBP_R(4892); }
        | REORGANIZE_SYM { MDBP_R(4893); }
        | REPAIR { MDBP_R(4894); }
        | REPEATABLE_SYM { MDBP_R(4895); }
        | REPEAT_SYM { MDBP_R(4896); }
        | REPLAY_SYM { MDBP_R(4897); }
        | REPLICATION { MDBP_R(4898); }
        | REQUIRE_SYM { MDBP_R(4899); }
        | RESET_SYM { MDBP_R(4900); }
        | RESIGNAL_SYM { MDBP_R(4901); }
        | RESOURCES { MDBP_R(4902); }
        | RESTART_SYM { MDBP_R(4903); }
        | RESTRICT { MDBP_R(4904); }
        | RESUME_SYM { MDBP_R(4905); }
        | RETURNED_SQLSTATE_SYM { MDBP_R(4906); }
        | RETURNING_SYM { MDBP_R(4907); }
        | RETURNS_SYM { MDBP_R(4908); }
        | RETURN_MARIADB_SYM { MDBP_R(4909); }
        | REUSE_SYM { MDBP_R(4910); }
        | REVERSE_SYM { MDBP_R(4911); }
        | RIGHT { MDBP_R(4912); }
        | ROLLBACK_SYM { MDBP_R(4913); }
        | ROLLUP_SYM { MDBP_R(4914); }
        | ROUTINE_SYM { MDBP_R(4915); }
        | ROWCOUNT_SYM { MDBP_R(4916); }
        | ROWNUM_SYM { MDBP_R(4917); }
        | ROWS_SYM { MDBP_R(4918); }
        | ROWTYPE_MARIADB_SYM { MDBP_R(4919); }
        | ROW_COUNT_SYM { MDBP_R(4920); }
        | ROW_FORMAT_SYM { MDBP_R(4921); }
        | ROW_NUMBER_SYM { MDBP_R(4922); }
        | ROW_SYM { MDBP_R(4923); }
        | RTREE_SYM { MDBP_R(4924); }
        | SAVEPOINT_SYM { MDBP_R(4925); }
        | SCALAR_SYM { MDBP_R(4926); }
        | SCHEDULE_SYM { MDBP_R(4927); }
        | SCHEMA_NAME_SYM { MDBP_R(4928); }
        | SECOND_MICROSECOND_SYM { MDBP_R(4929); }
        | SECOND_SYM { MDBP_R(4930); }
        | SECURITY_SYM { MDBP_R(4931); }
        | SELECT_SYM { MDBP_R(4932); }
        | SENSITIVE_SYM { MDBP_R(4933); }
        | SEPARATOR_SYM { MDBP_R(4934); }
        | SEQUENCE_SYM { MDBP_R(4935); }
        | SERIALIZABLE_SYM { MDBP_R(4936); }
        | SERIAL_SYM { MDBP_R(4937); }
        | SERVER_SYM { MDBP_R(4938); }
        | SESSION_SYM { MDBP_R(4939); }
        | SESSION_USER_SYM { MDBP_R(4940); }
        | SETVAL_SYM { MDBP_R(4941); }
        | SHARE_SYM { MDBP_R(4942); }
        | SHUTDOWN { MDBP_R(4943); }
        | SIGNAL_SYM { MDBP_R(4944); }
        | SIGNED_SYM { MDBP_R(4945); }
        | SIMPLE_SYM { MDBP_R(4946); }
        | SKIP_SYM { MDBP_R(4947); }
        | SLAVE { MDBP_R(4948); }
        | SLAVES { MDBP_R(4949); }
        | SLAVE_POS_SYM { MDBP_R(4950); }
        | SLOW { MDBP_R(4951); }
        | SMALLINT { MDBP_R(4952); }
        | SNAPSHOT_SYM { MDBP_R(4953); }
        | SOCKET_SYM { MDBP_R(4954); }
        | SOFT_SYM { MDBP_R(4955); }
        | SONAME_SYM { MDBP_R(4956); }
        | SOUNDS_SYM { MDBP_R(4957); }
        | SOURCE_SYM { MDBP_R(4958); }
        | SPATIAL_SYM { MDBP_R(4959); }
        | SPECIFIC_SYM { MDBP_R(4960); }
        | SQLEXCEPTION_SYM { MDBP_R(4961); }
        | SQLSTATE_SYM { MDBP_R(4962); }
        | SQLWARNING_SYM { MDBP_R(4963); }
        | SQL_AFTER_GTIDS_SYM { MDBP_R(4964); }
        | SQL_BEFORE_GTIDS_SYM { MDBP_R(4965); }
        | SQL_BIG_RESULT { MDBP_R(4966); }
        | SQL_BUFFER_RESULT { MDBP_R(4967); }
        | SQL_CACHE_SYM { MDBP_R(4968); }
        | SQL_CALC_FOUND_ROWS { MDBP_R(4969); }
        | SQL_NO_CACHE_SYM { MDBP_R(4970); }
        | SQL_SMALL_RESULT { MDBP_R(4971); }
        | SQL_SYM { MDBP_R(4972); }
        | SQL_THREAD { MDBP_R(4973); }
        | SSL_SYM { MDBP_R(4974); }
        | STAGE_SYM { MDBP_R(4975); }
        | STARTING { MDBP_R(4976); }
        | STARTS_SYM { MDBP_R(4977); }
        | START_SYM { MDBP_R(4978); }
        | STATEMENT_SYM { MDBP_R(4979); }
        | STATS_AUTO_RECALC_SYM { MDBP_R(4980); }
        | STATS_PERSISTENT_SYM { MDBP_R(4981); }
        | STATS_SAMPLE_PAGES_SYM { MDBP_R(4982); }
        | STDDEV_SAMP_SYM { MDBP_R(4983); }
        | STD_SYM { MDBP_R(4984); }
        | STOP_SYM { MDBP_R(4985); }
        | STORAGE_SYM { MDBP_R(4986); }
        | STORED_SYM { MDBP_R(4987); }
        | STRAIGHT_JOIN { MDBP_R(4988); }
        | STRING_SYM { MDBP_R(4989); }
        | ST_COLLECT_SYM { MDBP_R(4990); }
        | SUBCLASS_ORIGIN_SYM { MDBP_R(4991); }
        | SUBDATE_SYM { MDBP_R(4992); }
        | SUBJECT_SYM { MDBP_R(4993); }
        | SUBPARTITIONS_SYM { MDBP_R(4994); }
        | SUBPARTITION_SYM { MDBP_R(4995); }
        | SUBSTRING { MDBP_R(4996); }
        | SUM_SYM { MDBP_R(4997); }
        | SUPER_SYM { MDBP_R(4998); }
        | SUSPEND_SYM { MDBP_R(4999); }
        | SWAPS_SYM { MDBP_R(5000); }
        | SWITCHES_SYM { MDBP_R(5001); }
        | SYSDATE { MDBP_R(5002); }
        | SYSTEM { MDBP_R(5003); }
        | SYSTEM_TIME_SYM { MDBP_R(5004); }
        | TABLES { MDBP_R(5005); }
        | TABLESPACE { MDBP_R(5006); }
        | TABLE_CHECKSUM_SYM { MDBP_R(5007); }
        | TABLE_NAME_SYM { MDBP_R(5008); }
        | TEMPORARY { MDBP_R(5009); }
        | TEMPTABLE_SYM { MDBP_R(5010); }
        | TERMINATED { MDBP_R(5011); }
        | TEXT_SYM { MDBP_R(5012); }
        | THAN_SYM { MDBP_R(5013); }
        | THEN_SYM { MDBP_R(5014); }
        | THREADS_SYM { MDBP_R(5015); }
        | TIES_SYM { MDBP_R(5016); }
        | TIMESTAMP { MDBP_R(5017); }
        | TIMESTAMP_ADD { MDBP_R(5018); }
        | TIMESTAMP_DIFF { MDBP_R(5019); }
        | TIME_SYM { MDBP_R(5020); }
        | TINYBLOB { MDBP_R(5021); }
        | TINYINT { MDBP_R(5022); }
        | TINYTEXT { MDBP_R(5023); }
        | TO_DATE { MDBP_R(5024); }
        | TRAILING { MDBP_R(5025); }
        | TRANSACTIONAL_SYM { MDBP_R(5026); }
        | TRANSACTION_SYM { MDBP_R(5027); }
        | TRIGGERS_SYM { MDBP_R(5028); }
        | TRIGGER_SYM { MDBP_R(5029); }
        | TRIM { MDBP_R(5030); }
        | TRIM_ORACLE { MDBP_R(5031); }
        | TRUNCATE_SYM { MDBP_R(5032); }
        | UNBOUNDED_SYM { MDBP_R(5033); }
        | UNCOMMITTED_SYM { MDBP_R(5034); }
        | UNDEFINED_SYM { MDBP_R(5035); }
        | UNDO_SYM { MDBP_R(5036); }
        | UNICODE_SYM { MDBP_R(5037); }
        | UNINSTALL_SYM { MDBP_R(5038); }
        | UNION_SYM { MDBP_R(5039); }
        | UNIQUE_SYM { MDBP_R(5040); }
        | UNKNOWN_SYM { MDBP_R(5041); }
        | UNSIGNED { MDBP_R(5042); }
        | UNTIL_SYM { MDBP_R(5043); }
        | UPGRADE_SYM { MDBP_R(5044); }
        | USAGE { MDBP_R(5045); }
        | USE_FRM { MDBP_R(5046); }
        | USING { MDBP_R(5047); }
        | UTC_DATE_SYM { MDBP_R(5048); }
        | UTC_TIMESTAMP_SYM { MDBP_R(5049); }
        | UTC_TIME_SYM { MDBP_R(5050); }
        | VALUES { MDBP_R(5051); }
        | VALUE_SYM { MDBP_R(5052); }
        | VARBINARY { MDBP_R(5053); }
        | VARCHAR { MDBP_R(5054); }
        | VARCHAR2_MARIADB_SYM { MDBP_R(5055); }
        | VARIABLES { MDBP_R(5056); }
        | VARIANCE_SYM { MDBP_R(5057); }
        | VARYING { MDBP_R(5058); }
        | VAR_SAMP_SYM { MDBP_R(5059); }
        | VECTOR_SYM { MDBP_R(5060); }
        | VENDORS_SYM { MDBP_R(5061); }
        | VERSIONING_SYM { MDBP_R(5062); }
        | VIA_SYM { MDBP_R(5063); }
        | VIRTUAL_SYM { MDBP_R(5064); }
        | VISIBLE_SYM { MDBP_R(5065); }
        | WAIT_SYM { MDBP_R(5066); }
        | WARNINGS { MDBP_R(5067); }
        | WEEK_SYM { MDBP_R(5068); }
        | WEIGHT_STRING_SYM { MDBP_R(5069); }
        | WHEN_SYM { MDBP_R(5070); }
        | WHERE { MDBP_R(5071); }
        | WHILE_SYM { MDBP_R(5072); }
        | WINDOW_SYM { MDBP_R(5073); }
        | WITH { MDBP_R(5074); }
        | WITHIN { MDBP_R(5075); }
        | WITHOUT { MDBP_R(5076); }
        | WORK_SYM { MDBP_R(5077); }
        | WRAPPER_SYM { MDBP_R(5078); }
        | WRITE_SYM { MDBP_R(5079); }
        | X509_SYM { MDBP_R(5080); }
        | XA_SYM { MDBP_R(5081); }
        | XML_SYM { MDBP_R(5082); }
        | XOR { MDBP_R(5083); }
        | YEAR_MONTH_SYM { MDBP_R(5084); }
        | YEAR_SYM { MDBP_R(5085); }
        | ZEROFILL { MDBP_R(5086); }
        ;

mrs_qualified_identifier:
          mrs_identifier { MDBP_R(5087); }
        | mrs_identifier '.' mrs_identifier { MDBP_R(5088); }
        ;

mrs_text_or_identifier:
          mrs_identifier { MDBP_R(5089); }
        | TEXT_STRING { MDBP_R(5090); }
        | LEX_HOSTNAME { MDBP_R(5091); }
        ;

mrs_int_number:
          NUM { MDBP_R(5092); }
        | LONG_NUM { MDBP_R(5093); }
        | ULONGLONG_NUM { MDBP_R(5094); }
        ;

mrs_request_path:
          '/' mrs_path_segment { MDBP_R(5095); }
        | mrs_request_path '/' mrs_path_segment { MDBP_R(5096); }
        ;

mrs_path_segment:
          IDENT_sys { MDBP_R(5097); }
        | ACCOUNT_SYM { MDBP_R(5098); }
        | ACTION { MDBP_R(5099); }
        | ADD { MDBP_R(5100); }
        | ADDDATE_SYM { MDBP_R(5101); }
        | ADMIN_SYM { MDBP_R(5102); }
        | AFTER_SYM { MDBP_R(5103); }
        | AGAINST { MDBP_R(5104); }
        | AGGREGATE_SYM { MDBP_R(5105); }
        | ALGORITHM_SYM { MDBP_R(5106); }
        | ALL { MDBP_R(5107); }
        | ALLOW_SYM { MDBP_R(5108); }
        | ALTER { MDBP_R(5109); }
        | ALWAYS_SYM { MDBP_R(5110); }
        | ANALYZE_SYM { MDBP_R(5111); }
        | AND_SYM { MDBP_R(5112); }
        | ANY_SYM { MDBP_R(5113); }
        | APPS_SYM { MDBP_R(5114); }
        | APP_SYM { MDBP_R(5115); }
        | ARRAY_SYM { MDBP_R(5116); }
        | AS { MDBP_R(5117); }
        | ASC { MDBP_R(5118); }
        | ASCII_SYM { MDBP_R(5119); }
        | ASENSITIVE_SYM { MDBP_R(5120); }
        | ATOMIC_SYM { MDBP_R(5121); }
        | AT_SYM { MDBP_R(5122); }
        | AUTHENTICATION_SYM { MDBP_R(5123); }
        | AUTHORIZATION_SYM { MDBP_R(5124); }
        | AUTHORS_SYM { MDBP_R(5125); }
        | AUTH_SYM { MDBP_R(5126); }
        | AUTODETECT_SYM { MDBP_R(5127); }
        | AUTO_INC { MDBP_R(5128); }
        | AUTO_SYM { MDBP_R(5129); }
        | AVAILABLE_SYM { MDBP_R(5130); }
        | AVG_ROW_LENGTH { MDBP_R(5131); }
        | AVG_SYM { MDBP_R(5132); }
        | BACKUP_SYM { MDBP_R(5133); }
        | BEFORE_SYM { MDBP_R(5134); }
        | BEGIN_MARIADB_SYM { MDBP_R(5135); }
        | BETWEEN_SYM { MDBP_R(5136); }
        | BIGINT { MDBP_R(5137); }
        | BINARY { MDBP_R(5138); }
        | BINLOG_SYM { MDBP_R(5139); }
        | BIT_AND { MDBP_R(5140); }
        | BIT_OR { MDBP_R(5141); }
        | BIT_SYM { MDBP_R(5142); }
        | BIT_XOR { MDBP_R(5143); }
        | BLOB_MARIADB_SYM { MDBP_R(5144); }
        | BLOCK_SYM { MDBP_R(5145); }
        | BODY_MARIADB_SYM { MDBP_R(5146); }
        | BOOLEAN_SYM { MDBP_R(5147); }
        | BOOL_SYM { MDBP_R(5148); }
        | BOTH { MDBP_R(5149); }
        | BTREE_SYM { MDBP_R(5150); }
        | BY { MDBP_R(5151); }
        | BYTE_SYM { MDBP_R(5152); }
        | CACHE_SYM { MDBP_R(5153); }
        | CALL_SYM { MDBP_R(5154); }
        | CASCADE { MDBP_R(5155); }
        | CASCADED { MDBP_R(5156); }
        | CASE_SYM { MDBP_R(5157); }
        | CAST_SYM { MDBP_R(5158); }
        | CATALOG_NAME_SYM { MDBP_R(5159); }
        | CHAIN_SYM { MDBP_R(5160); }
        | CHANGE { MDBP_R(5161); }
        | CHANGED { MDBP_R(5162); }
        | CHANNEL_SYM { MDBP_R(5163); }
        | CHARSET { MDBP_R(5164); }
        | CHAR_SYM { MDBP_R(5165); }
        | CHECKPOINT_SYM { MDBP_R(5166); }
        | CHECKSUM_SYM { MDBP_R(5167); }
        | CHECK_SYM { MDBP_R(5168); }
        | CIPHER_SYM { MDBP_R(5169); }
        | CLASS_ORIGIN_SYM { MDBP_R(5170); }
        | CLASS_SYM { MDBP_R(5171); }
        | CLIENT_SYM { MDBP_R(5172); }
        | CLOB_MARIADB_SYM { MDBP_R(5173); }
        | CLONE_SYM { MDBP_R(5174); }
        | CLOSE_SYM { MDBP_R(5175); }
        | COALESCE { MDBP_R(5176); }
        | CODE_SYM { MDBP_R(5177); }
        | COLLATE_SYM { MDBP_R(5178); }
        | COLLATION_SYM { MDBP_R(5179); }
        | COLUMNS { MDBP_R(5180); }
        | COLUMN_ADD_SYM { MDBP_R(5181); }
        | COLUMN_CHECK_SYM { MDBP_R(5182); }
        | COLUMN_CREATE_SYM { MDBP_R(5183); }
        | COLUMN_DELETE_SYM { MDBP_R(5184); }
        | COLUMN_GET_SYM { MDBP_R(5185); }
        | COLUMN_NAME_SYM { MDBP_R(5186); }
        | COLUMN_SYM { MDBP_R(5187); }
        | COMMENT_SYM { MDBP_R(5188); }
        | COMMITTED_SYM { MDBP_R(5189); }
        | COMMIT_SYM { MDBP_R(5190); }
        | COMPACT_SYM { MDBP_R(5191); }
        | COMPLETION_SYM { MDBP_R(5192); }
        | COMPRESSED_SYM { MDBP_R(5193); }
        | CONCURRENT { MDBP_R(5194); }
        | CONDITION_SYM { MDBP_R(5195); }
        | CONFIGURE_SYM { MDBP_R(5196); }
        | CONNECTION_SYM { MDBP_R(5197); }
        | CONSISTENT_SYM { MDBP_R(5198); }
        | CONSTRAINT { MDBP_R(5199); }
        | CONSTRAINT_CATALOG_SYM { MDBP_R(5200); }
        | CONSTRAINT_NAME_SYM { MDBP_R(5201); }
        | CONSTRAINT_SCHEMA_SYM { MDBP_R(5202); }
        | CONTAINS_SYM { MDBP_R(5203); }
        | CONTENT_SYM { MDBP_R(5204); }
        | CONTEXT_SYM { MDBP_R(5205); }
        | CONTINUE_MARIADB_SYM { MDBP_R(5206); }
        | CONTRIBUTORS_SYM { MDBP_R(5207); }
        | CONVERSION_SYM { MDBP_R(5208); }
        | CONVERT_SYM { MDBP_R(5209); }
        | COUNT_SYM { MDBP_R(5210); }
        | CPU_SYM { MDBP_R(5211); }
        | CREATE { MDBP_R(5212); }
        | CROSS { MDBP_R(5213); }
        | CUBE_SYM { MDBP_R(5214); }
        | CUME_DIST_SYM { MDBP_R(5215); }
        | CURDATE { MDBP_R(5216); }
        | CURRENT_PATH { MDBP_R(5217); }
        | CURRENT_POS_SYM { MDBP_R(5218); }
        | CURRENT_ROLE { MDBP_R(5219); }
        | CURRENT_SYM { MDBP_R(5220); }
        | CURRENT_USER { MDBP_R(5221); }
        | CURSOR_NAME_SYM { MDBP_R(5222); }
        | CURSOR_SYM { MDBP_R(5223); }
        | CURTIME { MDBP_R(5224); }
        | CYCLE_SYM { MDBP_R(5225); }
        | DAEMONS_SYM { MDBP_R(5226); }
        | DAEMON_SYM { MDBP_R(5227); }
        | DATABASE { MDBP_R(5228); }
        | DATABASES { MDBP_R(5229); }
        | DATA_SYM { MDBP_R(5230); }
        | DATETIME { MDBP_R(5231); }
        | DATE_ADD_INTERVAL { MDBP_R(5232); }
        | DATE_SUB_INTERVAL { MDBP_R(5233); }
        | DATE_SYM { MDBP_R(5234); }
        | DAY_HOUR_SYM { MDBP_R(5235); }
        | DAY_MICROSECOND_SYM { MDBP_R(5236); }
        | DAY_MINUTE_SYM { MDBP_R(5237); }
        | DAY_SECOND_SYM { MDBP_R(5238); }
        | DAY_SYM { MDBP_R(5239); }
        | DEALLOCATE_SYM { MDBP_R(5240); }
        | DECIMAL_SYM { MDBP_R(5241); }
        | DECLARE_MARIADB_SYM { MDBP_R(5242); }
        | DEFAULT { MDBP_R(5243); }
        | DEFINER_SYM { MDBP_R(5244); }
        | DELAYED_SYM { MDBP_R(5245); }
        | DELAY_KEY_WRITE_SYM { MDBP_R(5246); }
        | DELETE_DOMAIN_ID_SYM { MDBP_R(5247); }
        | DELETE_SYM { MDBP_R(5248); }
        | DENSE_RANK_SYM { MDBP_R(5249); }
        | DENY { MDBP_R(5250); }
        | DESC { MDBP_R(5251); }
        | DESCRIBE { MDBP_R(5252); }
        | DETERMINISTIC_SYM { MDBP_R(5253); }
        | DEVELOPMENT_SYM { MDBP_R(5254); }
        | DIAGNOSTICS_SYM { MDBP_R(5255); }
        | DIRECTORY_SYM { MDBP_R(5256); }
        | DISABLED_SYM { MDBP_R(5257); }
        | DISABLE_SYM { MDBP_R(5258); }
        | DISCARD { MDBP_R(5259); }
        | DISK_SYM { MDBP_R(5260); }
        | DISTINCT { MDBP_R(5261); }
        | DIV_SYM { MDBP_R(5262); }
        | DOUBLE_SYM { MDBP_R(5263); }
        | DO_DOMAIN_IDS_SYM { MDBP_R(5264); }
        | DO_SYM { MDBP_R(5265); }
        | DROP { MDBP_R(5266); }
        | DUAL_SYM { MDBP_R(5267); }
        | DUMPFILE { MDBP_R(5268); }
        | DUPLICATE_SYM { MDBP_R(5269); }
        | DYNAMIC_SYM { MDBP_R(5270); }
        | EACH_SYM { MDBP_R(5271); }
        | ELSE { MDBP_R(5272); }
        | ELSEIF_MARIADB_SYM { MDBP_R(5273); }
        | ELSIF_MARIADB_SYM { MDBP_R(5274); }
        | EMPTY_SYM { MDBP_R(5275); }
        | ENABLED_SYM { MDBP_R(5276); }
        | ENABLE_SYM { MDBP_R(5277); }
        | ENCLOSED { MDBP_R(5278); }
        | END { MDBP_R(5279); }
        | ENDPOINTS_SYM { MDBP_R(5280); }
        | ENDS_SYM { MDBP_R(5281); }
        | ENGINES_SYM { MDBP_R(5282); }
        | ENGINE_SYM { MDBP_R(5283); }
        | ENUM { MDBP_R(5284); }
        | ERRORS { MDBP_R(5285); }
        | ERROR_SYM { MDBP_R(5286); }
        | ESCAPED { MDBP_R(5287); }
        | ESCAPE_SYM { MDBP_R(5288); }
        | EVENTS_SYM { MDBP_R(5289); }
        | EVENT_SYM { MDBP_R(5290); }
        | EVERY_SYM { MDBP_R(5291); }
        | EXAMINED_SYM { MDBP_R(5292); }
        | EXCEPTION_MARIADB_SYM { MDBP_R(5293); }
        | EXCEPT_SYM { MDBP_R(5294); }
        | EXCHANGE_SYM { MDBP_R(5295); }
        | EXCLUDE_SYM { MDBP_R(5296); }
        | EXECUTE_SYM { MDBP_R(5297); }
        | EXISTS { MDBP_R(5298); }
        | EXIT_MARIADB_SYM { MDBP_R(5299); }
        | EXPANSION_SYM { MDBP_R(5300); }
        | EXPIRE_SYM { MDBP_R(5301); }
        | EXPORT_SYM { MDBP_R(5302); }
        | EXTENDED_SYM { MDBP_R(5303); }
        | EXTENDS_SYM { MDBP_R(5304); }
        | EXTENT_SIZE_SYM { MDBP_R(5305); }
        | EXTRACT_SYM { MDBP_R(5306); }
        | FALSE_SYM { MDBP_R(5307); }
        | FAST_SYM { MDBP_R(5308); }
        | FAULTS_SYM { MDBP_R(5309); }
        | FEDERATED_SYM { MDBP_R(5310); }
        | FEED_SYM { MDBP_R(5311); }
        | FETCH_SYM { MDBP_R(5312); }
        | FILES_SYM { MDBP_R(5313); }
        | FILE_SYM { MDBP_R(5314); }
        | FILTER_SYM { MDBP_R(5315); }
        | FIRST_SYM { MDBP_R(5316); }
        | FIRST_VALUE_SYM { MDBP_R(5317); }
        | FIXED_SYM { MDBP_R(5318); }
        | FLOAT_SYM { MDBP_R(5319); }
        | FLUSH_SYM { MDBP_R(5320); }
        | FOLLOWING_SYM { MDBP_R(5321); }
        | FOLLOWS_SYM { MDBP_R(5322); }
        | FORCE_SYM { MDBP_R(5323); }
        | FOREIGN { MDBP_R(5324); }
        | FORMAT_SYM { MDBP_R(5325); }
        | FOR_SYM { MDBP_R(5326); }
        | FOUND_SYM { MDBP_R(5327); }
        | FROM { MDBP_R(5328); }
        | FULL { MDBP_R(5329); }
        | FULLTEXT_SYM { MDBP_R(5330); }
        | FUNCTIONS_SYM { MDBP_R(5331); }
        | FUNCTION_SYM { MDBP_R(5332); }
        | GENERAL { MDBP_R(5333); }
        | GENERATED_SYM { MDBP_R(5334); }
        | GET_FORMAT { MDBP_R(5335); }
        | GET_SYM { MDBP_R(5336); }
        | GLOBAL_SYM { MDBP_R(5337); }
        | GOTO_MARIADB_SYM { MDBP_R(5338); }
        | GRANT { MDBP_R(5339); }
        | GRANTS { MDBP_R(5340); }
        | GROUP_CONCAT_SYM { MDBP_R(5341); }
        | GROUP_SYM { MDBP_R(5342); }
        | HANDLER_SYM { MDBP_R(5343); }
        | HARD_SYM { MDBP_R(5344); }
        | HASH_SYM { MDBP_R(5345); }
        | HAVING { MDBP_R(5346); }
        | HELP_SYM { MDBP_R(5347); }
        | HIERARCHY_SYM { MDBP_R(5348); }
        | HIGH_PRIORITY { MDBP_R(5349); }
        | HISTORY_SYM { MDBP_R(5350); }
        | HOSTS_SYM { MDBP_R(5351); }
        | HOST_SYM { MDBP_R(5352); }
        | HOUR_MICROSECOND_SYM { MDBP_R(5353); }
        | HOUR_MINUTE_SYM { MDBP_R(5354); }
        | HOUR_SECOND_SYM { MDBP_R(5355); }
        | HOUR_SYM { MDBP_R(5356); }
        | HTTPS_SYM { MDBP_R(5357); }
        | HTTP_SYM { MDBP_R(5358); }
        | IDENTIFIED_SYM { MDBP_R(5359); }
        | ID_SYM { MDBP_R(5360); }
        | IF_SYM { MDBP_R(5361); }
        | IGNORED_SYM { MDBP_R(5362); }
        | IGNORE_DOMAIN_IDS_SYM { MDBP_R(5363); }
        | IGNORE_SERVER_IDS_SYM { MDBP_R(5364); }
        | IGNORE_SYM { MDBP_R(5365); }
        | IMMEDIATE_SYM { MDBP_R(5366); }
        | IMPORT { MDBP_R(5367); }
        | INCLUDING_SYM { MDBP_R(5368); }
        | INCREMENT_SYM { MDBP_R(5369); }
        | INDEXES { MDBP_R(5370); }
        | INDEX_SYM { MDBP_R(5371); }
        | INFILE { MDBP_R(5372); }
        | INNER_SYM { MDBP_R(5373); }
        | INOUT_SYM { MDBP_R(5374); }
        | INSENSITIVE_SYM { MDBP_R(5375); }
        | INSERT { MDBP_R(5376); }
        | INSERT_METHOD { MDBP_R(5377); }
        | INSTALL_SYM { MDBP_R(5378); }
        | INTERSECT_SYM { MDBP_R(5379); }
        | INTERVAL_SYM { MDBP_R(5380); }
        | INTO { MDBP_R(5381); }
        | INT_SYM { MDBP_R(5382); }
        | INVISIBLE_SYM { MDBP_R(5383); }
        | INVOKER_SYM { MDBP_R(5384); }
        | IN_SYM { MDBP_R(5385); }
        | IO_SYM { MDBP_R(5386); }
        | IPC_SYM { MDBP_R(5387); }
        | IS { MDBP_R(5388); }
        | ISOLATION { MDBP_R(5389); }
        | ISOPEN_SYM { MDBP_R(5390); }
        | ISSUER_SYM { MDBP_R(5391); }
        | ITEMS_SYM { MDBP_R(5392); }
        | ITEM_SYM { MDBP_R(5393); }
        | ITERATE_SYM { MDBP_R(5394); }
        | JOIN_SYM { MDBP_R(5395); }
        | JSON_ARRAYAGG_SYM { MDBP_R(5396); }
        | JSON_OBJECTAGG_SYM { MDBP_R(5397); }
        | JSON_SYM { MDBP_R(5398); }
        | JSON_TABLE_SYM { MDBP_R(5399); }
        | KEYS { MDBP_R(5400); }
        | KEY_BLOCK_SIZE { MDBP_R(5401); }
        | KEY_SYM { MDBP_R(5402); }
        | KILL_SYM { MDBP_R(5403); }
        | LAG_SYM { MDBP_R(5404); }
        | LANGUAGE_SYM { MDBP_R(5405); }
        | LASTVAL_SYM { MDBP_R(5406); }
        | LAST_SYM { MDBP_R(5407); }
        | LAST_VALUE { MDBP_R(5408); }
        | LEADING { MDBP_R(5409); }
        | LEAD_SYM { MDBP_R(5410); }
        | LEAVES { MDBP_R(5411); }
        | LEAVE_SYM { MDBP_R(5412); }
        | LEFT { MDBP_R(5413); }
        | LESS_SYM { MDBP_R(5414); }
        | LEVEL_SYM { MDBP_R(5415); }
        | LIKE { MDBP_R(5416); }
        | LIMIT { MDBP_R(5417); }
        | LINEAR_SYM { MDBP_R(5418); }
        | LINES { MDBP_R(5419); }
        | LIST_SYM { MDBP_R(5420); }
        | LOAD { MDBP_R(5421); }
        | LOCALTIMESTAMP { MDBP_R(5422); }
        | LOCAL_SYM { MDBP_R(5423); }
        | LOCKED_SYM { MDBP_R(5424); }
        | LOCKS_SYM { MDBP_R(5425); }
        | LOCK_SYM { MDBP_R(5426); }
        | LOGS_SYM { MDBP_R(5427); }
        | LOG_SYM { MDBP_R(5428); }
        | LONGBLOB { MDBP_R(5429); }
        | LONGTEXT { MDBP_R(5430); }
        | LONG_SYM { MDBP_R(5431); }
        | LOOP_SYM { MDBP_R(5432); }
        | LOW_PRIORITY { MDBP_R(5433); }
        | MANAGEMENT_SYM { MDBP_R(5434); }
        | MAPPING_SYM { MDBP_R(5435); }
        | MARIADB_SYM { MDBP_R(5436); }
        | MASTER_CONNECT_RETRY_SYM { MDBP_R(5437); }
        | MASTER_DELAY_SYM { MDBP_R(5438); }
        | MASTER_DEMOTE_TO_SLAVE_SYM { MDBP_R(5439); }
        | MASTER_GTID_POS_SYM { MDBP_R(5440); }
        | MASTER_HEARTBEAT_PERIOD_SYM { MDBP_R(5441); }
        | MASTER_HOST_SYM { MDBP_R(5442); }
        | MASTER_LOG_FILE_SYM { MDBP_R(5443); }
        | MASTER_LOG_POS_SYM { MDBP_R(5444); }
        | MASTER_PASSWORD_SYM { MDBP_R(5445); }
        | MASTER_PORT_SYM { MDBP_R(5446); }
        | MASTER_RETRY_COUNT_SYM { MDBP_R(5447); }
        | MASTER_SERVER_ID_SYM { MDBP_R(5448); }
        | MASTER_SSL_CAPATH_SYM { MDBP_R(5449); }
        | MASTER_SSL_CA_SYM { MDBP_R(5450); }
        | MASTER_SSL_CERT_SYM { MDBP_R(5451); }
        | MASTER_SSL_CIPHER_SYM { MDBP_R(5452); }
        | MASTER_SSL_CRLPATH_SYM { MDBP_R(5453); }
        | MASTER_SSL_CRL_SYM { MDBP_R(5454); }
        | MASTER_SSL_KEY_SYM { MDBP_R(5455); }
        | MASTER_SSL_SYM { MDBP_R(5456); }
        | MASTER_SSL_VERIFY_SERVER_CERT_SYM { MDBP_R(5457); }
        | MASTER_SYM { MDBP_R(5458); }
        | MASTER_USER_SYM { MDBP_R(5459); }
        | MASTER_USE_GTID_SYM { MDBP_R(5460); }
        | MATCH { MDBP_R(5461); }
        | MAXVALUE_SYM { MDBP_R(5462); }
        | MAX_CONNECTIONS_PER_HOUR { MDBP_R(5463); }
        | MAX_QUERIES_PER_HOUR { MDBP_R(5464); }
        | MAX_ROWS { MDBP_R(5465); }
        | MAX_STATEMENT_TIME_SYM { MDBP_R(5466); }
        | MAX_SYM { MDBP_R(5467); }
        | MAX_UPDATES_PER_HOUR { MDBP_R(5468); }
        | MAX_USER_CONNECTIONS_SYM { MDBP_R(5469); }
        | MEDIAN_SYM { MDBP_R(5470); }
        | MEDIA_SYM { MDBP_R(5471); }
        | MEDIUMBLOB { MDBP_R(5472); }
        | MEDIUMINT { MDBP_R(5473); }
        | MEDIUMTEXT { MDBP_R(5474); }
        | MEDIUM_SYM { MDBP_R(5475); }
        | MEMORY_SYM { MDBP_R(5476); }
        | MERGE_SYM { MDBP_R(5477); }
        | MESSAGE_TEXT_SYM { MDBP_R(5478); }
        | METADATA_SYM { MDBP_R(5479); }
        | MICROSECOND_SYM { MDBP_R(5480); }
        | MIGRATE_SYM { MDBP_R(5481); }
        | MINUS_ORACLE_SYM { MDBP_R(5482); }
        | MINUTE_MICROSECOND_SYM { MDBP_R(5483); }
        | MINUTE_SECOND_SYM { MDBP_R(5484); }
        | MINUTE_SYM { MDBP_R(5485); }
        | MINVALUE_SYM { MDBP_R(5486); }
        | MIN_ROWS { MDBP_R(5487); }
        | MIN_SYM { MDBP_R(5488); }
        | MODE_SYM { MDBP_R(5489); }
        | MODIFIES_SYM { MDBP_R(5490); }
        | MODIFY_SYM { MDBP_R(5491); }
        | MOD_SYM { MDBP_R(5492); }
        | MONITOR_SYM { MDBP_R(5493); }
        | MONTH_SYM { MDBP_R(5494); }
        | MRS_SYM { MDBP_R(5495); }
        | MUTEX_SYM { MDBP_R(5496); }
        | MYSQL_ERRNO_SYM { MDBP_R(5497); }
        | MYSQL_SYM { MDBP_R(5498); }
        | NAMES_SYM { MDBP_R(5499); }
        | NAME_SYM { MDBP_R(5500); }
        | NATIONAL_SYM { MDBP_R(5501); }
        | NATURAL { MDBP_R(5502); }
        | NCHAR_SYM { MDBP_R(5503); }
        | NESTED_SYM { MDBP_R(5504); }
        | NEVER_SYM { MDBP_R(5505); }
        | NEW_SYM { MDBP_R(5506); }
        | NEXTVAL_SYM { MDBP_R(5507); }
        | NEXT_SYM { MDBP_R(5508); }
        | NOCACHE_SYM { MDBP_R(5509); }
        | NOCOPY_SYM { MDBP_R(5510); }
        | NOCYCLE_SYM { MDBP_R(5511); }
        | NODEGROUP_SYM { MDBP_R(5512); }
        | NOMAXVALUE_SYM { MDBP_R(5513); }
        | NOMINVALUE_SYM { MDBP_R(5514); }
        | NONE_SYM { MDBP_R(5515); }
        | NOTFOUND_SYM { MDBP_R(5516); }
        | NOT_SYM { MDBP_R(5517); }
        | NOWAIT_SYM { MDBP_R(5518); }
        | NOW_SYM { MDBP_R(5519); }
        | NO_SYM { MDBP_R(5520); }
        | NO_WRITE_TO_BINLOG { MDBP_R(5521); }
        | NTH_VALUE_SYM { MDBP_R(5522); }
        | NTILE_SYM { MDBP_R(5523); }
        | NULL_SYM { MDBP_R(5524); }
        | NUMBER_MARIADB_SYM { MDBP_R(5525); }
        | NUMERIC_SYM { MDBP_R(5526); }
        | NUMTODSINTERVAL_SYM { MDBP_R(5527); }
        | NUMTOYMINTERVAL_SYM { MDBP_R(5528); }
        | NVARCHAR_SYM { MDBP_R(5529); }
        | OBJECT_SYM { MDBP_R(5530); }
        | OFFSET_SYM { MDBP_R(5531); }
        | OF_SYM { MDBP_R(5532); }
        | OLD_PASSWORD_SYM { MDBP_R(5533); }
        | OLD_VALUE_SYM { MDBP_R(5534); }
        | ON { MDBP_R(5535); }
        | ONE_SYM { MDBP_R(5536); }
        | ONLINE_SYM { MDBP_R(5537); }
        | ONLY_SYM { MDBP_R(5538); }
        | OPEN_SYM { MDBP_R(5539); }
        | OPTIMIZE { MDBP_R(5540); }
        | OPTION { MDBP_R(5541); }
        | OPTIONALLY { MDBP_R(5542); }
        | OPTIONS_SYM { MDBP_R(5543); }
        | ORDER_SYM { MDBP_R(5544); }
        | ORDINALITY_SYM { MDBP_R(5545); }
        | OR_SYM { MDBP_R(5546); }
        | OTHERS_MARIADB_SYM { MDBP_R(5547); }
        | OUTER { MDBP_R(5548); }
        | OUTFILE { MDBP_R(5549); }
        | OUT_SYM { MDBP_R(5550); }
        | OVERLAPS_SYM { MDBP_R(5551); }
        | OVER_SYM { MDBP_R(5552); }
        | OWNER_SYM { MDBP_R(5553); }
        | PACKAGE_MARIADB_SYM { MDBP_R(5554); }
        | PACK_KEYS_SYM { MDBP_R(5555); }
        | PAGE_CHECKSUM_SYM { MDBP_R(5556); }
        | PAGE_SYM { MDBP_R(5557); }
        | PARAMETERS_SYM { MDBP_R(5558); }
        | PARSER_SYM { MDBP_R(5559); }
        | PARSE_VCOL_EXPR_SYM { MDBP_R(5560); }
        | PARTIAL { MDBP_R(5561); }
        | PARTITIONING_SYM { MDBP_R(5562); }
        | PARTITIONS_SYM { MDBP_R(5563); }
        | PARTITION_SYM { MDBP_R(5564); }
        | PASSWORD_SYM { MDBP_R(5565); }
        | PATH_SYM { MDBP_R(5566); }
        | PERCENTILE_CONT_SYM { MDBP_R(5567); }
        | PERCENTILE_DISC_SYM { MDBP_R(5568); }
        | PERCENT_RANK_SYM { MDBP_R(5569); }
        | PERIOD_SYM { MDBP_R(5570); }
        | PERSISTENT_SYM { MDBP_R(5571); }
        | PER_SYM { MDBP_R(5572); }
        | PHASE_SYM { MDBP_R(5573); }
        | PLUGINS_SYM { MDBP_R(5574); }
        | PLUGIN_SYM { MDBP_R(5575); }
        | PORTION_SYM { MDBP_R(5576); }
        | PORT_SYM { MDBP_R(5577); }
        | POSITION_SYM { MDBP_R(5578); }
        | PRECEDES_SYM { MDBP_R(5579); }
        | PRECEDING_SYM { MDBP_R(5580); }
        | PRECISION { MDBP_R(5581); }
        | PREPARE_SYM { MDBP_R(5582); }
        | PRESERVE_SYM { MDBP_R(5583); }
        | PREVIOUS_SYM { MDBP_R(5584); }
        | PREV_SYM { MDBP_R(5585); }
        | PRIMARY_SYM { MDBP_R(5586); }
        | PRIVATE_SYM { MDBP_R(5587); }
        | PRIVILEGES { MDBP_R(5588); }
        | PROCEDURES_SYM { MDBP_R(5589); }
        | PROCEDURE_SYM { MDBP_R(5590); }
        | PROCESSLIST_SYM { MDBP_R(5591); }
        | PROFILES_SYM { MDBP_R(5592); }
        | PROFILE_SYM { MDBP_R(5593); }
        | PROTOCOL_SYM { MDBP_R(5594); }
        | PROXY_SYM { MDBP_R(5595); }
        | PUBLISHED_SYM { MDBP_R(5596); }
        | PURGE { MDBP_R(5597); }
        | QUARTER_SYM { MDBP_R(5598); }
        | QUERY_SYM { MDBP_R(5599); }
        | QUICK { MDBP_R(5600); }
        | RAISE_MARIADB_SYM { MDBP_R(5601); }
        | RANGE_SYM { MDBP_R(5602); }
        | RANK_SYM { MDBP_R(5603); }
        | RAW_MARIADB_SYM { MDBP_R(5604); }
        | READS_SYM { MDBP_R(5605); }
        | READ_ONLY_SYM { MDBP_R(5606); }
        | READ_SYM { MDBP_R(5607); }
        | READ_WRITE_SYM { MDBP_R(5608); }
        | REAL { MDBP_R(5609); }
        | REBUILD_SYM { MDBP_R(5610); }
        | RECORD_SYM { MDBP_R(5611); }
        | RECOVER_SYM { MDBP_R(5612); }
        | RECURSIVE_SYM { MDBP_R(5613); }
        | REDIRECTION_SYM { MDBP_R(5614); }
        | REDUNDANT_SYM { MDBP_R(5615); }
        | REFERENCES { MDBP_R(5616); }
        | REF_SYM { MDBP_R(5617); }
        | REF_SYSTEM_ID_SYM { MDBP_R(5618); }
        | REGEXP { MDBP_R(5619); }
        | REGISTER_SYM { MDBP_R(5620); }
        | RELAY { MDBP_R(5621); }
        | RELAYLOG_SYM { MDBP_R(5622); }
        | RELAY_LOG_FILE_SYM { MDBP_R(5623); }
        | RELAY_LOG_POS_SYM { MDBP_R(5624); }
        | RELAY_THREAD { MDBP_R(5625); }
        | RELEASE_SYM { MDBP_R(5626); }
        | RELOAD { MDBP_R(5627); }
        | REMOVE_SYM { MDBP_R(5628); }
        | RENAME { MDBP_R(5629); }
        | REORGANIZE_SYM { MDBP_R(5630); }
        | REPAIR { MDBP_R(5631); }
        | REPEATABLE_SYM { MDBP_R(5632); }
        | REPEAT_SYM { MDBP_R(5633); }
        | REPLACE { MDBP_R(5634); }
        | REPLAY_SYM { MDBP_R(5635); }
        | REPLICATION { MDBP_R(5636); }
        | REQUEST_SYM { MDBP_R(5637); }
        | REQUIRED_SYM { MDBP_R(5638); }
        | REQUIRE_SYM { MDBP_R(5639); }
        | RESET_SYM { MDBP_R(5640); }
        | RESIGNAL_SYM { MDBP_R(5641); }
        | RESOURCES { MDBP_R(5642); }
        | RESTART_SYM { MDBP_R(5643); }
        | RESTRICT { MDBP_R(5644); }
        | REST_SYM { MDBP_R(5645); }
        | RESULT_SYM { MDBP_R(5646); }
        | RESUME_SYM { MDBP_R(5647); }
        | RETURNED_SQLSTATE_SYM { MDBP_R(5648); }
        | RETURNING_SYM { MDBP_R(5649); }
        | RETURNS_SYM { MDBP_R(5650); }
        | RETURN_MARIADB_SYM { MDBP_R(5651); }
        | REUSE_SYM { MDBP_R(5652); }
        | REVERSE_SYM { MDBP_R(5653); }
        | REVOKE { MDBP_R(5654); }
        | RIGHT { MDBP_R(5655); }
        | ROLES_SYM { MDBP_R(5656); }
        | ROLE_SYM { MDBP_R(5657); }
        | ROLLBACK_SYM { MDBP_R(5658); }
        | ROLLUP_SYM { MDBP_R(5659); }
        | ROUTINE_SYM { MDBP_R(5660); }
        | ROWCOUNT_SYM { MDBP_R(5661); }
        | ROWNUM_SYM { MDBP_R(5662); }
        | ROWS_SYM { MDBP_R(5663); }
        | ROWTYPE_MARIADB_SYM { MDBP_R(5664); }
        | ROW_COUNT_SYM { MDBP_R(5665); }
        | ROW_FORMAT_SYM { MDBP_R(5666); }
        | ROW_NUMBER_SYM { MDBP_R(5667); }
        | ROW_SYM { MDBP_R(5668); }
        | RTREE_SYM { MDBP_R(5669); }
        | SAVEPOINT_SYM { MDBP_R(5670); }
        | SCALAR_SYM { MDBP_R(5671); }
        | SCHEDULE_SYM { MDBP_R(5672); }
        | SCHEMA_NAME_SYM { MDBP_R(5673); }
        | SCRIPTS_SYM { MDBP_R(5674); }
        | SECOND_MICROSECOND_SYM { MDBP_R(5675); }
        | SECOND_SYM { MDBP_R(5676); }
        | SECRET_SYM { MDBP_R(5677); }
        | SECURITY_SYM { MDBP_R(5678); }
        | SELECT_SYM { MDBP_R(5679); }
        | SENSITIVE_SYM { MDBP_R(5680); }
        | SEPARATOR_SYM { MDBP_R(5681); }
        | SEQUENCE_SYM { MDBP_R(5682); }
        | SERIALIZABLE_SYM { MDBP_R(5683); }
        | SERIAL_SYM { MDBP_R(5684); }
        | SERVER_SYM { MDBP_R(5685); }
        | SERVICES_SYM { MDBP_R(5686); }
        | SERVICE_SYM { MDBP_R(5687); }
        | SESSION_SYM { MDBP_R(5688); }
        | SESSION_USER_SYM { MDBP_R(5689); }
        | SET { MDBP_R(5690); }
        | SETS_SYM { MDBP_R(5691); }
        | SETVAL_SYM { MDBP_R(5692); }
        | SHARE_SYM { MDBP_R(5693); }
        | SHOW { MDBP_R(5694); }
        | SHUTDOWN { MDBP_R(5695); }
        | SIGNAL_SYM { MDBP_R(5696); }
        | SIGNED_SYM { MDBP_R(5697); }
        | SIMPLE_SYM { MDBP_R(5698); }
        | SKIP_SYM { MDBP_R(5699); }
        | SLAVE { MDBP_R(5700); }
        | SLAVES { MDBP_R(5701); }
        | SLAVE_POS_SYM { MDBP_R(5702); }
        | SLOW { MDBP_R(5703); }
        | SMALLINT { MDBP_R(5704); }
        | SNAPSHOT_SYM { MDBP_R(5705); }
        | SOCKET_SYM { MDBP_R(5706); }
        | SOFT_SYM { MDBP_R(5707); }
        | SONAME_SYM { MDBP_R(5708); }
        | SOUNDS_SYM { MDBP_R(5709); }
        | SOURCE_SYM { MDBP_R(5710); }
        | SPATIAL_SYM { MDBP_R(5711); }
        | SPECIFIC_SYM { MDBP_R(5712); }
        | SQLEXCEPTION_SYM { MDBP_R(5713); }
        | SQLSTATE_SYM { MDBP_R(5714); }
        | SQLWARNING_SYM { MDBP_R(5715); }
        | SQL_AFTER_GTIDS_SYM { MDBP_R(5716); }
        | SQL_BEFORE_GTIDS_SYM { MDBP_R(5717); }
        | SQL_BIG_RESULT { MDBP_R(5718); }
        | SQL_BUFFER_RESULT { MDBP_R(5719); }
        | SQL_CACHE_SYM { MDBP_R(5720); }
        | SQL_CALC_FOUND_ROWS { MDBP_R(5721); }
        | SQL_NO_CACHE_SYM { MDBP_R(5722); }
        | SQL_SMALL_RESULT { MDBP_R(5723); }
        | SQL_SYM { MDBP_R(5724); }
        | SQL_THREAD { MDBP_R(5725); }
        | SSL_SYM { MDBP_R(5726); }
        | STAGE_SYM { MDBP_R(5727); }
        | STARTING { MDBP_R(5728); }
        | STARTS_SYM { MDBP_R(5729); }
        | START_SYM { MDBP_R(5730); }
        | STATEMENT_SYM { MDBP_R(5731); }
        | STATIC_SYM { MDBP_R(5732); }
        | STATS_AUTO_RECALC_SYM { MDBP_R(5733); }
        | STATS_PERSISTENT_SYM { MDBP_R(5734); }
        | STATS_SAMPLE_PAGES_SYM { MDBP_R(5735); }
        | STATUS_SYM { MDBP_R(5736); }
        | STDDEV_SAMP_SYM { MDBP_R(5737); }
        | STD_SYM { MDBP_R(5738); }
        | STOP_SYM { MDBP_R(5739); }
        | STORAGE_SYM { MDBP_R(5740); }
        | STORED_SYM { MDBP_R(5741); }
        | STRAIGHT_JOIN { MDBP_R(5742); }
        | STRING_SYM { MDBP_R(5743); }
        | ST_COLLECT_SYM { MDBP_R(5744); }
        | SUBCLASS_ORIGIN_SYM { MDBP_R(5745); }
        | SUBDATE_SYM { MDBP_R(5746); }
        | SUBJECT_SYM { MDBP_R(5747); }
        | SUBPARTITIONS_SYM { MDBP_R(5748); }
        | SUBPARTITION_SYM { MDBP_R(5749); }
        | SUBSTRING { MDBP_R(5750); }
        | SUM_SYM { MDBP_R(5751); }
        | SUPER_SYM { MDBP_R(5752); }
        | SUSPEND_SYM { MDBP_R(5753); }
        | SWAPS_SYM { MDBP_R(5754); }
        | SWITCHES_SYM { MDBP_R(5755); }
        | SYSDATE { MDBP_R(5756); }
        | SYSTEM { MDBP_R(5757); }
        | SYSTEM_TIME_SYM { MDBP_R(5758); }
        | TABLES { MDBP_R(5759); }
        | TABLESPACE { MDBP_R(5760); }
        | TABLE_CHECKSUM_SYM { MDBP_R(5761); }
        | TABLE_NAME_SYM { MDBP_R(5762); }
        | TABLE_SYM { MDBP_R(5763); }
        | TEMPORARY { MDBP_R(5764); }
        | TEMPTABLE_SYM { MDBP_R(5765); }
        | TERMINATED { MDBP_R(5766); }
        | TEXT_SYM { MDBP_R(5767); }
        | THAN_SYM { MDBP_R(5768); }
        | THEN_SYM { MDBP_R(5769); }
        | THREADS_SYM { MDBP_R(5770); }
        | TIES_SYM { MDBP_R(5771); }
        | TIMESTAMP { MDBP_R(5772); }
        | TIMESTAMP_ADD { MDBP_R(5773); }
        | TIMESTAMP_DIFF { MDBP_R(5774); }
        | TIME_SYM { MDBP_R(5775); }
        | TINYBLOB { MDBP_R(5776); }
        | TINYINT { MDBP_R(5777); }
        | TINYTEXT { MDBP_R(5778); }
        | TO_DATE { MDBP_R(5779); }
        | TO_SYM { MDBP_R(5780); }
        | TRAILING { MDBP_R(5781); }
        | TRANSACTIONAL_SYM { MDBP_R(5782); }
        | TRANSACTION_SYM { MDBP_R(5783); }
        | TRIGGERS_SYM { MDBP_R(5784); }
        | TRIGGER_SYM { MDBP_R(5785); }
        | TRIM { MDBP_R(5786); }
        | TRIM_ORACLE { MDBP_R(5787); }
        | TRUE_SYM { MDBP_R(5788); }
        | TRUNCATE_SYM { MDBP_R(5789); }
        | TYPESCRIPT_SYM { MDBP_R(5790); }
        | TYPE_SYM { MDBP_R(5791); }
        | UNBOUNDED_SYM { MDBP_R(5792); }
        | UNCOMMITTED_SYM { MDBP_R(5793); }
        | UNDEFINED_SYM { MDBP_R(5794); }
        | UNDO_SYM { MDBP_R(5795); }
        | UNICODE_SYM { MDBP_R(5796); }
        | UNINSTALL_SYM { MDBP_R(5797); }
        | UNION_SYM { MDBP_R(5798); }
        | UNIQUE_SYM { MDBP_R(5799); }
        | UNKNOWN_SYM { MDBP_R(5800); }
        | UNLOCK_SYM { MDBP_R(5801); }
        | UNPUBLISHED_SYM { MDBP_R(5802); }
        | UNSIGNED { MDBP_R(5803); }
        | UNTIL_SYM { MDBP_R(5804); }
        | UPDATE_SYM { MDBP_R(5805); }
        | UPGRADE_SYM { MDBP_R(5806); }
        | URL_SYM { MDBP_R(5807); }
        | USAGE { MDBP_R(5808); }
        | USERS_SYM { MDBP_R(5809); }
        | USER_SYM { MDBP_R(5810); }
        | USE_FRM { MDBP_R(5811); }
        | USE_SYM { MDBP_R(5812); }
        | USING { MDBP_R(5813); }
        | UTC_DATE_SYM { MDBP_R(5814); }
        | UTC_TIMESTAMP_SYM { MDBP_R(5815); }
        | UTC_TIME_SYM { MDBP_R(5816); }
        | VALIDATION_SYM { MDBP_R(5817); }
        | VALUES { MDBP_R(5818); }
        | VALUE_SYM { MDBP_R(5819); }
        | VARBINARY { MDBP_R(5820); }
        | VARCHAR { MDBP_R(5821); }
        | VARCHAR2_MARIADB_SYM { MDBP_R(5822); }
        | VARIABLES { MDBP_R(5823); }
        | VARIANCE_SYM { MDBP_R(5824); }
        | VARYING { MDBP_R(5825); }
        | VAR_SAMP_SYM { MDBP_R(5826); }
        | VECTOR_SYM { MDBP_R(5827); }
        | VENDORS_SYM { MDBP_R(5828); }
        | VENDOR_SYM { MDBP_R(5829); }
        | VERSIONING_SYM { MDBP_R(5830); }
        | VIA_SYM { MDBP_R(5831); }
        | VIEWS_SYM { MDBP_R(5832); }
        | VIEW_SYM { MDBP_R(5833); }
        | VIRTUAL_SYM { MDBP_R(5834); }
        | VISIBLE_SYM { MDBP_R(5835); }
        | WAIT_SYM { MDBP_R(5836); }
        | WARNINGS { MDBP_R(5837); }
        | WEEK_SYM { MDBP_R(5838); }
        | WEIGHT_STRING_SYM { MDBP_R(5839); }
        | WHEN_SYM { MDBP_R(5840); }
        | WHERE { MDBP_R(5841); }
        | WHILE_SYM { MDBP_R(5842); }
        | WINDOW_SYM { MDBP_R(5843); }
        | WITH { MDBP_R(5844); }
        | WITHIN { MDBP_R(5845); }
        | WITHOUT { MDBP_R(5846); }
        | WORK_SYM { MDBP_R(5847); }
        | WRAPPER_SYM { MDBP_R(5848); }
        | WRITE_SYM { MDBP_R(5849); }
        | X509_SYM { MDBP_R(5850); }
        | XA_SYM { MDBP_R(5851); }
        | XML_SYM { MDBP_R(5852); }
        | XOR { MDBP_R(5853); }
        | YEAR_MONTH_SYM { MDBP_R(5854); }
        | YEAR_SYM { MDBP_R(5855); }
        | ZEROFILL { MDBP_R(5856); }
        ;

mrs_annotation:
          '@' LEX_HOSTNAME { MDBP_R(5857); }
        ;
