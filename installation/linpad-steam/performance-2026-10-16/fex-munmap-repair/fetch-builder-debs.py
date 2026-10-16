from pathlib import Path
import concurrent.futures
import hashlib
import json
import shlex
import subprocess
import urllib.parse

base = Path(__file__).resolve().parent
metadata = {}
for paragraph in (base / 'builder-package-metadata.txt').read_text().split('\n\n'):
    record = dict(line.split(': ', 1) for line in paragraph.splitlines()
                  if ': ' in line and not line.startswith(' '))
    if 'Filename' in record:
        metadata[record['Filename']] = record
rows = []
for line in (base / 'builder-package-uris.txt').read_text().splitlines():
    if not line.startswith("'http"):
        continue
    url, name, size, _ = shlex.split(line)
    url = url.replace('http://ports.ubuntu.com/', 'https://ports.ubuntu.com/', 1)
    assert url.startswith('https://ports.ubuntu.com/ubuntu-ports/')
    record = metadata[urllib.parse.unquote(urllib.parse.urlsplit(url).path.split('/ubuntu-ports/', 1)[1])]
    assert name == Path(name).name
    rows.append(dict(url=url, filename=name, bytes=int(size), sha256=record['SHA256'],
                     package=record['Package'], version=record['Version']))
(base / 'builder-packages.json').write_text(json.dumps(rows, indent=2) + '\n')
cache = base / 'deb-cache'
cache.mkdir(exist_ok=True)


def fetch(row):
    path = cache / row['filename']
    if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != row['sha256']:
        subprocess.run(['curl', '-fL', '-sS', '--retry', '2', '--max-time', '180',
                        row['url'], '-o', str(path)], check=True)
    assert path.stat().st_size == row['bytes']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256']
    return row['filename']


with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
    for count, name in enumerate(pool.map(fetch, rows), 1):
        print(f'{count}/{len(rows)} SHA256 verified {name}', flush=True)
