#!/usr/bin/env python3
"""Download hash-pinned JAR assets without executing them, and write an importer source list."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

MAX_MANIFEST = 2 * 1024 * 1024
MAX_FILE = 256 * 1024 * 1024
MAX_TOTAL = 4 * 1024 * 1024 * 1024
MAX_FILES = 1000


def check_url(url):
    if not isinstance(url, str):
        raise ValueError('download URL must be a string')
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
        raise ValueError('download URLs must use HTTPS without credentials or fragments')
    return url


def read_manifest(path):
    with Path(path).open('rb') as source:
        raw = source.read(MAX_MANIFEST + 1)
    if len(raw) > MAX_MANIFEST:
        raise ValueError('download manifest exceeds 2 MiB')
    document = json.loads(raw)
    if not isinstance(document, dict):
        raise ValueError('download manifest must be an object')
    files = document.get('files')
    if document.get('format') != 1 or not isinstance(files, list) or not 1 <= len(files) <= MAX_FILES:
        raise ValueError('invalid download manifest format or file count')
    names, total = set(), 0
    for entry in files:
        if not isinstance(entry, dict):
            raise ValueError('download entry must be an object')
        name = entry.get('path', '')
        if not isinstance(name, str) or not name or any(c in name for c in '\\:<>\"|?*') or any(ord(c) < 32 for c in name):
            raise ValueError('unsafe download path')
        parts = name.split('/')
        if any(not part or part in ('.', '..') or part.endswith((' ', '.')) for part in parts):
            raise ValueError('unsafe download path')
        reserved = {'CON', 'PRN', 'AUX', 'NUL'} | {prefix + str(i) for prefix in ('COM', 'LPT') for i in range(1, 10)}
        if any(part.split('.')[0].upper() in reserved for part in parts):
            raise ValueError('reserved Windows filename')
        if PurePosixPath(name).suffix.lower() != '.jar' or name.casefold() in names:
            raise ValueError('duplicate download path or non-JAR file')
        names.add(name.casefold())
        size = entry.get('size')
        if type(size) is not int or not 1 <= size <= MAX_FILE:
            raise ValueError('download file exceeds size limit')
        total += size
        if total > MAX_TOTAL:
            raise ValueError('download manifest exceeds total size limit')
        if not isinstance(entry.get('sha1'), str) or not re.fullmatch('[0-9a-f]{40}', entry['sha1']):
            raise ValueError('invalid SHA-1 digest')
        check_url(entry.get('url'))
    return files


def matches(path, entry):
    if not path.is_file() or path.stat().st_size != entry['size']:
        return False
    digest = hashlib.sha1()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest() == entry['sha1']


def download_file(entry, destination):
    destination = Path(destination).resolve()
    target = destination.joinpath(*entry['path'].split('/'))
    if not target.resolve().is_relative_to(destination) or target.is_symlink():
        raise ValueError('download path escapes destination')
    target.parent.mkdir(parents=True, exist_ok=True)
    if matches(target, entry):
        return target
    # An interrupted or invalid response must leave any previous file intact.
    for attempt in range(3):
        temporary = None
        try:
            request = urllib.request.Request(entry['url'], headers={
                'User-Agent': 'Mineways-asset-regression/1', 'Accept-Encoding': 'identity'})
            deadline = time.monotonic() + 180
            with urllib.request.urlopen(request, timeout=30) as response:
                check_url(response.geturl())
                length = response.headers.get('Content-Length')
                if length is not None and int(length) != entry['size']:
                    raise ValueError('server content length differs from pinned size: ' + entry['path'])
                digest, received = hashlib.sha1(), 0
                with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.download-', delete=False) as output:
                    temporary = Path(output.name)
                    while True:
                        chunk = response.read1(min(1024 * 1024, entry['size'] + 1 - received))
                        if not chunk:
                            break
                        received += len(chunk)
                        if received > entry['size'] or time.monotonic() > deadline:
                            raise ValueError('download exceeds pinned size or time limit: ' + entry['path'])
                        digest.update(chunk)
                        output.write(chunk)
                if received != entry['size'] or digest.hexdigest() != entry['sha1']:
                    raise ValueError('download size or SHA-1 mismatch: ' + entry['path'])
            os.replace(temporary, target)
            temporary = None
            return target
        except (urllib.error.URLError, TimeoutError) as error:
            if attempt == 2:
                raise RuntimeError('download failed: ' + entry['path']) from error
            time.sleep(attempt + 1)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    raise AssertionError('unreachable retry state')


def download_manifest(manifest, destination, workers=8):
    files = read_manifest(manifest)
    if not 1 <= workers <= 16:
        raise ValueError('workers must be between 1 and 16')
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(download_file, entry, destination) for entry in files]
        for completed, future in enumerate(as_completed(futures), 1):
            try:
                future.result()
            except Exception:
                for pending in futures:
                    pending.cancel()
                raise
            if completed % 50 == 0 or completed == len(files):
                print(f'Verified {completed}/{len(files)} assets', flush=True)
    source_list = destination / 'sources.txt'
    if source_list.is_symlink():
        raise ValueError('source list must not be a symbolic link')
    source_list.write_text(''.join(str(destination.joinpath(*entry['path'].split('/'))) + '\n'
                                   for entry in files), encoding='utf-8')
    return source_list


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--workers', type=int, default=8)
    args = parser.parse_args()
    try:
        source_list = download_manifest(args.manifest, args.destination, args.workers)
    except (ValueError, OSError, RuntimeError) as error:
        parser.exit(1, f'Asset download failed: {error}\n')
    print('Verified source list: ' + str(source_list))


if __name__ == '__main__':
    main()
