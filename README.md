# my_redis_server (C port)

A small, dependency-free clone of a Redis server, written in C11. This
is a straight port of an original C++17 implementation, translated
line-for-line in structure but rebuilt on plain C data structures
(no STL), with a handful of bug fixes and robustness improvements
along the way.

It speaks a subset of the real RESP (REdis Serialization Protocol),
so it's compatible with `redis-cli` and most Redis client libraries
for the commands it implements.

## Features

- **Key/value strings**: `SET`, `GET`, `DEL`/`UNLINK`, `TYPE`, `KEYS`,
  `RENAME`, `EXPIRE`
- **Lists**: `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LGET`, `LLEN`, `LREM`,
  `LINDEX`, `LSET`
- **Hashes**: `HSET`, `HGET`, `HDEL`, `HEXISTS`, `HGETALL`, `HKEYS`,
  `HVALS`, `HLEN`, `HMSET`
- **Other**: `PING`, `ECHO`, `FLUSHALL`
- Per-key expiry (`EXPIRE`), lazily purged on access
- One thread per client connection (POSIX threads)
- Automatic persistence: dumps the dataset to `dump.my_rdb` every 5
  minutes, on clean shutdown (`Ctrl+C`), and loads it back on startup
- Thread-safe: a single mutex guards the whole dataset (simple, and
  fine for this project's scope — see [Concurrency](#concurrency))

## Building

Requires `gcc` and `make` (POSIX threads are used, no other external
dependencies).

```
make          # build ./my_redis_server
make run      # build and run on the default port (6379)
make rebuild  # clean + build
make clean    # remove build/ and the binary
```

## Running

```
./my_redis_server [port]
```

`port` defaults to `6379` if omitted. On startup the server tries to
load `dump.my_rdb` from the current directory; if it's not there (or
fails to parse), it just starts empty.

Talk to it with `redis-cli`:

```
redis-cli -p 6379 SET name "hello world"
redis-cli -p 6379 GET name
redis-cli -p 6379 RPUSH mylist a b c
redis-cli -p 6379 LGET mylist
```

(`LGET` is this project's own command for "give me the whole list" —
it's not part of real Redis, which uses `LRANGE`.)

Stop the server with `Ctrl+C`; it dumps the dataset to `dump.my_rdb`
before exiting.

## Project layout

```
include/
  hashtable.h              generic string-keyed hash table
  strlist.h                growable array of strings (for LIST values)
  strbuf.h                 growable buffer for building RESP responses
  redis_database.h         the in-memory data store + persistence
  redis_command_handler.h  RESP parsing + command dispatch
  redis_server.h           TCP accept loop / per-connection threads
src/
  (matching .c files, plus main.c)
Makefile
```

Three small utility modules (`hashtable`, `strlist`, `strbuf`) exist
because C has no `std::unordered_map`, `std::vector<std::string>`, or
`std::ostringstream` — they're direct, minimal stand-ins for those,
used throughout the rest of the codebase. Everything else mirrors the
original C++ file-for-file: `RedisDatabase` → `redis_database.*`,
`RedisCommandHandler` → `redis_command_handler.*`,
`RedisServer` → `redis_server.*`.

## Design notes

- **Ownership**: every `rdb_*` accessor that returns a string (e.g.
  `rdb_get`, `rdb_lget`, `rdb_hgetall`) hands back a freshly
  `strdup`'d copy, never a pointer into internal storage. The caller
  always owns and must `free()` what it gets back. This matters more
  in C than in the original C++, since there's no RAII/destructor to
  make a lifetime bug obvious — returning internal pointers across
  the mutex boundary would be a real use-after-free risk under
  concurrent access.
- **Concurrency**: a single `pthread_mutex_t` inside `RedisDatabase`
  guards all three stores (string/list/hash) plus the expiry map.
  It's coarse-grained — one lock for the whole dataset rather than
  per-key locking — which keeps the implementation simple and correct
  at the cost of some throughput under heavy concurrent load. Good
  enough for a learning project; a sharded lock or per-key locking
  would be the natural next step if this needed to scale.
- **Connection handling**: each accepted client gets its own detached
  `pthread`. Detaching (rather than collecting threads into a vector
  to `join` later, as the original C++ version did) means finished
  connections clean up their own resources immediately instead of
  accumulating until process exit.
- **Persistence format**: a simple length-prefixed text format,
  `<byte-length>:<raw-bytes>` per token, one record per line
  (`K key value`, `L key count item...`, `H key count field value...`).
  This is deliberately more explicit than a naive whitespace-split
  format so that keys/values containing spaces round-trip correctly
  (see [Fixes](#fixes-vs-the-original-c-version) below). It still
  assumes no key or value contains a literal newline byte.

## Fixes vs. the original C++ version

Porting surfaced a few real bugs, fixed here:

1. **`DEL` always returned "not found."** The original computed
   whether anything was erased but then had an unconditional
   `return false;` after it. `rdb_del` now returns the actual result.
2. **Values with spaces corrupted persistence.** The original dump
   format wrote plain space-separated text and read it back with
   `istringstream >>`, which splits on whitespace — so a value like
   `"hello world"` would silently break on reload. The new
   length-prefixed format handles this correctly.
3. **Malformed RESP input could crash a connection thread.**
   `std::stoi` throws on bad input; the C port uses `strtol` with
   explicit error checking everywhere a client-supplied number is
   parsed (RESP lengths, `EXPIRE`, `LREM`, `LINDEX`, `LSET`).
4. **Shutdown log messages could be lost.** The signal handler now
   flushes stdio before exiting, so the "shutting down" / "dumped"
   messages reliably show up before the process ends.

## Known limitations

- Keys/values containing a literal `\n` byte are not supported by the
  persistence format (they'd break the one-record-per-line layout).
- The lock is dataset-wide, not per-key — see
  [Concurrency](#concurrency) above.
- No `AUTH`, `SELECT`/multiple databases, transactions, pub/sub, or
  the rest of the real Redis command set — this implements a small
  teaching-sized subset.
- No RESP3 support; responses are RESP2 only.
