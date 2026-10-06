# Example: Sharded Accept (`qb-example-services-sharded-accept`)

The fifth tier-5 project: one listener per core on ONE port, and the kernel deciding which core a new
connection lands on. Each core accepts, reads and answers its own connections -- no acceptor hands
sockets to a pool, no single accepting core to saturate. It is `01-tcp-chat`'s layout with the
hand-off removed.

Prerequisite: `05-services/01-tcp-chat` (the acceptor / pool / session layout this one does without).

## The option, and what each system does with it

`qb::io::tcp::listen_options{.reuse_port = true}` (3.3, Huly QB-78) sets `SO_REUSEPORT` between the
socket's creation and its bind:

| System | What happens |
|---|---|
| Linux | every listener that asks shares the port, and the kernel **balances** new connections across them |
| macOS, the BSDs | the port is shared, the accept is **not** balanced (FreeBSD's balancing variant is `SO_REUSEPORT_LB`) |
| Windows | no such option: a listen that asks for it **fails** with `ENOPROTOOPT`; this program then serves from core 0 |

A listener that did not ask is refused the port, so two unrelated servers never share one by accident.

## Layout

| File | Role |
|---|---|
| `main.cpp` | `Shard` (one per core: a listener on the shared port and the sessions it accepted), `ShardSession` (answers which core served it), `PortChosen` (core 0 broadcasts the port it bound), and `main()` -- 64 blocking clients that ask "which core?" and a tally |

## What the run proves

The distribution is measured, not assumed: every connection's answer names the core that accepted
it. On Linux the program fails unless at least two of the four cores served (all 64 on one listener
has a probability of 4 * 4^-64 under the kernel's hash); elsewhere it reports what the system did.

## Run

```sh
cd build/presets/release/examples/05-services && ./qb-example-services-sharded-accept
```
