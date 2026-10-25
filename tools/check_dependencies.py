"""Verify pinned local dependency contents without contacting upstream branches."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    lock = json.loads((ROOT / 'tools/dependencies.lock.json').read_text(encoding='utf-8'))
    errors = []
    for name, digest in lock['files'].items():
        path = ROOT / name
        if not path.is_file():
            errors.append(f'missing: {name}')
        elif hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            errors.append(f'changed: {name}')
    for folder in ('firmware/third_party/', 'firmware/fonts/'):
        expected = {n for n in lock['files'] if n.startswith(folder)}
        actual = {p.relative_to(ROOT).as_posix() for p in (ROOT / folder).rglob('*') if p.is_file()}
        errors.extend(f'unlocked: {n}' for n in sorted(actual - expected))
    for error in errors:
        print(error)
    print(f'dependency lock: {len(lock["files"])} files, {len(errors)} errors')
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
