FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
COPY deb-cache /debs
# The initial dpkg pass configures Python's pre-dependencies; install the two
# meta-packages afterward, and require a clean dependency audit before use.
RUN export DEBIAN_FRONTEND=noninteractive; \
    (dpkg -i /debs/*.deb || true) && \
    dpkg -i /debs/python3-minimal_*.deb && \
    dpkg -i /debs/python3_*.deb && \
    apt-get check && test -z "$(dpkg --audit)" && \
    rm -rf /debs /var/lib/apt/lists/*
WORKDIR /work
