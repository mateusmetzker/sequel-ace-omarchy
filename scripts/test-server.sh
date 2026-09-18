#!/usr/bin/env bash
#
# Throw-away MariaDB server for developing and testing the Linux port.
#
# Runs an unprivileged mariadbd on 127.0.0.1:33306 (root password "root") with
# its data directory under $SA_TEST_SERVER_DIR (default: ~/.cache/sequel-ace-test-server).
# Uses the mariadbd found in PATH; when none is installed and pacman is
# available (Arch Linux), the mariadb packages are downloaded and extracted
# locally without installing anything system-wide.
#
#   test-server.sh start     initialise (first run) and start the server
#   test-server.sh stop
#   test-server.sh status
#   test-server.sh seed      load tests/fixtures/seed.sql
#   test-server.sh env       print the SA_TEST_MYSQL_* variables to eval
#
# Licensed under the MIT license. See LICENSE at the repository root.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="${SA_TEST_SERVER_DIR:-$HOME/.cache/sequel-ace-test-server}"
PORT="${SA_TEST_SERVER_PORT:-33306}"
DATA_DIR="$BASE_DIR/data"
RUN_DIR="$BASE_DIR/run"
PKG_DIR="$BASE_DIR/pkg"
# UNIX socket paths are limited to 107 bytes; keep it short.
SOCKET="${SA_TEST_SERVER_SOCKET:-/tmp/sequel-ace-test-$(id -u).sock}"
PASSWORD="root"

log() { printf '%s\n' "$*" >&2; }

find_binaries() {
    if command -v mariadbd >/dev/null 2>&1 && command -v mariadb >/dev/null 2>&1; then
        MARIADBD="$(command -v mariadbd)"
        MARIADB="$(command -v mariadb)"
        INSTALL_DB="$(command -v mariadb-install-db)"
        BASEDIR="$(dirname "$(dirname "$MARIADBD")")"
        return
    fi
    if [ -x "$PKG_DIR/usr/bin/mariadbd" ]; then
        MARIADBD="$PKG_DIR/usr/bin/mariadbd"
        MARIADB="$PKG_DIR/usr/bin/mariadb"
        INSTALL_DB="$PKG_DIR/usr/bin/mariadb-install-db"
        BASEDIR="$PKG_DIR/usr"
        return
    fi
    if command -v pacman >/dev/null 2>&1; then
        log "mariadbd not found; downloading the Arch packages into $PKG_DIR (nothing is installed)"
        mkdir -p "$PKG_DIR"
        for pkg in mariadb mariadb-clients; do
            url="$(pacman -Sp "$pkg" | tail -1)"
            curl -fsSL -o "$PKG_DIR/$pkg.pkg.tar.zst" "$url"
            tar --use-compress-program=unzstd -xf "$PKG_DIR/$pkg.pkg.tar.zst" -C "$PKG_DIR"
        done
        find_binaries
        return
    fi
    log "Install mariadb (server and client) or set PATH so mariadbd and mariadb are found."
    exit 1
}

start() {
    find_binaries
    mkdir -p "$RUN_DIR"
    if status >/dev/null 2>&1; then
        log "already running on 127.0.0.1:$PORT"
        return
    fi
    if [ ! -d "$DATA_DIR/mysql" ]; then
        log "initialising data directory $DATA_DIR"
        mkdir -p "$DATA_DIR"
        "$INSTALL_DB" --basedir="$BASEDIR" --datadir="$DATA_DIR" --auth-root-authentication-method=normal --skip-test-db --user="$(id -un)" >/dev/null
        FRESH=1
    fi
    nohup "$MARIADBD" --no-defaults --basedir="$BASEDIR" --datadir="$DATA_DIR" --lc-messages-dir="$BASEDIR/share/mysql" \
        --port="$PORT" --bind-address=127.0.0.1 --socket="$SOCKET" --pid-file="$RUN_DIR/mariadb.pid" \
        --skip-name-resolve --innodb-buffer-pool-size=64M --log-error="$RUN_DIR/error.log" >"$RUN_DIR/stdout.log" 2>&1 &
    for _ in $(seq 1 50); do
        if "$MARIADB" --socket="$SOCKET" -uroot -e 'SELECT 1' >/dev/null 2>&1 || "$MARIADB" --socket="$SOCKET" -uroot -p"$PASSWORD" -e 'SELECT 1' >/dev/null 2>&1; then
            break
        fi
        sleep 0.2
    done
    # mariadb-install-db creates root@localhost and root@127.0.0.1 without a
    # password; give both the fixed test password (idempotent, runs on every start).
    local client=("$MARIADB" --socket="$SOCKET" -uroot)
    "${client[@]}" -e 'SELECT 1' >/dev/null 2>&1 || client+=(-p"$PASSWORD")
    "${client[@]}" -e "
        CREATE USER IF NOT EXISTS 'root'@'127.0.0.1';
        ALTER USER 'root'@'127.0.0.1' IDENTIFIED BY '$PASSWORD';
        GRANT ALL ON *.* TO 'root'@'127.0.0.1' WITH GRANT OPTION;
        ALTER USER 'root'@'localhost' IDENTIFIED BY '$PASSWORD';
        FLUSH PRIVILEGES;"
    status
}

stop() {
    if [ -f "$RUN_DIR/mariadb.pid" ] && kill -0 "$(cat "$RUN_DIR/mariadb.pid")" 2>/dev/null; then
        kill "$(cat "$RUN_DIR/mariadb.pid")"
        log "stopped"
    else
        log "not running"
    fi
}

status() {
    if [ -f "$RUN_DIR/mariadb.pid" ] && kill -0 "$(cat "$RUN_DIR/mariadb.pid")" 2>/dev/null; then
        log "running on 127.0.0.1:$PORT (socket $SOCKET, pid $(cat "$RUN_DIR/mariadb.pid"))"
        return 0
    fi
    log "not running"
    return 1
}

seed() {
    find_binaries
    "$MARIADB" --socket="$SOCKET" -uroot -p"$PASSWORD" < "$HERE/../tests/fixtures/seed.sql"
    log "seeded sequel_ace_test"
}

env_vars() {
    cat <<EOV
export SA_TEST_MYSQL=1
export SA_TEST_MYSQL_HOST=127.0.0.1
export SA_TEST_MYSQL_PORT=$PORT
export SA_TEST_MYSQL_USER=root
export SA_TEST_MYSQL_PASSWORD=$PASSWORD
export SA_TEST_MYSQL_DATABASE=sequel_ace_test
export SA_TEST_MYSQL_TABLE=customers
EOV
}

case "${1:-}" in
    start) start ;;
    stop) stop ;;
    status) status ;;
    seed) seed ;;
    env) env_vars ;;
    *) log "usage: $0 {start|stop|status|seed|env}"; exit 2 ;;
esac
