"""Reproducible stdio measurements; excludes browser and model latency."""
import argparse
import json
from pathlib import Path
import platform
import statistics
import subprocess
import time


def measure(binary, samples):
    start = time.perf_counter()
    process = subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    try:
        def ping(identifier):
            process.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': identifier, 'method': 'ping'}) + '\n')
            process.stdin.flush()
            result = json.loads(process.stdout.readline())
            if result.get('id') != identifier or 'error' in result:
                raise RuntimeError('Invalid ping response')
        ping(0)
        startup = (time.perf_counter() - start) * 1000
        for i in range(10):
            ping(i + 1)
        timings = []
        for i in range(samples):
            start = time.perf_counter()
            ping(i + 11)
            timings.append((time.perf_counter() - start) * 1000)
        ordered = sorted(timings)
        return {'binary': str(binary), 'binary_bytes': binary.stat().st_size,
            'spawn_to_first_ping_ms': startup, 'warmup_pings': 10, 'samples': samples,
            'ping_p50_ms': statistics.median(timings), 'ping_p95_ms': ordered[int((samples - 1) * .95)],
            'ping_p99_ms': ordered[int((samples - 1) * .99)]}
    finally:
        process.stdin.close()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        process.stdout.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--samples', type=int, default=200)
    parser.add_argument('--reference-binary', type=Path, help='Optional trusted build for same-machine comparison')
    args = parser.parse_args()
    if not 20 <= args.samples <= 100000:
        parser.error('samples must be between 20 and 100000')
    results = [measure(args.binary.resolve(), args.samples)]
    if args.reference_binary:
        results.append(measure(args.reference_binary.resolve(), args.samples))
    print(json.dumps({'platform': platform.platform(), 'python': platform.python_version(),
        'scope': 'Local process startup and warmed stdio ping. No browser, model or network latency.',
        'results': results}, indent=2))
