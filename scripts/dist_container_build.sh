#!/usr/bin/env bash
# 配布物を **ubuntu:22.04 のコンテナの中で** 作る（#20 / A39）。
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:22.04 bash scripts/dist_container_build.sh \
#     -DFETCHCONTENT_BASE_DIR=/src/build/_deps -DSHASHOKU_TEST_ASSETS_DIR=/src/build/_assets
#
# 引数はそのまま `cmake --preset dist` に渡る。
#
# **なぜコンテナの中か**: 配布物は C++ ランタイムだけを静的にし、glibc は動的のままにしている
# （完全静的は glibc の LGPL を理由に却下した。A39）。glibc を動的にすると、
# **ビルド機の glibc の版がそのまま「動く環境の下限」になる**。ランナー（ubuntu-24.04、
# glibc 2.39）で直接ビルドすると 22.04 で起動できないバイナリができるので、22.04 で作る。
#
# apt を使うので root で走る前提。ツールチェーンの導入は SHASHOKU_SKIP_TOOLCHAIN=1 で
# 飛ばせる（既に clang-18 + libc++ がある環境で、残りの手順をローカルで試すため）。
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
export CCACHE_DIR="${CCACHE_DIR:-$PWD/build/_ccache}"

install_toolchain() {
  echo "== ツールチェーンを入れる（素の ubuntu:22.04 には何も無い）"
  apt-get update
  apt-get install -y --no-install-recommends \
    ca-certificates curl gnupg xz-utils git file binutils \
    cmake ninja-build ccache

  # clang-18 / libc++-18 は 22.04 の apt には無いので apt.llvm.org の jammy-18 を足す。
  curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/trusted.gpg.d/apt-llvm.asc
  echo "deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main" \
    > /etc/apt/sources.list.d/llvm-18.list
  apt-get update
  apt-get install -y --no-install-recommends clang-18 libc++-18-dev libc++abi-18-dev

  # ホストの利用者が所有するワークツリーを root で触るので、git に許可を出す
  # （pack_dist.sh が tar の時刻にコミットの日時を使う）。
  git config --global --add safe.directory "$PWD" || true
}

if [ -n "${SHASHOKU_SKIP_TOOLCHAIN:-}" ]; then
  echo "== ツールチェーンの導入を飛ばす（SHASHOKU_SKIP_TOOLCHAIN）"
else
  install_toolchain
fi

echo "== 版"
cmake --version | head -1
ninja --version
clang++-18 --version | head -1
ldd --version | head -1

echo "== configure / build / test（dist プリセット = Release + 静的ランタイム + 既定フォント）"
if command -v ccache > /dev/null; then
  export CMAKE_C_COMPILER_LAUNCHER=ccache
  export CMAKE_CXX_COMPILER_LAUNCHER=ccache
fi
cmake --preset dist "$@"
cmake --build --preset dist
# 配布するのと同じ設定でビルドしたバイナリで、ゴールデン画像を含む全件を通す（A32）。
ctest --preset dist

bin=build/dist/tools/shashoku/shashoku
echo "== 配布物の検査"
scripts/check_dist_binary.sh "$bin"
"$bin" --version

echo "== アーカイブの組み立て"
scripts/pack_dist.sh "$bin" build/package
