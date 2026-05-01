[![CI](https://github.com/draxinar/ouo/actions/workflows/ci.yml/badge.svg)](https://github.com/draxinar/ouo/actions/workflows/ci.yml)

# OUO

This repository is a reverse engineering of the Ultima Online server present in the [Ultima Online Demo](https://github.com/draxinar/uodemo), distributed as part of the first release of `Ultima Online: The Second Age`. The server data is available in a separate repository: [rundir](https://github.com/draxinar/rundir).

The goal is to be able to build and run the Ultima Online server as closely as possible to the original server as it ran around June 1998. For convenience, we support a slightly larger set of Ultima Online client versions than the original one, so you can connect with the initial release of `Ultima Online: The Second Age` (client 1.25.35).

## Era

Ultima Online: The Second Age was released on October 29, 1998.
UoDemo.exe is dated 1998-09-02 21:27:44 UTC.
The `update.txt` file from `UoDemo.dat` suggests the server data were extracted from the production server on June 2nd, 1998.

## Test Center

A Test Center is available at https://uo.serpent-isle.com/.

## Usage

See [USAGE.md](USAGE.md) for build and installation instructions.

## Technical constraints

UoDemo.exe was compiled with Microsoft Visual C++ 5.0 (Visual Studio 97), targeting a pre-C++98 dialect of C++.

This server is a full re-implementation in standard C99.

While the original Ultima Online server was running on Solaris, the Ultima Online Demo contained a Windows port. We ported it back to a POSIX operating system. So far, it has been tested mainly on Linux, but other POSIX operating systems should be supported as well.

The original binary was 32-bit; this server builds in both 32-bit and 64-bit.

## Client versions

Between the build of the Ultima Online Demo (June 1998) and the release of the first `Ultima Online: The Second Age` client (October 1998, version 1.25.35), there were protocol changes, which made the client incompatible with the server provided as part of the demo. Some parts of the protocol were also specifically removed for the demo.
The server was first modified to allow users to connect using client 1.25.35, then progressively extended to support clients up to 5.0.9.1.

This server has been tested with Ultima Online clients 1.25.30 to 5.0.9.1 with and without encryption enabled.

We recommend using clients from the Renaissance era, which seems to be a good compromise between stability and era accuracy.

Client 2.0.7 could be a good candidate, since it's the last client before the introduction of the `map1.mul` support (Ilshenar map from the Third Dawn expansion).

## Goals

The reverse engineering of the Ultima Online server was done with different goals in mind:

- Time capsule: play Ultima Online the way it was in June 1998.
- Education: learn how an MMORPG server was designed in 1998.
- Portability: port the server to Linux and other operating systems.
- Bug fixes: easily fix bugs and improve the code.

## Custom systems

The demo binary shipped with many subsystems stubbed out or disconnected. Custom additions restore missing functionality:

- Account management
- Multi-client support
- Watchdog
- Periodic world saves
- Auto-initial spawn
- NPC ecology: predator/prey/scavenger AI where creatures hunt, flee, and form packs
- Light tables
- Light source toggle
- Ground item and multi decay
- Ghost movements: auto-unfreeze and walk-to-healer flow for clients 1.26.4b+ that removed the death gump
- Keyword teleporters
- House commands
- GM commands
- New skills: Meditation (Feb 2, 1999), Stealth (Feb 24, 1999), Remove Trap (Feb 24, 1999)
- Stability fixes: crashes, buffer overflows, uninitialized variables
- Gameplay fixes: skill gain, fame/notoriety direction, spawn density

Some custom systems are gated by runtime feature flags. Use `-features none` for pure binary-match behavior, or `-features skill_lock,ecology,...` for a subset.

## Contributing

Contributions are very welcome.

You can use any disassembler, like [radare2](https://rada.re/) or [Ghidra](https://ghidralite.com/) to disassemble a function and rewrite it in C, as closely as possible to the original.

Always note the address (from `UoDemo.exe`) in a comment preceding the function, so we always know where to look in the binary.

Once you're ready, you can send a [Pull Request](https://github.com/draxinar/ouo/pulls).

## Thanks

Ultima Online was created by Origin Systems, Inc. and none of this would have been possible without the tremendous work of the development team.

This server has been inspired by the work done by Batlin and Derrick on the [UO:98](https://uo98.org/) project in 2011. This server pushes the UO:98 ideas forward by recreating the whole original server source tree through reverse engineering of UoDemo.exe.

[Norman Lancaster](https://www.servuo.com/threads/client-1-25-35b-t2a-retail-release-detailed-information.4544/) documented [UO: T2A client](https://sites.google.com/site/ultimaonlineoldpackets), including [compression](https://sites.google.com/site/ultimaonlineoldpackets/client/compression) and [encryption](https://sites.google.com/site/ultimaonlineoldpackets/client/encryption).
