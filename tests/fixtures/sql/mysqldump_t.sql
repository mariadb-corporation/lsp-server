drop database if exists mysqldump_test_db;
create database mysqldump_test_db;
use mysqldump_test_db;

# Autoincrement

create table `at1` (
    t1_name varchar(255) default null,
    t1_id int(10) unsigned not null auto_increment,
    key (t1_name),
    primary key (t1_id)
) auto_increment = 1000 default charset=latin1;

insert into at1 (t1_name) values('bla');
insert into at1 (t1_name) values('bla');
insert into at1 (t1_name) values('bla');

# --exec $MYSQL_DUMP --skip-comments test t1 > $MYSQLTEST_VARDIR/tmp/bug19025.sql

# Views

CREATE TABLE tv2 (
  a varchar(30) default NULL,
  KEY a (a(5))
);

INSERT INTO tv2 VALUES ('alfred');
INSERT INTO tv2 VALUES ('angie');
INSERT INTO tv2 VALUES ('bingo');
INSERT INTO tv2 VALUES ('waffle');
INSERT INTO tv2 VALUES ('lemon');
create view v2 as select * from tv2 where a like 'a%' with check option;

create table tv1(a int, b int, c varchar(30));

insert into tv1 values(1, 2, "one"), (2, 4, "two"), (3, 6, "three");

create view v3 as
select * from tv1;

create  view v1 as
select * from v3 where b in (1, 2, 3, 4, 5, 6, 7);

create  view v4 as
select v3.a from v3, v1 where v1.a=v3.a and v3.b=3 limit 1;

# --exec $MYSQL_DUMP --skip-comments test

# Test for dumping triggers

CREATE TABLE t1 (a int, b bigint default NULL);
CREATE TABLE t2 (a int);
delimiter |
create trigger trg1 before insert on t1 for each row
begin
  if new.a > 10 then
    set new.a := 10;
    set new.a := 11;
  end if;
end|
create trigger trg2 before update on t1 for each row begin
  if old.a % 2 = 0 then set new.b := 12; end if;
end|
set sql_mode="traditional"|
create trigger trg3 after update on t1 for each row
begin
  if new.a = -1 then
    set @fired:= "Yes";
  end if;
end|
create trigger trg4 before insert on t2 for each row
begin
  if new.a > 10 then
    set @fired:= "No";
  end if;
end|
set sql_mode=default|
delimiter ;

INSERT INTO t1 (a) VALUES (1),(2),(3),(22);
update t1 set a = 4 where a=3;
# Triggers should be dumped by default
# --exec $MYSQL_DUMP --skip-comments --databases test
# Skip dumping triggers
# --exec $MYSQL_DUMP --skip-comments --databases --skip-triggers test
# Dump and reload...
# --exec $MYSQL_DUMP --skip-comments --databases test > $MYSQLTEST_VARDIR/tmp/mysqldump.sql

# --exec $MYSQL test < $MYSQLTEST_VARDIR/tmp/mysqldump.sql


# Events

create event ee1 on schedule at '2035-12-31 20:01:23' do set @a=5;
create event ee2 on schedule at '2029-12-31 21:01:23' do set @a=5;
# --exec $MYSQL_DUMP --events second > $MYSQLTEST_VARDIR/tmp/bug16853-2.sql


# Routines

SET GLOBAL log_bin_trust_function_creators = 1;

CREATE TABLE tr1 (id int);
INSERT INTO tr1 VALUES(1), (2), (3), (4), (5);

DELIMITER //
CREATE FUNCTION `bug9056_func1`(a INT, b INT) RETURNS int(11) RETURN a+b //
CREATE PROCEDURE `bug9056_proc1`(IN a INT, IN b INT, OUT c INT)
BEGIN SELECT a+b INTO c; end  //

create function bug9056_func2(f1 char binary) returns char
begin
  set f1= concat( 'hello', f1 );
  return f1;
end //

CREATE PROCEDURE bug9056_proc2(OUT a INT)
BEGIN
  select sum(id) from tr1 into a;
END //

DELIMITER ;

set sql_mode='ansi';
create procedure `a'b` () select 1; # to fix syntax highlighting :')
set sql_mode=default;

