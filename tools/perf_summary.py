"""Summarise responder [PERF] UART lines per requester operation.

Usage: py -3.13 perf_summary.py <run dir> [<run dir> ...]
A run dir is produced by task1_spdm_runs.ps1 (timeline.txt + device_uart.log).
For each operation (timeline START marker) it prints: DFU transfers, wire bytes
(requester+responder SPDM/MCTP payload), device busy time, signature / key-exchange /
KEM time, largest assembled SPDM message, and peak stack / heap seen so far.
"""
import re
import sys
from datetime import datetime
from pathlib import Path

FIELDS = ("n", "req", "rq", "rsp", "rs", "large", "t", "sign", "kexg", "kexs", "kem", "cum_t",
          "cum_b", "nsstk", "nsheap", "sstk", "sheap")
PERF = re.compile(r"^(\d\d:\d\d:\d\d\.\d+) \[I\] \[PERF\] (.*)$")


def ts(s):
    return datetime.strptime(s, "%H:%M:%S.%f")


def parse(run: Path):
    marks = []
    for line in (run / "timeline.txt").read_text().splitlines():
        t, _, name = line.split(" ", 2)
        marks.append((ts(t), name))
    perf = []
    for line in (run / "device_uart.log").read_text(errors="replace").splitlines():
        m = PERF.match(line)
        if m:
            toks = m.group(2).split()
            if len(toks) != len(FIELDS) or any("=" not in t for t in toks):
                continue  # partial / interleaved UART line (e.g. capture started mid-line)
            kv = dict(x.split("=", 1) for x in toks)
            if set(kv) != set(FIELDS):
                continue
            perf.append((ts(m.group(1)), kv))
    return marks, perf


def summarise(run: Path):
    marks, perf = parse(run)
    rows = []
    for i, (start, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else datetime.max
        lines = [kv for t, kv in perf if start <= t < end]
        if not lines:
            rows.append((name, 0, 0, 0, 0, 0, 0, 0, 0, "", ""))
            continue
        # cum_* reset with every requester process (n restarts); sum each segment's last value.
        tot_t = tot_b = xfers = 0
        prev = None
        for kv in lines + [None]:
            if prev is not None and (kv is None or int(kv["n"]) < int(prev["n"])):
                tot_t += int(prev["cum_t"]); tot_b += int(prev["cum_b"]); xfers += int(prev["n"])
            prev = kv
        sign = sum(int(kv["sign"]) for kv in lines)
        kex = sum(int(kv["kexg"]) + int(kv["kexs"]) for kv in lines)
        kem = sum(int(kv["kem"]) for kv in lines)
        large = max(int(kv["large"]) for kv in lines)
        heavy = max(lines, key=lambda kv: int(kv["t"]))
        last = lines[-1]
        rows.append((name, xfers, tot_b, tot_t, sign, kex, kem, large, int(heavy["t"]),
                     f'{heavy["req"]}->{heavy["rsp"]}',
                     f'ns_stk={last["nsstk"]} ns_heap={last["nsheap"]} s_stk={last["sstk"]} s_heap={last["sheap"]}'))
    return rows


def main(argv):
    for d in argv:
        run = Path(d)
        print(f"\n== {run.name}")
        print(f'{"op":16s} {"xfers":>5s} {"bytes":>7s} {"dev_us":>9s} {"sign_us":>8s} {"ecdh_us":>8s} '
              f'{"kem_us":>7s} {"largest":>7s} {"max_msg_us":>10s} {"max_msg":9s} peaks')
        for r in summarise(run):
            print(f"{r[0]:16s} {r[1]:5d} {r[2]:7d} {r[3]:9d} {r[4]:8d} {r[5]:8d} {r[6]:7d} {r[7]:7d} {r[8]:10d} {r[9]:9s} {r[10]}")


if __name__ == "__main__":
    main(sys.argv[1:])
