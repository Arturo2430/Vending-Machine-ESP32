-- ********************************************************************
-- Máquina expendedora basada en diagramas de estado
-- Esquema SQLite
-- ********************************************************************
-- Notas:
--   * El dinero se guarda SIEMPRE como entero en centavos (nunca REAL).
--   * Las cantidades (stock, capacidad, reservado) son enteras.
-- ********************************************************************

PRAGMA foreign_keys = ON;

-- administradores: Cuentas que pueden autorizar mantenimiento, reposición y recargas.
CREATE TABLE administradores (
    id_admin        INTEGER PRIMARY KEY AUTOINCREMENT,
    nombre_acceso   TEXT    NOT NULL UNIQUE,
    hash_contrasena TEXT    NOT NULL,
    hash_pin        TEXT,
    sal             TEXT    NOT NULL,
    activo          INTEGER NOT NULL DEFAULT 1 CHECK (activo IN (0, 1))
);

-- productos : Catálogo de productos, independiente del canal donde se colocan.
CREATE TABLE productos (
    id_producto                INTEGER PRIMARY KEY AUTOINCREMENT,
    codigo_unico               TEXT    NOT NULL UNIQUE,
    nombre                     TEXT    NOT NULL,
    costo_referencia_centavos  INTEGER,
    activo                     INTEGER NOT NULL DEFAULT 1 CHECK (activo IN (0, 1))
);

-- slots :Los 4 canales físicos de la máquina.
-- El canal es fijo; el producto que contiene puede cambiar
CREATE TABLE slots (
    id_slot         INTEGER PRIMARY KEY CHECK (id_slot BETWEEN 1 AND 4),
    id_producto     INTEGER REFERENCES productos (id_producto),
    precio_centavos INTEGER NOT NULL CHECK (precio_centavos >= 0),
    capacidad       INTEGER NOT NULL CHECK (capacidad >= 0),
    stock           INTEGER NOT NULL DEFAULT 0 CHECK (stock >= 0),
    reservado       INTEGER NOT NULL DEFAULT 0 CHECK (reservado >= 0),
    habilitado      INTEGER NOT NULL DEFAULT 1 CHECK (habilitado IN (0, 1)),
    version         INTEGER NOT NULL DEFAULT 0,
    CHECK (stock + reservado <= capacidad)
);

-- tarjetas_demo : Tarjetas RFID simuladas usadas como método de pago.
CREATE TABLE tarjetas_demo (
    id_tarjeta          INTEGER PRIMARY KEY AUTOINCREMENT,
    uid                 TEXT    NOT NULL UNIQUE,
    saldo_centavos      INTEGER NOT NULL DEFAULT 0 CHECK (saldo_centavos >= 0),
    reserva_centavos    INTEGER NOT NULL DEFAULT 0 CHECK (reserva_centavos >= 0),
    habilitada          INTEGER NOT NULL DEFAULT 1 CHECK (habilitada IN (0, 1)),
    CHECK (saldo_centavos - reserva_centavos >= 0)
);

-- transacciones : Registro central de cada compra, desde que se autoriza hasta que se cierra.
CREATE TABLE transacciones (
    id_transaccion              INTEGER PRIMARY KEY AUTOINCREMENT,
    id_slot                     INTEGER NOT NULL REFERENCES slots (id_slot),
    id_tarjeta                  INTEGER REFERENCES tarjetas_demo (id_tarjeta),
    metodo                      TEXT    NOT NULL,
    precio_historico_centavos   INTEGER NOT NULL CHECK (precio_historico_centavos >= 0),
    nombre_historico            TEXT    NOT NULL,
    estado                      TEXT    NOT NULL CHECK (estado IN ('RESERVADA', 'CONFIRMADA', 'INCIERTA')),
    secuencia                   INTEGER
);

