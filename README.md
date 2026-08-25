# CustomRedis

A small, dependency-free clone of a Redis server, written in C11.

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
- A hand-rolled hash table where each bucket holds a small binary
  search tree instead of a plain chain, keeping worst-case lookups
  at O(log n) instead of degrading to O(n) under heavy collisions

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

This is a network server, not an interactive shell — it only reads
commands that arrive over its TCP socket, never from its own stdin.
Talk to it from a second terminal with `redis-cli`:

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
  hashtable.h              generic string-keyed hash table (BST buckets)
  strlist.h                growable array of strings (for LIST values)
  strbuf.h                 growable buffer for building RESP responses
  redis_database.h         the in-memory data store + persistence
  redis_command_handler.h  RESP parsing + command dispatch
  redis_server.h           TCP accept loop / per-connection threads
src/
  (matching .c files, plus main.c)
Makefile
```

Three small utility modules (`hashtable`, `strlist`, `strbuf`) sit
underneath everything else: a generic hash table, a growable string
array (used for LIST values), and a growable buffer for building RESP
responses. Everything else is organized around the three natural
pieces of a server like this: `redis_database.*` (the data store
itself), `redis_command_handler.*` (parsing client input and
dispatching to the right command), and `redis_server.*` (the TCP
accept loop and per-connection threads).

## Design notes

- **Hash table**: each bucket holds a small binary search tree
  (ordered by `strcmp` on the key) instead of a plain linked list.
  A bucket with several colliding keys still resolves lookups in
  O(log n) instead of degrading to an O(n) chain — the same reasoning
  that pushed languages like Python and PHP toward collision-resistant
  hashing after "hash flooding" attacks (crafting many keys that
  collide into one bucket to force O(n) behavior) became a known DoS
  class around 2011. It's not a self-balancing tree (no AVL/red-black
  rotations), so a bucket can still degrade toward a list-like shape
  if colliding keys happen to be inserted in sorted string order — but
  an attacker now needs both a bucket collision *and* a specific
  insertion order to cause it, a meaningfully higher bar than plain
  chaining. The table also grows automatically as load factor rises.
- **Ownership**: every `rdb_*` accessor that returns a string (e.g.
  `rdb_get`, `rdb_lget`, `rdb_hgetall`) hands back a freshly
  `strdup`'d copy, never a pointer into internal storage. The caller
  always owns and must `free()` what it gets back — returning internal
  pointers across the mutex boundary would be a real use-after-free
  risk under concurrent access.
- **Concurrency**: a single `pthread_mutex_t` inside `RedisDatabase`
  guards all three stores (string/list/hash) plus the expiry map.
  It's coarse-grained — one lock for the whole dataset rather than
  per-key locking — which keeps the implementation simple and correct
  at the cost of some throughput under heavy concurrent load. Good
  enough for a learning project; a sharded lock or per-key locking
  would be the natural next step if this needed to scale.
- **Connection handling**: each accepted client gets its own detached
  `pthread`. Detaching (rather than collecting threads into a vector
  to join later) means finished connections clean up their own
  resources immediately instead of accumulating until process exit.
- **RESP parsing**: client-supplied lengths and counts are parsed with
  `strtol` plus explicit error checking, so a malformed or adversarial
  RESP frame can't crash a connection thread.
- **Persistence format**: a simple length-prefixed text format,
  `<byte-length>:<raw-bytes>` per token, one record per line
  (`K key value`, `L key count item...`, `H key count field value...`).
  Being explicit about lengths (rather than splitting on whitespace)
  means keys and values containing spaces round-trip correctly. It
  still assumes no key or value contains a literal newline byte.

## Known limitations

- Keys/values containing a literal `\n` byte are not supported by the
  persistence format (they'd break the one-record-per-line layout).
- The lock is dataset-wide, not per-key — see
  [Concurrency](#concurrency) above.
- No `AUTH`, `SELECT`/multiple databases, transactions, pub/sub, or
  the rest of the real Redis command set — this implements a small
  teaching-sized subset.
- No RESP3 support; responses are RESP2 only.
