CREATE TABLE users (
    id            INT UNSIGNED NOT NULL AUTO_INCREMENT,
    nickname      VARCHAR(16)  NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    coin          INT          NOT NULL DEFAULT 100, -- 시작 자금
    PRIMARY KEY (id),
    UNIQUE KEY uk_nickname (nickname)
);

CREATE TABLE inventory (
    id      INT UNSIGNED     NOT NULL AUTO_INCREMENT,
    user_id INT UNSIGNED     NOT NULL,
    crop    TINYINT UNSIGNED NOT NULL,
    seeds   INT              NOT NULL DEFAULT 0,
    held    INT              NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE KEY uk_user_id_crop (user_id, crop)
);

CREATE TABLE farm_tiles (
    id         INT UNSIGNED     NOT NULL AUTO_INCREMENT,
    user_id    INT UNSIGNED     NOT NULL,
    x          TINYINT UNSIGNED NOT NULL,
    y          TINYINT UNSIGNED NOT NULL,
    state      TINYINT UNSIGNED NOT NULL DEFAULT 0,
    crop       TINYINT UNSIGNED NOT NULL DEFAULT 0,
    stage      TINYINT UNSIGNED NOT NULL DEFAULT 0,
    watered_at BIGINT UNSIGNED  NOT NULL DEFAULT 0,
    PRIMARY KEY (id),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE,
    UNIQUE KEY uk_user_id_x_y (user_id, x, y)
);