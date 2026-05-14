# Usage

## Local install

Install the build dependencies:

Debian/Ubuntu:
```
$ sudo apt-get install gcc make git
```

RHEL/Fedora:
```
$ sudo dnf install gcc glibc-devel make git
```

Clone the [rundir](https://github.com/draxinar/rundir) repository next to the source tree:

```
$ git clone https://github.com/draxinar/rundir.git ../.rundir
```

Build ouo:

```
$ make
```

Run ouo:

```
$ ./ouo
```

The server listens on port 2593 by default. Run `./ouo -help` for a list of options.

Connect with an Ultima Online client (versions 1.25.30 to 5.0.9.1) to port 2593.

## Server install

Create a dedicated system user and directory tree:

```
$ sudo useradd -r -s /usr/sbin/nologin -d /opt/ouo ouo
$ sudo mkdir -p /opt/ouo/run
$ sudo chown ouo:ouo /opt/ouo /opt/ouo/run
```

Clone, build, and install:

```
$ sudo -u ouo git clone https://github.com/draxinar/ouo.git /opt/ouo/src
$ sudo -u ouo git clone https://github.com/draxinar/rundir.git /opt/ouo/.rundir
$ cd /opt/ouo/src && sudo -u ouo make
$ sudo make install
$ sudo chown -R ouo:ouo /opt/ouo
```

This installs the binary to `/opt/ouo/run/`, systemd units, helper scripts, and a default environment file (`/etc/default/ouo`).

Enable and start the server:

```
$ sudo systemctl daemon-reload
$ sudo systemctl enable --now ouo.service ouo-backup.timer ouo-status.timer
```

The server auto-restarts on crash and logs a stack trace via the coredump service. The backup timer saves world state hourly. The status timer writes a `status.json` file every minute.

View logs:

```
$ journalctl -u ouo -f
```

## Privilegied access

Accounts are stored in `../.rundir/access.list`, one line per account:

```
login salt_hex:hash_hex accountNum flags plevel
```

Accounts auto-create on first login, so to promote a user:

1. Have them log in once with the client to create the entry.
2. Edit the account's last two columns (both decimal).

   `flags` is a bitmask - add values to combine (e.g. `3` = banned + godmode):

| Value | Name         | Effect                 |
|-------|--------------|------------------------|
| 0     | -            | No flags               |
| 1     | ACCT_BANNED  | Login rejected         |
| 2     | ACCT_GODMODE | Sets `PlayerIsEditing` |

`plevel` is a single privilege level:

| Value | Role        | Effect                                          |
|-------|-------------|-------------------------------------------------|
| 0     | Player      | Default                                         |
| 1     | Counselor   | Sets `PlayerIsCounselor`                        |
| 2     | Game Master | Sets `PlayerIsCounselor` + `PlayerIsGameMaster` |

3. Reload the server without dropping players:

```
$ sudo kill -HUP $(pgrep ouo)
```

or restart it.

`plevel` and `flags` are applied in `Player_Login` and cleared on logout, so they affect every character on the account on every login. Already-connected users must disconnect and reconnect for a change to take effect.

The `-gm` server flag auto-applies GameMaster + GodMode to every player at login, overriding access.list. Useful for local testing; do not use in production.

The `-test` server flag enables Test Center mode. Every player at login gets the `PlayerIsTestCenter` permission tier, which unlocks a small `.command` set (`.set`, `.set list`, `.where`, `.help`, `.resurrect`) for self-administration without granting full GM editing. New characters created while the server runs with `-test` receive a starter kit at creation time: 10000 gold in the bank box, plus a spellbook filled with all 64 spells and a bag containing 100 of each of the 8 standard reagents in the backpack. For full GM powers, use `-gm` instead; the two flags can be combined.
