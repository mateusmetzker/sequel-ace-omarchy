# Sequel Ace for Omarchy

A native Linux port of [Sequel Ace](https://sequel-ace.com), the MySQL and
MariaDB database management application for macOS.

The original is written in Objective-C, Swift and SwiftUI on top of AppKit,
which has no Linux implementation. This repository re-implements the
application in C++20 on **Qt 6 Widgets** and **libmariadb**, keeping the
original's behaviour, workflows and file formats. The platform-neutral pieces
of the original are reused verbatim: the SQL editor lexer
(`SPEditorTokens.l`), the editor colour themes, the content filter
definitions and the `Favorites.plist` format.

It runs on **any Linux desktop**. On [Omarchy](https://omarchy.org) it also
follows the desktop theme: the window colours and the SQL syntax colours are
read from the active Omarchy theme and change live when you switch themes.

## Omarchy theme integration

Sequel Ace paints itself in the colours of the active Omarchy theme, read from
`~/.local/state/omarchy/current/theme/colors.toml` — the file Omarchy
repoints on every `omarchy-theme-set`. Window and panel backgrounds, buttons,
selection, scrollbars, tabs and the SQL editor's syntax colours all derive
from it, and switching themes updates the running application immediately, no
restart needed.

On a non-Omarchy desktop it falls back to a built-in dark palette; everything
else works the same.

* `SA_OMARCHY_THEME_DIR=/usr/share/omarchy/themes/tokyo-night sequel-ace`
  previews another theme without changing the desktop's own.
* The SQL editor's colour scheme preference defaults to **System (Omarchy)**.
  Pick one of the bundled macOS themes (Dark Ace, Monokai, Nord, Nemo, Blue
  Dark, Pixely Night, The Pixel) in Preferences to keep it fixed regardless of
  the desktop theme.

## Building

Requirements: CMake 3.21+, a C++20 compiler, Qt 6.4+ (Core, Gui, Widgets,
Network, Concurrent, Svg, Test), libmariadb (MariaDB Connector/C), and
optionally libsecret (keyring passwords) and flex (regenerating the lexer).

```sh
# Arch / Omarchy: sudo pacman -S cmake qt6-base qt6-svg mariadb-libs libsecret flex
# Debian/Ubuntu:  sudo apt install cmake qt6-base-dev libqt6svg6-dev libmariadb-dev libsecret-1-dev flex
# Fedora:         sudo dnf install cmake qt6-qtbase-devel qt6-qtsvg-devel mariadb-connector-c-devel libsecret-devel flex

git clone https://github.com/mateusmetzker/sequel-ace-omarchy.git
cd sequel-ace-omarchy
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/sequel-ace
```

`./build.sh` runs the two CMake commands above.

## Installing

### Arch Linux / Omarchy

```sh
scripts/make-package.sh            # writes dist/sequel-ace-<version>-1-x86_64.pkg.tar.zst
scripts/make-package.sh --install  # same, then sudo pacman -U
```

The script tars the working copy (uncommitted changes included), runs
`makepkg` with `packaging/arch/PKGBUILD` and names the package
`<version>.r<commit count>.g<hash>`, so every rebuild upgrades the previous
install. Dependencies: `qt6-base`, `qt6-svg`, `mariadb-libs`, `libsecret`,
`hicolor-icon-theme`; `openssh` is optional (SSH tunnels) and
`gnome-keyring` optional (persistent passwords). Remove it with
`sudo pacman -R sequel-ace`.

### Any other distribution

```sh
sudo cmake --install build          # or: DESTDIR=/tmp/pkg cmake --install build
```

This installs the binary, the desktop entry, the AppStream metadata and the
icons under `CMAKE_INSTALL_PREFIX` (`/usr/local` by default).

## What works

| Area | State |
| --- | --- |
| Connections: TCP/IP, UNIX socket, SSH tunnel (system OpenSSH; password, key or agent), SSL | working |
| Favorites: groups, drag and drop, colours, import/export, compatible with the macOS `Favorites.plist` | working |
| Passwords in the desktop keyring (libsecret / Secret Service) | working |
| Tables list with filter; add, rename, duplicate, truncate, delete; maintenance commands | working |
| Table content: paging, sorting, multi-rule AND/OR filters (every Sequel Ace operator, custom `WHERE`, filter by cell value), in-place editing, add/duplicate/delete rows, NULL handling, field editor with hex and image view | working |
| Table structure: column editor with `ALTER TABLE` generation, indexes | working |
| Relations (foreign keys), triggers, table info with editable options and `CREATE` syntax | working |
| Query editor: syntax highlighting, current-query highlight, completion (keywords, functions, tables, columns), run current/selection/all, explain current query, history, query favorites, error continuation prompts, destructive-SQL confirmation gate | working |
| Console window, server variables, process list with kill | working |
| Export: CSV per table, SQL dump (structure and/or content), and the same two formats for the current query result or the filtered table content | working |
| User Manager: accounts, global and per-schema privileges, resource limits | working |
| Local MCP server: 19 tools over the open connections, read-only by default behind a quote- and comment-aware SQL guard | working |
| Omarchy theme sync, live on theme switch | working |
| `.spf` connection files and `.sql` scripts on the command line | basic |

### Not ported (yet)

* Import: CSV and SQL.
* Export to XML, HTML, PDF and Dot (CSV and SQL dumps are available).
* Bundles and user-defined actions.
* Printing.
* AWS IAM and HashiCorp Vault authentication.
* Localisation — the interface is English only.
* Flatpak and AppImage packaging (an Arch package is provided).

### Known gaps inside the ported areas

* The User Manager always changes MariaDB passwords through the legacy
  `SET PASSWORD` path; `ALTER USER ... IDENTIFIED VIA` is not implemented.
  Only MySQL 5.7.6 and later take the modern `ALTER USER` route.
* Query history is one global JSON file, not the per-favorite SQLite store the
  macOS app keeps.
* The content view holds one page in memory, so a filtered export re-runs its
  `SELECT` in batches instead of dumping the loaded page. Choose "Only the
  rows currently loaded" in the export dialog for the latter.

## MCP server

Preferences → **MCP Server** exposes the open connections to an AI assistant
over a loopback-bound HTTP server (default port 8765) that speaks both MCP
transports: Streamable HTTP (`POST /mcp`) and the legacy SSE transport
(`GET /sse` + `POST /message`). It is **read-only by default**: writes are
rejected by a SQL guard that parses quotes and comments before classifying a
statement, so `SELECT /* UPDATE */ 1` passes and a disguised write does not.
Turn the guard off explicitly if you want an assistant to be able to write.

## Files and settings

| Purpose | Location |
| --- | --- |
| Favorites | `~/.local/share/sequel-ace/Favorites.plist` (same format as macOS — copy the file from `~/Library/Application Support/Sequel Ace/Data/` to migrate) |
| Preferences | `~/.config/sequel-ace/sequel-ace.conf` |
| Query history | `~/.local/share/sequel-ace/QueryHistory.json` |
| Passwords | desktop keyring, schema `com.sequel-ace.SequelAce.Password` |

SSH tunnels use the `ssh` binary found in `PATH` (or the one set in
Preferences); prompts for passwords, passphrases and host keys are answered by
the application itself through `SSH_ASKPASS`.

## Tests

```sh
cd build
ctest --output-on-failure                       # unit tests only

SA_TEST_MYSQL=1 SA_TEST_MYSQL_HOST=127.0.0.1 SA_TEST_MYSQL_PORT=3306 \
SA_TEST_MYSQL_USER=root SA_TEST_MYSQL_PASSWORD=secret ctest --output-on-failure
```

With `SA_TEST_MYSQL=1` the integration tests (`tst_integration`) and the
widget tests (`tst_ui`) run against the given server, creating and dropping
their own databases. `scripts/test-server.sh start && scripts/test-server.sh seed`
brings up a throw-away MariaDB on port 33306 (root/root) for this, without
root privileges, and `eval "$(scripts/test-server.sh env)"` exports the
variables.

`sequel-ace --smoke-test <dir>` connects with the same variables and writes a
screenshot of every view to `<dir>`, useful with `QT_QPA_PLATFORM=offscreen`.

## Layout

```
├── CMakeLists.txt
├── src/core/        connection, session, schema queries, favorites, prefs (no Qt Widgets)
├── src/ui/          Qt Widgets application
├── src/lexers/      SPEditorTokens.l from the macOS app (+ generated C)
├── resources/       themes, ContentFilters.plist, CompletionTokens.json, icons
├── packaging/       desktop entry, AppStream metadata, icons, arch/PKGBUILD
├── scripts/         test-server.sh (unprivileged MariaDB), make-package.sh (Arch package)
└── tests/           tst_core, tst_integration, tst_ui, fixtures/seed.sql
```

## Licence and credits

MIT, the same licence as the upstream project — see [LICENSE](LICENSE).
Sequel Ace is © Sequel Ace and Moballo, LLC; this is an unofficial Linux port
and is not affiliated with the upstream project.
