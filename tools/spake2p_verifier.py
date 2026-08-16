#!/usr/bin/env python3
"""Generate a Matter SPAKE2+ verifier for a custom setup passcode.

The firmware embeds its commissioning passcode as a SPAKE2+ verifier
(CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_VERIFIER in main/chip_project_config.h).
Changing the passcode there requires regenerating the verifier with this tool:

    python3 tools/spake2p_verifier.py <passcode>

Then paste the printed base64 string into main/chip_project_config.h.

Requires the `cryptography` package (present in the ESP-IDF python env).
Mirrors Spake2pVerifier::Generate() in the CHIP SDK: verifier = w0 || L where
w0s||w1s = PBKDF2-SHA256(passcode LE32, salt, iterations, 80 bytes),
w0 = w0s mod n, w1 = w1s mod n, L = w1 * G (P-256, uncompressed point).
"""

import base64
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC

# Defaults matching CHIP_DEVICE_CONFIG_USE_TEST_SPAKE2P_SALT / ITERATION_COUNT
SALT_B64 = "U1BBS0UyUCBLZXkgU2FsdA=="  # "SPAKE2P Key Salt"
ITERATIONS = 1000

# NIST P-256 group order
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551

# Matter spec 5.1.7.1: passcodes that MUST NOT be used
INVALID_PASSCODES = {
    0, 11111111, 22222222, 33333333, 44444444, 55555555,
    66666666, 77777777, 88888888, 99999999, 12345678, 87654321,
}


def generate_verifier(passcode: int, salt: bytes, iterations: int) -> bytes:
    ws = PBKDF2HMAC(
        algorithm=hashes.SHA256(), length=80, salt=salt, iterations=iterations
    ).derive(struct.pack("<I", passcode))
    w0 = int.from_bytes(ws[:40], "big") % P256_ORDER
    w1 = int.from_bytes(ws[40:], "big") % P256_ORDER
    L = ec.derive_private_key(w1, ec.SECP256R1()).public_key().public_bytes(
        serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint
    )
    return w0.to_bytes(32, "big") + L


def self_test() -> None:
    # Known vector from CHIPDeviceConfig.h (passcode 20202021, default salt/iters)
    expected = (
        "uWFwqugDNGiEck/po7KHwwMwwqZgN10XuyBajPGuyzUEV/iree4lOrao5GuwnlQ65CJz"
        "beUB49s31EH+NEkg0JVI5MGCQGMMT/SRPFNRODm3wH/MBiehuFc6FJ/NH6Rmzw=="
    )
    got = base64.b64encode(
        generate_verifier(20202021, base64.b64decode(SALT_B64), ITERATIONS)
    ).decode()
    assert got == expected, f"self-test failed:\n  got      {got}\n  expected {expected}"


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(f"usage: {sys.argv[0]} <passcode (1-99999998)>")
    passcode = int(sys.argv[1])
    if not 1 <= passcode <= 99999998 or passcode in INVALID_PASSCODES:
        sys.exit(f"error: {passcode} is not a valid Matter setup passcode")

    self_test()
    verifier = generate_verifier(passcode, base64.b64decode(SALT_B64), ITERATIONS)
    print(f"passcode : {passcode}")
    print(f"verifier : {base64.b64encode(verifier).decode()}")


if __name__ == "__main__":
    main()
