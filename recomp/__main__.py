"""CLI: python3 -m recomp --dol main.dol --map build.map --overrides recomp/overrides.toml --out DIR"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from recomp.emit import RecompError
from recomp.image import ImageError, load_image
from recomp.program import generate


def main() -> int:
    ap = argparse.ArgumentParser(prog="recomp", description="Recompile main.dol to C.")
    ap.add_argument("--dol", type=Path, required=True)
    ap.add_argument("--map", type=Path, required=True)
    ap.add_argument("--overrides", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--shards", type=int, default=64)
    args = ap.parse_args()
    try:
        img = load_image(args.dol, args.map)
        report = generate(img, args.overrides, args.out, args.shards)
    except (ImageError, RecompError, OSError) as e:
        print(f"recomp: error: {e}", file=sys.stderr)
        return 2
    print(f"recomp: {report.functions} translated, {report.overridden} overridden, "
          f"{len(report.unimpl)} unimplemented, {len(report.errors)} errors "
          f"(details in {args.out / 'report.txt'})")
    for e in report.errors[:10]:
        print(f"recomp: error: {e}", file=sys.stderr)
    return 1 if report.errors else 0


if __name__ == "__main__":
    sys.exit(main())
