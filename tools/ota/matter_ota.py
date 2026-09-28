#!/usr/bin/env python3
"""Local Matter OTA release tool for esp-demo-matter.

How Matter OTA works here: the device is an OTA *Requestor*. It never looks
for updates itself — a controller tells it where an OTA *Provider* is
(AnnounceOTAProvider), the device asks that provider "I am VID/PID at version
N, anything newer?" (QueryImage), and the provider streams the image over BDX.

This tool plays both roles next to Home Assistant, on its own fabric
(Matter multi-admin, so HA keeps working as before):

  controller  python CHIP controller (home-assistant-chip-core wheel)
  provider    prebuilt chip-ota-provider-app (home-assistant-libs)

Both run in a Docker container (tools/ota/Dockerfile, built on first use)
with host networking; bumping the version and building happen on the host.
State (this tool's fabric) lives in ~/.local/state/esp-demo-matter-ota.

Commands:
  pair <code> [--node N]   one-time: join a device to this tool's fabric
                           (<code> from HA: device page → "Share device")
  release [--no-bump]      bump version, build, offer to all paired devices
  offer                    offer the current build without bumping/building
  status                   print software version / update state per device
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
IN_CONTAINER = bool(os.environ.get("OTA_IN_CONTAINER"))
# Host-side state (fabric keys, controller + provider storage). Mounted at
# /data in the container, which is where the CHIP wheels insist on storing it.
HOST_STATE = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "esp-demo-matter-ota"
STATE = Path("/data") if IN_CONTAINER else HOST_STATE
OTA_BIN = REPO / "build" / "esp_demo_matter-ota.bin"
CMAKELISTS = REPO / "CMakeLists.txt"

IMAGE = "esp-demo-matter-ota"
PROVIDER_BIN = "/usr/local/bin/chip-ota-provider-app"   # baked into the image
PAA_DIR = "/paa"                                        # test PAA roots, baked in

CONTROLLER_NODE = 112233
PROVIDER_NODE = 0xFFFF_0001        # provider's node ID on our fabric
PROVIDER_DISCRIMINATOR = 3841
PROVIDER_PASSCODE = 20202021
PROVIDER_PORT = 5565               # keep clear of 5540 in case something else uses it
FABRIC_VENDOR = 0xFFF1

CONFIG = STATE / "config.json"     # {"devices": [node ids], "provider_commissioned": bool}


def log(msg: str) -> None:
    print(f"[ota] {msg}", flush=True)


def load_config() -> dict:
    if CONFIG.exists():
        return json.loads(CONFIG.read_text())
    return {"devices": [], "provider_commissioned": False}


def save_config(cfg: dict) -> None:
    CONFIG.write_text(json.dumps(cfg, indent=2))


# ── version bump + build ──────────────────────────────────────────────────────

def bump_version() -> tuple[str, int]:
    text = CMAKELISTS.read_text()
    ver = re.search(r'set\(PROJECT_VER "(\d+)\.(\d+)\.(\d+)"\)', text)
    num = re.search(r"set\(PROJECT_VER_NUMBER (\d+)\)", text)
    if not ver or not num:
        sys.exit("could not find PROJECT_VER / PROJECT_VER_NUMBER in CMakeLists.txt")
    major, minor, patch = map(int, ver.groups())
    new_ver = f"{major}.{minor}.{patch + 1}"
    new_num = int(num.group(1)) + 1
    text = text.replace(ver.group(0), f'set(PROJECT_VER "{new_ver}")')
    text = text.replace(num.group(0), f"set(PROJECT_VER_NUMBER {new_num})")
    CMAKELISTS.write_text(text)
    log(f"version bumped to {new_ver} (number {new_num})")
    return new_ver, new_num


def build() -> None:
    log("building firmware (idf.py build)")
    idf = shutil.which("idf.py")
    if idf:
        cmd = ["idf.py", "build"]
    else:
        # Not in an ESP-IDF shell — source the export script in bash.
        export = Path.home() / ".espressif/v6.1/esp-idf/export.sh"
        cmd = ["bash", "-c", f"source {export} >/dev/null && idf.py build"]
    subprocess.run(cmd, cwd=REPO, check=True)


def image_version(path: Path) -> tuple[int, str]:
    """Read the version fields out of the Matter OTA image header."""
    tool = next(REPO.glob("managed_components/espressif__esp_matter/connectedhomeip/"
                          "connectedhomeip/src/app/ota_image_tool.py"))
    out = subprocess.run([sys.executable, str(tool), "show", str(path)],
                         capture_output=True, text=True, check=True).stdout
    num = int(re.search(r"Version: (\d+)", out).group(1))
    vstr = re.search(r"Version String: (\S+)", out).group(1)
    return num, vstr


# ── Matter controller ─────────────────────────────────────────────────────────

class Controller:
    def __init__(self) -> None:
        # Imported lazily so `--help` works without the wheels installed.
        import chip.native
        import chip.logging
        import chip.CertificateAuthority
        from chip.ChipStack import ChipStack

        chip.native.Init()
        self.stack = ChipStack(str(STATE / "controller.json"), enableServerInteractions=False)
        ca_mgr = chip.CertificateAuthority.CertificateAuthorityManager(
            self.stack, self.stack.GetStorageManager())
        ca_mgr.LoadAuthoritiesFromStorage()
        ca = ca_mgr.activeCaList[0] if ca_mgr.activeCaList else ca_mgr.NewCertificateAuthority()
        admin = ca.adminList[0] if ca.adminList else ca.NewFabricAdmin(vendorId=FABRIC_VENDOR, fabricId=1)
        self.ctrl = admin.NewController(nodeId=CONTROLLER_NODE, paaTrustStorePath=PAA_DIR)
        self._ca_mgr = ca_mgr

    def shutdown(self) -> None:
        self.ctrl.Shutdown()
        self._ca_mgr.Shutdown()
        self.stack.Shutdown()

    async def pair(self, code: str, node: int) -> None:
        from chip.ChipDeviceCtrl import DiscoveryType
        await self.ctrl.CommissionWithCode(code, node, DiscoveryType.DISCOVERY_NETWORK_ONLY)

    async def commission_provider(self) -> None:
        from chip.discovery import FilterType
        await self.ctrl.CommissionOnNetwork(
            nodeId=PROVIDER_NODE, setupPinCode=PROVIDER_PASSCODE,
            filterType=FilterType.LONG_DISCRIMINATOR, filter=PROVIDER_DISCRIMINATOR)

    async def grant_provider_access(self, devices: list[int]) -> None:
        """Provider ACL: us as admin, the devices may call the OTA Provider cluster."""
        from chip.clusters import AccessControl as AC, OtaSoftwareUpdateProvider as OTAP
        from chip.clusters.Types import NullValue
        Entry = AC.Structs.AccessControlEntryStruct
        acl = [
            Entry(privilege=AC.Enums.AccessControlEntryPrivilegeEnum.kAdminister,
                  authMode=AC.Enums.AccessControlEntryAuthModeEnum.kCase,
                  subjects=[CONTROLLER_NODE], targets=NullValue),
            Entry(privilege=AC.Enums.AccessControlEntryPrivilegeEnum.kOperate,
                  authMode=AC.Enums.AccessControlEntryAuthModeEnum.kCase,
                  subjects=devices,
                  targets=[AC.Structs.AccessControlTargetStruct(cluster=OTAP.id)]),
        ]
        await self.ctrl.WriteAttribute(PROVIDER_NODE, [(0, AC.Attributes.Acl(acl))])

    async def announce(self, device: int) -> None:
        from chip.clusters import OtaSoftwareUpdateRequestor as OTAR
        await self.ctrl.SendCommand(device, 0, OTAR.Commands.AnnounceOTAProvider(
            providerNodeID=PROVIDER_NODE, vendorID=FABRIC_VENDOR,
            announcementReason=OTAR.Enums.AnnouncementReasonEnum.kUpdateAvailable,
            endpoint=0))

    async def read_state(self, device: int) -> tuple[int | None, str, int | None]:
        from chip.clusters import BasicInformation as BI, OtaSoftwareUpdateRequestor as OTAR
        res = await self.ctrl.ReadAttribute(device, [
            (0, BI.Attributes.SoftwareVersion),
            (0, OTAR.Attributes.UpdateState),
            (0, OTAR.Attributes.UpdateStateProgress),
        ])
        ep = res[0]
        ver = ep[BI][BI.Attributes.SoftwareVersion]
        state = ep[OTAR][OTAR.Attributes.UpdateState]
        prog = ep[OTAR][OTAR.Attributes.UpdateStateProgress]
        return ver, getattr(state, "name", str(state)), prog


# ── provider process ──────────────────────────────────────────────────────────

def start_provider(image: Path) -> subprocess.Popen:
    logfile = open(STATE / "provider.log", "w")
    log(f"starting OTA provider (log: {STATE / 'provider.log'})")
    proc = subprocess.Popen([
        PROVIDER_BIN, "-f", str(image),
        "--KVS", str(STATE / "provider.kvs"),
        "--discriminator", str(PROVIDER_DISCRIMINATOR),
        "--passcode", str(PROVIDER_PASSCODE),
        "--secured-device-port", str(PROVIDER_PORT),
    ], stdout=logfile, stderr=subprocess.STDOUT, start_new_session=True)
    time.sleep(3)
    if proc.poll() is not None:
        sys.exit(f"provider exited immediately — see {STATE / 'provider.log'}")
    return proc


def stop_provider(proc: subprocess.Popen) -> None:
    if proc.poll() is None:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=10)


# ── commands ──────────────────────────────────────────────────────────────────

async def cmd_pair(args) -> None:
    cfg = load_config()
    node = args.node or max(cfg["devices"] + [0]) + 1
    c = Controller()
    try:
        log(f"commissioning device as node {node} (network only)")
        await c.pair(args.code, node)
        ver, state, _ = await c.read_state(node)
        log(f"paired: node {node}, software version {ver}, OTA state {state}")
    finally:
        c.shutdown()
    if node not in cfg["devices"]:
        cfg["devices"].append(node)
    cfg["provider_commissioned"] = False  # ACL must be rewritten to include the new node
    save_config(cfg)


async def cmd_status(_args) -> None:
    cfg = load_config()
    c = Controller()
    try:
        for node in cfg["devices"]:
            try:
                ver, state, prog = await c.read_state(node)
                log(f"node {node}: version {ver}, state {state}, progress {prog}")
            except Exception as e:  # noqa: BLE001
                log(f"node {node}: unreachable ({e})")
    finally:
        c.shutdown()


async def offer(timeout_s: int) -> None:
    cfg = load_config()
    if not cfg["devices"]:
        sys.exit("no devices paired — run `pair <code>` first")
    if not OTA_BIN.exists():
        sys.exit(f"{OTA_BIN} missing — build first")
    target, target_str = image_version(OTA_BIN)
    log(f"offering {OTA_BIN.name}: {target_str} (number {target})")

    provider = start_provider(OTA_BIN)
    c = Controller()
    try:
        if not cfg["provider_commissioned"]:
            log("commissioning provider onto our fabric")
            await c.commission_provider()
            await c.grant_provider_access(cfg["devices"])
            cfg["provider_commissioned"] = True
            save_config(cfg)

        pending = []
        for node in cfg["devices"]:
            ver, _, _ = await c.read_state(node)
            if ver is not None and ver >= target:
                log(f"node {node}: already on {ver}, skipping")
                continue
            log(f"node {node}: on {ver} — announcing provider")
            await c.announce(node)
            pending.append(node)

        deadline = time.monotonic() + timeout_s
        while pending and time.monotonic() < deadline:
            await asyncio.sleep(10)
            for node in list(pending):
                try:
                    ver, state, prog = await c.read_state(node)
                except Exception:  # noqa: BLE001 — expected while it reboots
                    log(f"node {node}: not responding (rebooting?)")
                    continue
                if ver == target:
                    log(f"node {node}: updated to {target_str}")
                    pending.remove(node)
                else:
                    log(f"node {node}: {state} {prog if prog is not None else ''}")
        if pending:
            log(f"timed out waiting for nodes {pending} — see {STATE / 'provider.log'}")
            sys.exit(1)
    finally:
        c.shutdown()
        stop_provider(provider)


def in_container(argv: list[str]) -> None:
    """Re-run this script with `argv` inside the controller/provider container."""
    HOST_STATE.mkdir(parents=True, exist_ok=True)
    if subprocess.run(["docker", "image", "inspect", IMAGE], capture_output=True).returncode:
        log(f"building container image {IMAGE}")
        subprocess.run(["docker", "build", "-t", IMAGE, str(REPO / "tools/ota")], check=True)
    # Host network: the device is reached over the Thread border router's
    # IPv6 routes and found via mDNS, exactly as from the host itself.
    cmd = ["docker", "run", "--rm", "-it" if sys.stdin.isatty() else "-i",
           "--network", "host",
           # Run as the host user so the state files stay owned by you.
           "--user", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/tmp",
           "-v", f"{HOST_STATE}:/data", "-v", f"{REPO}:/repo:ro",
           IMAGE, *argv]
    sys.exit(subprocess.run(cmd).returncode)


def cmd_release(args) -> None:
    if not args.no_bump:
        bump_version()
    build()
    in_container(["offer", "--timeout", str(args.timeout)])


async def cmd_offer(args) -> None:
    await offer(args.timeout)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("pair"); sp.add_argument("code"); sp.add_argument("--node", type=int)
    sp.set_defaults(fn=cmd_pair)
    sr = sub.add_parser("release"); sr.add_argument("--no-bump", action="store_true")
    sr.add_argument("--timeout", type=int, default=1800); sr.set_defaults(fn=cmd_release)
    so = sub.add_parser("offer"); so.add_argument("--timeout", type=int, default=1800)
    so.set_defaults(fn=cmd_offer)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    args = p.parse_args()
    if args.cmd == "release":
        if IN_CONTAINER:
            sys.exit("release runs on the host (it needs ESP-IDF to build)")
        cmd_release(args)
    elif not IN_CONTAINER:
        in_container(sys.argv[1:])
    else:
        STATE.mkdir(parents=True, exist_ok=True)
        asyncio.run(args.fn(args))


if __name__ == "__main__":
    main()