# Dump the DB and ROUTINES
# --exec $MYSQL_DUMP --skip-comments --routines --databases test

# Libraries

/*!90200 CREATE LIBRARY lib1 LANGUAGE JAVASCRIPT AS $$ $$ */;

set sql_mode='ansi';
/*!90200 CREATE LIBRARY `a'b`
    LANGUAGE JAVASCRIPT
AS  $$
      export function f(n) {
        return n;
      }
    $$
*/;
set sql_mode=default;

# Sequences (MariaDB only - MySQL skips /*M! and MariaDB skips /*!90200 above)

/*M!100300 CREATE SEQUENCE seq1 */;

# advanced by one value, with the cache off so the position is predictable:
# with a cache the sequence hands out a whole cache worth of values at once
/*M!100300 CREATE SEQUENCE seq2 START WITH 100 INCREMENT BY 5 NOCACHE */;
/*M!100300 DO NEXTVAL(seq2) */;

/*M!100300 CREATE SEQUENCE seq3 MINVALUE 1 MAXVALUE 1000 CYCLE */;

# a cycling sequence which wrapped twice (1, 2, 1, 2, 1): cycle_count is 2
/*M!100300 CREATE SEQUENCE seq4 MINVALUE 1 MAXVALUE 2 NOCACHE CYCLE */;
/*M!100300 DO NEXTVAL(seq4) */;
/*M!100300 DO NEXTVAL(seq4) */;
/*M!100300 DO NEXTVAL(seq4) */;
/*M!100300 DO NEXTVAL(seq4) */;
/*M!100300 DO NEXTVAL(seq4) */;

# wrapped once, then altered to NOCYCLE
/*M!100300 CREATE SEQUENCE seq5 MINVALUE 1 MAXVALUE 2 NOCACHE CYCLE */;
/*M!100300 DO NEXTVAL(seq5) */;
/*M!100300 DO NEXTVAL(seq5) */;
/*M!100300 DO NEXTVAL(seq5) */;
/*M!100300 ALTER SEQUENCE seq5 NOCYCLE */;

set sql_mode='ansi';
/*M!100300 CREATE SEQUENCE `a'b seq` */;
set sql_mode=default;

# Check constraints (MariaDB only here: MySQL spells non-enforcement as a
# per-constraint NOT ENFORCED flag, MariaDB as a session variable, so the two
# are not the same fixture - see MARIADB_DUMP_LOAD.md section 5.2)

/*M!100201 CREATE TABLE ck1 (
  a INT CHECK (a > 0),
  b INT,
  c VARCHAR(20),
  CONSTRAINT b_range CHECK (b BETWEEN 1 AND 100),
  CONSTRAINT c_ck CHECK (c <> 'KEY' AND c NOT IN ('a,b','(x)')),
  KEY idx_b (b)
) */;
/*M!100201 INSERT INTO ck1 VALUES (1, 50, 'ok') */;

# Oracle-mode packages (MariaDB only: MySQL has no PACKAGE routine type and no
# ORACLE sql_mode - see MARIADB_DUMP_LOAD.md section 5.3). The bodies contain
# semicolons, which survive the client-side splitter only because it treats
# /*M! ... */ as one comment span.

/*M!100300 set sql_mode=oracle */;
/*M!100300 CREATE PACKAGE pkg1 AS
  PROCEDURE p1(a INT);
  FUNCTION f1(b INT) RETURN INT;
END */;
/*M!100300 CREATE PACKAGE BODY pkg1 AS
  vc INT := 10;
  PROCEDURE p1(a INT) AS
  BEGIN
    SELECT a FROM DUAL;
  END;
  FUNCTION f1(b INT) RETURN INT AS
  BEGIN
    RETURN b + vc;
  END;
END */;

# a specification with no body of its own, and a name which needs quoting
/*M!100300 CREATE PACKAGE "a'b pkg" AS FUNCTION g() RETURN INT; END */;
/*M!100300 set sql_mode=default */;

# a standalone function of the same name as pkg1, to show the two namespaces
# do not collide
/*M!100300 CREATE FUNCTION pkg1(x INT) RETURNS INT DETERMINISTIC RETURN x */;
