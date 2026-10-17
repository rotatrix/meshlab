"""Collect notices under short content-addressed names with a source-path index."""
import hashlib
import json
import os
from pathlib import Path


def collect(destination, roots):
    destination.mkdir(parents=True, exist_ok=True)
    index = []
    for label, root in roots:
        for folder, dirs, files in os.walk(root):
            dirs[:] = sorted(d for d in dirs if d not in
                             ('.git', 'node_modules', 'ci-logs', 'artifacts', 'install')
                             and not d.startswith('build'))
            for name in sorted(files):
                if not name.lower().startswith(('license', 'licence', 'copying', 'copyright', 'notice')):
                    continue
                path = Path(folder) / name
                if not path.is_file():
                    continue
                content = path.read_bytes()
                sha = hashlib.sha256(content).hexdigest()
                target = sha + '.txt'
                (destination / target).write_bytes(content)
                index.append(dict(component=label, source=path.relative_to(root).as_posix(),
                                  file=target, sha256=sha))
    (destination / 'sources.json').write_text(json.dumps(index, indent=2) + '\n')
