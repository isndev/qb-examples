# `02-io/resources`

Assets staged next to the tier-02 binaries by `qb_stage_example_resources()`, so an example can
open `"resources/ssl/cert.pem"` with no path plumbing and no baked absolute path.

## `ssl/`

`cert.pem` / `key.pem` — a **self-signed development certificate**, byte-identical to the pair in
`06-modules/http/resources/ssl/`. It is deliberately duplicated rather than shared: a tier reaching
across the tree into another tier's assets is a dependency the directory listing does not show, and
`qb_stage_example_resources` stages one directory per example directory.

    subject  CN=localhost, O=QB Framework
    SANs     DNS:localhost, IP:127.0.0.1, IP:::1
    extensions  CA:TRUE (so it can act as its own trust anchor)
    validity 2026-06-24 .. 2046-06-19

`02-io/07-tls.cpp` uses it twice over: as the server's identity, and — because it is self-signed
with `CA:TRUE` — as the CA a client passes to `ssl::Context::client().trust(...)`. That is what
lets the example demonstrate REAL peer verification on a loopback connection instead of switching
verification off, which is the one thing a TLS example must not teach.

**Not for anything but a demo.** The private key is in this repository. Never point a service at it.