-- comandos : La orden física de venta ("VEND") mandada al motor, y su resultado.
CREATE TABLE comandos (
    id_comando       INTEGER PRIMARY KEY AUTOINCREMENT,
    id_transaccion   INTEGER NOT NULL UNIQUE REFERENCES transacciones (id_transaccion),
    secuencia_unica  TEXT UNIQUE,
    estado           TEXT,
    resultado        TEXT CHECK (resultado IS NULL OR resultado IN ('DELIVERED', 'REJECTED_BEFORE_MOTION', 'UNCERTAIN'))
);

-- reposiciones :Cada reabastecimiento de un canal: se crea en iniciarReposicion
-- (antes de la manipulación física) y se cierra en guardarReposicion.
CREATE TABLE reposiciones (
    id_reposicion               INTEGER PRIMARY KEY AUTOINCREMENT,
    id_admin                    INTEGER NOT NULL REFERENCES administradores (id_admin),
    id_slot                     INTEGER NOT NULL REFERENCES slots (id_slot),
    origen                      TEXT    NOT NULL CHECK (origen IN ('teclado', 'web')),
    cantidad_anterior           INTEGER NOT NULL,
    cantidad_final              INTEGER,
    precio_anterior_centavos    INTEGER NOT NULL,
    precio_nuevo_centavos       INTEGER,
    estado                      TEXT    NOT NULL CHECK (estado IN ('PENDIENTE', 'CERRADA')),
    request_id                  TEXT    NOT NULL UNIQUE
);

-- movimientos_stock : Bitácora de auditoría de cada cambio de stock.
-- No es la fuente de verdad del stock actual. Es evidencia histórica.
-- ---------------------------------------------------------------------
CREATE TABLE movimientos_stock (
    id_movimiento_stock  INTEGER PRIMARY KEY AUTOINCREMENT,
    id_slot              INTEGER NOT NULL REFERENCES slots (id_slot),
    referencia_operacion TEXT,
    delta                INTEGER NOT NULL,
    motivo               TEXT    NOT NULL CHECK (motivo IN ('VENTA', 'REPOSICION', 'AJUSTE')),
    operador             INTEGER REFERENCES administradores (id_admin)
);

-- movimientos_saldo : Bitácora de auditoría de cada cambio de saldo de tarjeta (compras y recargas).
CREATE TABLE movimientos_saldo (
    id_movimiento_saldo  INTEGER PRIMARY KEY AUTOINCREMENT,
    id_tarjeta           INTEGER NOT NULL REFERENCES tarjetas_demo (id_tarjeta),
    referencia_opcional  TEXT,
    delta_centavos       INTEGER NOT NULL,
    tipo                 TEXT    NOT NULL CHECK (tipo IN ('COMPRA', 'RECARGA')),
    operador             INTEGER REFERENCES administradores (id_admin),
    request_id           TEXT    NOT NULL UNIQUE
);

-- denominaciones : Catálogo de valores de efectivo (monedas y billetes) aceptados
CREATE TABLE denominaciones (
    id_denominacion   INTEGER PRIMARY KEY AUTOINCREMENT,
    valor_centavos    INTEGER NOT NULL CHECK (valor_centavos > 0),
    tipo              TEXT    NOT NULL CHECK (tipo IN ('moneda', 'billete')),
    activa            INTEGER NOT NULL DEFAULT 1 CHECK (activa IN (0, 1)),
    UNIQUE (valor_centavos, tipo)
);

-- caja_efectivo : Cantidad actual disponible de cada denominación dentro de la
-- máquina, usada para decidir si hay suficiente para dar cambio. Es la fuente
-- de verdad (igual que slots.stock); movimientos_efectivo es solo bitácora.
CREATE TABLE caja_efectivo (
    id_denominacion  INTEGER PRIMARY KEY REFERENCES denominaciones (id_denominacion),
    cantidad         INTEGER NOT NULL DEFAULT 0 CHECK (cantidad >= 0),
    version          INTEGER NOT NULL DEFAULT 0
);

