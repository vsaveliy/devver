pkgname=devver
pkgver=1.0.2
pkgrel=4

pkgdesc="Development environment version checker"
arch=('x86_64')
license=('MIT')

depends=()

makedepends=(
    'cmake'
)

source=()

build() {
    cmake \
        -S "${startdir}" \
        -B "${srcdir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr

    cmake \
        --build "${srcdir}/build" \
        --parallel
}

package() {
    DESTDIR="${pkgdir}" \
        cmake \
        --install "${srcdir}/build"
}