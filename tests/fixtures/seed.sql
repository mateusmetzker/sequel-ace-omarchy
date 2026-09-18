-- Test database used by the Linux port's smoke test and by hand testing.
-- Load with: mariadb -uroot -p < seed.sql   (or linux/scripts/test-server.sh seed)
DROP DATABASE IF EXISTS sequel_ace_test;
CREATE DATABASE sequel_ace_test CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE sequel_ace_test;

CREATE TABLE customers (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT,
  name VARCHAR(120) NOT NULL,
  email VARCHAR(190) DEFAULT NULL,
  balance DECIMAL(12,2) NOT NULL DEFAULT 0.00,
  status ENUM('active','inactive','banned') NOT NULL DEFAULT 'active',
  tags SET('vip','beta','partner') DEFAULT NULL,
  is_verified BIT(1) NOT NULL DEFAULT b'0',
  notes TEXT,
  avatar BLOB,
  meta JSON DEFAULT NULL,
  location POINT DEFAULT NULL,
  born DATE DEFAULT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME(3) DEFAULT NULL ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (id),
  UNIQUE KEY uq_email (email),
  KEY idx_status_created (status, created_at)
) ENGINE=InnoDB COMMENT='Customer master data';

INSERT INTO customers (name,email,balance,status,tags,is_verified,notes,avatar,meta,location,born) VALUES
 ('Ana Souza','ana@example.com',150.75,'active','vip,beta',b'1','Primeira cliente',X'89504E470D0A','{"plan":"pro","langs":["pt","en"]}',ST_GeomFromText('POINT(-46.63 -23.55)'),'1990-04-12'),
 ('Bruno Lima','bruno@example.com',-20.00,'inactive',NULL,b'0',NULL,NULL,NULL,NULL,'1985-11-30'),
 ('Carla Menezes',NULL,0,'banned','partner',b'0','Texto com ç, ã, é e emoji 😀',NULL,'{"plan":"free"}',NULL,NULL),
 ('Dário Öztürk','dario@example.com',99999.99,'active','vip',b'1',REPEAT('lorem ipsum ',50),NULL,NULL,ST_GeomFromText('POINT(28.97 41.01)'),'2001-01-01');

CREATE TABLE orders (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  customer_id INT UNSIGNED NOT NULL,
  total DECIMAL(10,2) NOT NULL,
  placed_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY fk_orders_customer (customer_id),
  CONSTRAINT fk_orders_customer FOREIGN KEY (customer_id) REFERENCES customers (id) ON DELETE CASCADE ON UPDATE CASCADE
) ENGINE=InnoDB;
INSERT INTO orders (customer_id,total) VALUES (1,10.5),(1,20),(2,5.25),(4,1000);

CREATE TABLE no_pk_log (level VARCHAR(10), message VARCHAR(255), logged_at DATETIME DEFAULT CURRENT_TIMESTAMP);
INSERT INTO no_pk_log (level,message) VALUES ('info','boot'),('warn','disk 90%'),('info','boot');

CREATE TABLE big_numbers (id INT PRIMARY KEY AUTO_INCREMENT, v INT);
INSERT INTO big_numbers (v) SELECT seq FROM seq_1_to_3000;

CREATE VIEW v_active_customers AS SELECT id, name, email FROM customers WHERE status = 'active';

DELIMITER //
CREATE PROCEDURE sp_count_customers(OUT cnt INT) BEGIN SELECT COUNT(*) INTO cnt FROM customers; END//
DELIMITER ;
CREATE FUNCTION fn_double(x INT) RETURNS INT DETERMINISTIC RETURN x * 2;
CREATE TRIGGER trg_orders_bi BEFORE INSERT ON orders FOR EACH ROW SET NEW.total = ABS(NEW.total);

CREATE DATABASE IF NOT EXISTS sequel_ace_empty;