-- movimientos_efectivo : Bitácora de auditoría de cada cambio en caja_efectivo:
-- piezas recibidas como pago, piezas entregadas como cambio (ambas ligadas a la
-- transacción que las originó) o ajustes manuales del administrador desde la web.
-- Cubre a la vez el detalle de composición del pago y la auditoría de cantidad,
-- para no duplicar el mismo hecho en dos tablas distintas.
CREATE TABLE movimientos_efectivo (
    id_movimiento_efectivo  INTEGER PRIMARY KEY AUTOINCREMENT,
    id_denominacion         INTEGER NOT NULL REFERENCES denominaciones (id_denominacion),
    id_transaccion          INTEGER REFERENCES transacciones (id_transaccion),
    delta                   INTEGER NOT NULL,
    motivo                  TEXT    NOT NULL CHECK (motivo IN ('PAGO_RECIBIDO', 'CAMBIO_ENTREGADO', 'AJUSTE')),
    operador                INTEGER REFERENCES administradores (id_admin),
    request_id              TEXT UNIQUE,
    secuencia               INTEGER
);

-- ---------------------------------------------------------------------
-- eventos
-- Bitácora general de diagnóstico (errores, advertencias), separada
-- de los movimientos de negocio.
-- ---------------------------------------------------------------------
CREATE TABLE eventos (
    id_evento              INTEGER PRIMARY KEY AUTOINCREMENT,
    tipo                   TEXT    NOT NULL CHECK (tipo IN ('ERROR', 'INFO', 'ADVERTENCIA')),
    operacion_relacionada  TEXT,
    codigo_error           TEXT,
    secuencia              INTEGER,
    datos_breves           TEXT
);

-- ---------------------------------------------------------------------
-- configuraciones
-- Estado global del sistema. Se espera una sola fila activa
-- (id_configuracion = 1) que el ESP32 consulta al arrancar.
-- ---------------------------------------------------------------------
CREATE TABLE configuraciones (
    id_configuracion       INTEGER PRIMARY KEY AUTOINCREMENT,
    version_esquema        INTEGER NOT NULL,
    secuencia_global       INTEGER,
    modalidad_reposicion   TEXT CHECK (modalidad_reposicion IS NULL OR modalidad_reposicion IN ('teclado', 'web')),
    estado_inicializacion  TEXT
);

-- ---------------------------------------------------------------------
-- Índices de apoyo para las consultas más frecuentes
-- (las columnas UNIQUE ya crean su propio índice automáticamente;
-- estos son adicionales, para búsquedas por FK que se consultan mucho)
-- ---------------------------------------------------------------------
CREATE INDEX idx_transacciones_slot     ON transacciones (id_slot);
CREATE INDEX idx_transacciones_tarjeta  ON transacciones (id_tarjeta);
CREATE INDEX idx_reposiciones_slot      ON reposiciones (id_slot);
CREATE INDEX idx_mov_stock_slot         ON movimientos_stock (id_slot);
CREATE INDEX idx_mov_saldo_tarjeta      ON movimientos_saldo (id_tarjeta);
CREATE INDEX idx_mov_efectivo_denominacion ON movimientos_efectivo (id_denominacion);
CREATE INDEX idx_mov_efectivo_transaccion  ON movimientos_efectivo (id_transaccion);

-- ---------------------------------------------------------------------
-- Catálogo fijo de denominaciones de pesos mexicanos, usado en el pago
-- simulado por teclado (no hay aceptador físico de efectivo). El valor $20
-- existe como moneda y como billete a la vez, por eso ambas filas conviven.
-- ---------------------------------------------------------------------
INSERT INTO denominaciones (valor_centavos, tipo) VALUES
    (100,    'moneda'),   -- $1
    (200,    'moneda'),   -- $2
    (500,    'moneda'),   -- $5
    (1000,   'moneda'),   -- $10
    (2000,   'moneda'),   -- $20 (moneda)
    (2000,   'billete'),  -- $20 (billete)
    (5000,   'billete'),  -- $50
    (10000,  'billete'),  -- $100
    (20000,  'billete'),  -- $200
    (50000,  'billete'),  -- $500
    (100000, 'billete');  -- $1000
