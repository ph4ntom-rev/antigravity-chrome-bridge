# Local protocol benchmark

Run `python benchmarks/latency.py /path/to/binary --samples 200` after a Release build. The script measures process spawn to first MCP ping, then ten warmup pings and 200 sequential stdio pings. `--reference-binary` can measure another trusted compatible binary using the same method. Browser execution, network activity, model inference and memory consumption are outside this measurement.

One local run on Windows 10 build 19045, Python 3.10.6, GCC 16.1 MinGW, Release:

| Measurement | Result |
| --- | ---: |
| Binary size | 1,995,776 bytes |
| Spawn to first ping | 14.0129 ms |
| Warm ping p50 | 0.0484 ms |
| Warm ping p95 | 0.0580 ms |
| Warm ping p99 | 0.0624 ms |

These are a single machine's observations, not performance guarantees or a comparison with other implementations. The measured build preceded the final batch-validation change; rerun for the exact binary being evaluated. Earlier unsourced startup/memory/comparison claims have been removed.
