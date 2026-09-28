# Matter controller + OTA provider for tools/ota/matter_ota.py.
# Containerised because Home Assistant's CHIP wheels hard-code their storage
# to /data (they are built for the Matter Server add-on).
FROM python:3.12-slim-bookworm

ARG CHIP_WHEELS=2025.7.0
ARG PROVIDER_RELEASE=2025.9.0

RUN apt-get update \
 && apt-get install -y --no-install-recommends libglib2.0-0 libgirepository-1.0-1 libnl-3-200 libnl-route-3-200 libssl3 curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir \
        home-assistant-chip-core==${CHIP_WHEELS} home-assistant-chip-clusters==${CHIP_WHEELS}
RUN curl -fsSL -o /usr/local/bin/chip-ota-provider-app \
        https://github.com/home-assistant-libs/matter-linux-ota-provider/releases/download/${PROVIDER_RELEASE}/chip-ota-provider-app-x86-64 \
 && chmod +x /usr/local/bin/chip-ota-provider-app
RUN mkdir -p /paa \
 && for f in Chip-Test-PAA-FFF1-Cert.der Chip-Test-PAA-NoVID-Cert.der; do \
      curl -fsSL -o /paa/$f https://raw.githubusercontent.com/project-chip/connectedhomeip/master/credentials/development/paa-root-certs/$f; \
    done

ENV OTA_IN_CONTAINER=1
WORKDIR /repo
ENTRYPOINT ["python", "/repo/tools/ota/matter_ota.py"]
