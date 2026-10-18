FROM debian:13.7-slim@sha256:eb593cf2c358cacef45ca0a424bbc7d30cfa3466265fc2662b9466a0ca6ba1c5

RUN apt-get update \
 && apt-get install --yes --no-install-recommends \
      ca-certificates git cmake make gcc libc6-dev binutils \
      gcc-mingw-w64-x86-64 musl-tools wine python3 \
 && rm -rf /var/lib/apt/lists/*

# The checkout is mounted from the host and owned by another user, which git refuses as dubious ownership.
RUN git config --system --add safe.directory '*'

# Without a runtime directory wine prints an error line first, which breaks the anchored version tests.
RUN mkdir --mode=0700 /run/wine-runtime
ENV WINEDEBUG=-all XDG_RUNTIME_DIR=/run/wine-runtime
