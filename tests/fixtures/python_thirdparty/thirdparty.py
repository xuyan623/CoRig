"""Fixture node: report versions of third-party libraries imported from the payload.

The worker imports the declared dependencies before calling this module, so a
missing distribution fails the load instead of silently reporting a system copy.
"""
import cryptography
import paramiko

VERSIONS = {
    "paramiko": getattr(paramiko, "__version__", ""),
    "cryptography": getattr(cryptography, "__version__", ""),
}


def process(inputs, state):
    """Republish the imported versions on every trigger tick."""
    return dict(VERSIONS)
