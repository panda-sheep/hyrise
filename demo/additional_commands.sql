/*
CREATE TABLE LINEITEM (
  L_ORDERKEY INTEGER,
  L_PARTKEY INTEGER,
  L_SUPPKEY INTEGER,
  L_LINENUMBER INTEGER,
  L_QUANTITY REAL,
  L_EXTENDEDPRICE REAL,
  L_DISCOUNT REAL,
  L_TAX REAL,
  L_RETURNFLAG CHAR(1),
  L_LINESTATUS CHAR(1),
  L_SHIPDATE DATE,
  L_COMMITDATE DATE,
  L_RECEIPTDATE DATE,
  L_SHIPINSTRUCT CHAR(25),
  L_SHIPMODE CHAR(10),
  L_COMMENT VARCHAR(44),
  PRIMARY KEY (L_ORDERKEY, L_LINENUMBER));
);
*/

/*
postgres online: https://sqliteonline.com/
postres: list tables
SELECT table_name FROM information_schema.tables WHERE table_schema='public'
*/

CREATE TABLE LI (
  L_SUPPKEY INTEGER
);

/* add a column */
/* drop a column */
/* rename a column */
/* add a constraint to a column */
/* rename a table */

ALTER TABLE LI ADD COLUMN L_ORDERKEY INTEGER NOT NULL PRIMARY KEY;
ALTER TABLE LI DROP COLUMN L_ORDERKEY;
ALTER TABLE LI ADD COLUMN L_OORDERKEY INTEGER;
ALTER TABLE LI ADD COLUMN L_LINENUMBER INTEGER;
ALTER TABLE LI ADD CONSTRAINT pkey_l_orderkey_l_linenumber PRIMARY KEY (L_OORDERKEY, L_LINENUMBER);
ALTER TABLE LI RENAME COLUMN L_OORDERKEY TO L_ORDERKEY;
ALTER TABLE LI RENAME TO LINEITEM;
ALTER TABLE LINEITEM DROP CONSTRAINT pkey_l_orderkey_l_linenumber;
ALTER TABLE LINEITEM drop COLUMN l_suppkey;
ALTER TABLE LINEITEM DROP COLUMN l_orderkey;
INSERT INTO LINEITEM VALUES (1), (2), (3);
ALTER TABLE lineitem add COLUMN int_nullable INTEGER;
/* The following line results in an error since the table already contains values but the new column is not nullable
ALTER TABLE lineitem add COLUMN int_notnullable INTEGER NOT NULL;
*/
ALTER TABLE lineitem add COLUMN int_notnullable INTEGER NOT NULL DEFAULT 1337;
