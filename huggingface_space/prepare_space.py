"""
Prepare the standalone Gradio Space for deployment.

The demo wraps the SAME agent pipeline the FastAPI backend uses, defined in
`backend/bots.py`. A HuggingFace Space is a self-contained git repo, so the
sibling `../backend` directory is NOT present at runtime. This script vendors
`bots.py` into this folder so `app.py` can import it directly.

Run this before every deploy so the Space agents match the backend agents:

    python prepare_space.py
    # then push this folder to the Space (see README.md)

`bots.py` is gitignored in the main repo (it is a generated copy) but IS pushed
to the Space, where it must exist for the import to resolve.
"""

import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
SRC = HERE.parent / "backend" / "bots.py"
DST = HERE / "bots.py"


def main() -> None:
    if not SRC.exists():
        raise SystemExit(f"Source not found: {SRC}")
    shutil.copy2(SRC, DST)
    print(f"Vendored bots.py -> {DST.name} ({DST.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
