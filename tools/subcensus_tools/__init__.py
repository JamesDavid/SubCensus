"""SubCensus host-side tools.

Pure-Python utilities shared by every sensor (System §8) plus the codegen that keeps
the on-device artifacts in lockstep with the `shared/` single source of truth (System §10).

Nothing here runs on the Flipper; all operate on the shared artifacts or generate code.
"""

__version__ = "0.1.0"
