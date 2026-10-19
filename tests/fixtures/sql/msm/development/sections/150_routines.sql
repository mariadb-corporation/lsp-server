-- Copyright (c) 2026, MariaDB plc.
-- Routines and views of the shop schema
DELIMITER %%
CREATE FUNCTION order_count(c INT) RETURNS INT
BEGIN
  RETURN (SELECT COUNT(*) FROM orders WHERE customer_id = c);
END%%
DELIMITER ;
CREATE VIEW customer_orders AS
  SELECT c.name, o.id FROM customer c JOIN orders o ON o.customer_id = c.id;
