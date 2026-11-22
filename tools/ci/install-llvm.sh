#!/bin/sh
# Install LLVM (clang, the sanitizer and libFuzzer runtimes, clang-tidy) from apt.llvm.org
# in an Ubuntu CI container. The devspace installs the same packages
# (dotfiles devspaces/prusa.sh), so CI and local builds use the same clang.
# apt.llvm.org has no repository for an interim Ubuntu release such as 26.10. Then the
# script uses the repository of the LTS release 26.04 (resolute), as the devspace does.
# Run it as root. Usage: tools/ci/install-llvm.sh [VERSION]
set -eu
ver=${1:-23}
ver=${ver#clang-}
. /etc/os-release
codename=$VERSION_CODENAME
apt-get update -qq
apt-get install -y -qq --no-install-recommends ca-certificates curl >/dev/null
if ! curl -fsI "https://apt.llvm.org/$codename/dists/llvm-toolchain-$codename-$ver/Release" >/dev/null 2>&1; then
    codename=resolute
fi
install -d -m 0755 /etc/apt/keyrings
curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key > /etc/apt/keyrings/apt.llvm.org.asc
echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/$codename/ llvm-toolchain-$codename-$ver main" \
    > /etc/apt/sources.list.d/llvm.list
apt-get update -qq
apt-get install -y -qq --no-install-recommends "clang-$ver" "libclang-rt-$ver-dev" "clang-tidy-$ver" >/dev/null
"clang-$ver" --version | head -1
