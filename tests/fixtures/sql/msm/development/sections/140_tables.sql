-- Copyright (c) 2026, MariaDB plc.
-- Tables of the shop schema
CREATE TABLE customer (
  id INT PRIMARY KEY,
  name VARCHAR(40)
);
ALTER TABLE customer ADD COLUMN email VARCHAR(80);
CREATE TABLE orders (
  id INT PRIMARY KEY,
  customer_id INT,
  FOREIGN KEY (customer_id) REFERENCES customer (id)
);
ALTER TABLE missing ADD COLUMN x INT;
-- footer
