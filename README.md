# mempress

**Controlled memory-pressure generator for OOM and reclaim testing — grow to a target size at a controlled rate, hold, report. Single C binary, no dependencies.**

```
$ mempress --target 512MB --rate 64MB/s --hold 30s --report 5s
progress: 64.0 / 512.0 MiB at 1.0s, RSS 66.3 MiB
progress: 320.0 / 512.0 MiB at 5.0s, RSS 322.4 MiB
final: allocated 512.0 MiB in 8.00s, peak RSS 514.7 MiB
```

Sometimes you need to *be* the memory hog: to watch your supervisor restart
you, to see how long the kernel takes to reclaim, to size a container limit
by dying against it. mempress does exactly one thing — allocate to a target
size at a chosen pace — and reports honestly what the OS thinks it is using.

## Why mempress

- **Honest pressure.** Every chunk is written (`memset`) and read back, so
  pages are actually resident — not just reserved address space.
  `--no-touch` opts out deliberately when you want virtual-only growth.
- **Rate control.** `--rate 64MB/s` paces growth to a wall-clock schedule
  so you can watch reclaim lag in stages; without it, allocation runs flat
  out.
- **Hold and observe.** `--hold 30s` stays at target with periodic RSS
  lines, giving monitors time to react while the footprint is steady.
- **Real numbers.** RSS comes from the OS (mach task info on macOS,
  `/proc/self/statm` on Linux), not from the allocator's bookkeeping.
- **OOM-probe mode.** `--keep` skips the final free so you can watch the
  OOM killer or swap thrash do their thing to a live corpse.
- **Scriptable.** `--json` final report; SIGINT/SIGTERM stop the run
  gracefully (exit 130) with the report still printed.

## Usage

```sh
mempress --target 512MB                        # defaults: full speed, 5s hold
mempress --target 1GB --rate 64MB/s --hold 30s
mempress --target 256MB --hold 5m --report 10s # slow observability run
mempress --target 2GB --no-touch               # address space only
mempress --target 4GB --keep &                 # feed to your OOM killer
mempress --target 64MB --json | jq .peak_rss_bytes
```

Sizes: bytes, `KB`, `MB`, `GB` (case-insensitive). Times: seconds, `10s`,
`2m`, `500ms`.

Exit codes: 0 ok, 1 allocation failure or bad config, 2 usage error,
130 interrupted.

## Development

```sh
make test
```

Ten test groups cover size/time parsing (units, fractions, junk, overflow),
config defaults, a full engine run against an injectable clock, JSON report
shape, interrupt handling, and RSS availability — all with tiny (KiB-scale)
allocations, so the suite never pressures your machine.

## License

MIT — see [LICENSE](LICENSE).
