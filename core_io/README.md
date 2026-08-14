# Pre-3.0 holding directory — one core+I/O project awaiting its replacements

**This is not a tier.** The corpus is organised by level now (see
[`examples/README.md`](../README.md)). The three projects that used to live beside this one moved
to [`05-services/`](../05-services/), keeping their own READMEs:

| Was | Is |
|---|---|
| `core_io/chat_tcp/` | [`05-services/01-tcp-chat/`](../05-services/01-tcp-chat/README.md) |
| `core_io/message_broker/` | [`05-services/02-pubsub-broker/`](../05-services/02-pubsub-broker/README.md) |
| `core_io/file_processor/` | [`05-services/03-file-pipeline/`](../05-services/03-file-pipeline/README.md) |

`file_monitor/` did not move, because the architecture does not move it — it **retires** it into
three different homes, and a retirement only lands with its replacements:

* `watcher.{h,cpp}` → `02-io/08-timeouts-and-watchers`, where "`ev::stat` polls one path and
  cannot name the changed file" is the *lesson* rather than the disappointment.
* `events.h`'s relocation comment → `01-actors/03-event-payloads`, which is to be written around it.
* `processor.{h,cpp}` → `05-services/03-file-pipeline`, which is where a processor that currently
  receives zero events belongs.

It keeps its pre-3.0 hand-written target name on purpose: a derived name would put it in a tier,
and it is not in one. It is Unix-only, as it always was — POSIX `stat()`/`mmap()` with no Windows
equivalent here — so it is not created on Windows rather than failing to compile.

```bash
cmake --preset release
cmake --build --preset release --target file_monitor
./build/presets/release/examples/core_io/file_monitor/file_monitor
```

See [`file_monitor/README.md`](./file_monitor/README.md) for what it does.
