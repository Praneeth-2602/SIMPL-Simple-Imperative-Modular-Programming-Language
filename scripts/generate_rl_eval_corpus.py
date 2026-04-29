#!/usr/bin/env python3
from __future__ import annotations

import sys

from generate_rl_corpora import main

if __name__ == "__main__":
    sys.argv.insert(1, "--mode")
    sys.argv.insert(2, "eval")
    raise SystemExit(main())
