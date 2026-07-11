"""Write generated build artifacts only when content changes."""

import os


def write_if_changed(path, content):
    """Write *content* to *path* if it differs from the existing file. Returns True if written."""
    existing = None
    if os.path.isfile(path):
        try:
            with open(path, "r", encoding="utf-8") as file:
                existing = file.read()
        except OSError:
            pass

    if existing == content:
        return False

    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)
    temp_path = path + ".tmp"
    with open(temp_path, "w", encoding="utf-8", newline="\n") as file:
        file.write(content)
    os.replace(temp_path, path)
    return True
