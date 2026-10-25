"""Verify a trusted toolchain ZIP before atomically replacing the installation."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ZIP = ROOT / 'tools/downloads/arm-gnu-toolchain-13.3.zip'
DEST = ROOT / 'tools/arm-gnu-toolchain'


def within(path, workspace):
    resolved = Path(path).resolve()
    base = Path(workspace).resolve()
    if resolved == base or not resolved.is_relative_to(base):
        raise ValueError(f'path outside installation workspace: {path}')
    return resolved


def members(zf):
    result, seen, tops = [], set(), set()
    reserved = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)),
                *(f'LPT{i}' for i in range(1, 10))}
    for info in zf.infolist():
        name = info.filename
        parts = PurePosixPath(name).parts
        if (not parts or name.startswith('/') or '\\' in name or ':' in name or
                '..' in parts or stat.S_ISLNK(info.external_attr >> 16)):
            raise ValueError(f'unsafe ZIP member: {name}')
        if any(p.endswith((' ', '.')) or p.split('.')[0].upper() in reserved for p in parts):
            raise ValueError(f'unsafe Windows name: {name}')
        tops.add(parts[0])
        if len(parts) == 1:
            if not info.is_dir():
                raise ValueError('ZIP must have one top-level directory')
            continue
        rel = Path(*parts[1:])
        key = rel.as_posix().casefold()
        if key in seen:
            raise ValueError(f'duplicate ZIP destination: {name}')
        seen.add(key)
        result.append((info, rel))
    if len(tops) != 1 or not result:
        raise ValueError('ZIP must have one nonempty top-level directory')
    if not any(rel.as_posix().casefold() == 'bin/arm-none-eabi-gcc.exe' and not i.is_dir()
               and i.file_size > 0 for i, rel in result):
        raise ValueError('archive does not contain the compiler')
    return result


def install_toolchain(archive, destination, expected_sha256, workspace=ROOT):
    archive = within(archive, workspace)
    dest = within(destination, workspace)
    if Path(destination).is_symlink() or Path(destination).is_junction():
        raise ValueError('installation directory cannot be a symlink')
    if len(expected_sha256) != 64 or any(c not in '0123456789abcdefABCDEF' for c in expected_sha256):
        raise ValueError('a trusted SHA256 is required')
    with archive.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual.lower() != expected_sha256.lower():
        raise ValueError('archive SHA256 mismatch')
    previous = within(dest.with_name(dest.name + '.previous'), workspace)
    if previous.exists():
        raise ValueError(f'recovery directory already exists: {previous}')
    with zipfile.ZipFile(archive) as zf:
        items = members(zf)
        bad = zf.testzip()
        if bad:
            raise ValueError(f'corrupt ZIP member: {bad}')
        dest.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.toolchain-', dir=dest.parent) as temp:
            stage = within(temp, workspace)
            for info, rel in items:
                target = within(stage / rel, stage)
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with zf.open(info) as src, target.open('wb') as dst:
                        shutil.copyfileobj(src, dst)
            (stage / 'installation.json').write_text(json.dumps({'archive_sha256': actual}, indent=2), encoding='utf-8')
            if dest.exists():
                dest.rename(previous)
            try:
                stage.rename(dest)
            except OSError:
                if previous.exists():
                    previous.rename(dest)
                raise
    if previous.exists():
        # Check the resolved absolute target before recursive removal on Windows.
        shutil.rmtree(within(previous, workspace))
    archive.unlink()
    print(f'toolchain ready: {dest / "bin/arm-none-eabi-gcc.exe"}')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sha256', help='trusted publisher SHA256 of the ZIP')
    args = ap.parse_args(argv)
    sidecar = Path(str(ZIP) + '.sha256')
    try:
        digest = args.sha256
        if digest is None and sidecar.is_file():
            digest = sidecar.read_text(encoding='utf-8').split()[0]
        return install_toolchain(ZIP, DEST, digest or '')
    except (OSError, ValueError, zipfile.BadZipFile, IndexError) as exc:
        print(f'installation failed: {exc}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
