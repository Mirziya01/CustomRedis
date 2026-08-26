# my_redis_cli

A `redis-cli`-style interactive client written in C. Connects to any RESP2-speaking
Redis server over TCP, sends commands, and parses the replies — simple strings,
errors, integers, bulk strings, and arrays — including pub/sub (`SUBSCRIBE`).

## Features

- **Interactive REPL** with readline-backed line editing and history (arrow keys,
  `Ctrl+R` search, etc.)
- **One-shot mode**: `./my_redis_cli SET foo bar` runs a single command and exits
- **Full RESP2 parsing**: simple strings (`+`), errors (`-`), integers (`:`), bulk
  strings (`$`, including nil), and arrays (`*`, including nested arrays)
- **Pub/Sub mode**: `SUBSCRIBE channel` drops into a live message loop until you
  type `exit`/`quit`, at which point it sends `UNSUBSCRIBE` and returns to the REPL
- **IPv4/IPv6 agnostic** connection setup via `getaddrinfo`
- Quoted-argument support, so `SET mykey "hello world"` is one value, not three
  tokens

## Building

Requires `gcc` and `libreadline`.

```bash
sudo apt install libreadline-dev   # if not already installed
make
```

This produces a `my_redis_cli` binary. `make clean` removes build artifacts.

## Usage

```
./my_redis_cli                        # REPL, connects to 127.0.0.1:6379
./my_redis_cli -h <host> -p <port>     # REPL against a specific host/port
./my_redis_cli -p 6380                 # REPL, custom port, default host
./my_redis_cli PING                    # one-shot command, then exit
./my_redis_cli SET mykey "Hello World" # one-shot with a quoted value
```

Inside the REPL:

```
127.0.0.1:6379> SET mykey "Hello World"
OK
127.0.0.1:6379> GET mykey
Hello World
127.0.0.1:6379> SUBSCRIBE updates
(Subscribed) Type 'exit'/'quit' to quit subscription mode.
...
exit
(Exited subscription mode)
127.0.0.1:6379> help
...
127.0.0.1:6379> quit
Goodbye.
```

## Architecture

The codebase is split by responsibility, each with a matching header:

| Module | Responsibility |
|---|---|
| `redis_client.{h,c}` | Opens the TCP socket (`getaddrinfo` → `connect`), sends raw command bytes with short-write handling, and tears the connection down. |
| `command_handler.{h,c}` | Tokenizes a REPL input line (quote-aware) and encodes a token list into a RESP command (`*N\r\n$len\r\narg\r\n...`). |
| `response_parser.{h,c}` | Reads one RESP reply off the socket and returns a human-readable, malloc'd string. Recurses for arrays. |
| `cli.{h,c}` | Owns the REPL: polls stdin and the socket together (`poll`), drives readline, dispatches commands, and runs the pub/sub sub-loop. |
| `strbuf.{h,c}` | Small growable string buffer used internally wherever the code needs to build up output without knowing the final length up front (RESP encoding, line reads, array formatting). |
| `main.c` | Parses `-h`/`-p` and any trailing one-shot command, then hands off to `cli_run`. |

`~870` lines total. No dynamic dependencies beyond libc and `libreadline` — no
JSON, no external Redis client library, just sockets and RESP.

### Design notes

- **Ownership is explicit.** Every function that returns a `malloc`'d string or
  array says so in its header comment, and every caller frees it. There's no
  hidden allocation.
- **`poll()` drives both stdin and the socket** in the same loop, so a message
  arriving from the server (relevant during `SUBSCRIBE`) and a keystroke from the
  user are handled without one blocking the other.
- **Error paths return strings, not exceptions.** A malformed or truncated RESP
  reply produces a `"(Error) ..."` string rather than crashing — the REPL keeps
  running.
- **Quoted tokens are honored** in the input splitter, including empty quotes
  (`""`), so multi-word values round-trip correctly through `SET`/`GET`.

## Known limitations

- RESP2 only — no `HELLO`/RESP3 support (no maps, sets, doubles, push types as
  distinct reply kinds).
- No TLS, no `AUTH`/password prompt, no config file (`~/.myredisclirc` is
  mentioned in `help` output but not yet read).
- Not binary-safe end-to-end: bulk strings containing embedded `\0` will print
  truncated at the terminal, since replies are surfaced as C strings.
