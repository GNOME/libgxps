#!/bin/sh

NAME="$1"
VERSION="$2"

test -n "${MESON_SOURCE_ROOT}" || exit 1
test -n "${MESON_BUILD_ROOT}" || exit 1
test -n "${NAME}" || exit 1
test -n "${VERSION}" || exit 1

cd "${MESON_SOURCE_ROOT}"

echo "Removing old archive…"
rm -f "${NAME}-${VERSION}.tar"
rm -f "${NAME}-${VERSION}.tar.xz"

echo "Creating git archive…"
git archive --prefix="${NAME}-${VERSION}/" --format=tar HEAD -o ${NAME}-${VERSION}.tar

echo "Adding documentation to archive…"
BUILD_DIR=$(realpath --relative-to=${MESON_SOURCE_ROOT} ${MESON_BUILD_ROOT})
tar --transform "s|${BUILD_DIR}|${NAME}-${VERSION}|" -uf ${NAME}-${VERSION}.tar ${BUILD_DIR}/docs/reference/html
tar --transform "s|${BUILD_DIR}|${NAME}-${VERSION}|" -uf ${NAME}-${VERSION}.tar ${BUILD_DIR}/docs/tools/*.1

echo "Compressing archive…"
xz -f "${NAME}-${VERSION}.tar"
