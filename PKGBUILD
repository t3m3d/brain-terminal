# Maintainer: Brian <brian@krypton-lang.org>
pkgname=brain
pkgver=0.1.0
pkgrel=1
pkgdesc="Cross-platform Qt6 terminal emulator (PTY-based, theme-able)"
arch=('x86_64' 'aarch64')
url="https://github.com/t3m3d/brain"
license=('MIT')
depends=('qt6-base' 'hicolor-icon-theme')
makedepends=('git' 'cmake')
source=("git+$url.git#branch=main")
sha256sums=('SKIP')

build() {
    cd "$srcdir/$pkgname"
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build -j"$(nproc)"
}

package() {
    cd "$srcdir/$pkgname"

    install -Dm755 build/brain "$pkgdir/usr/bin/brain"

    if [[ -f resources/config/brain.conf ]]; then
        install -Dm644 resources/config/brain.conf \
            "$pkgdir/usr/share/brain/brain.conf"
    fi
    if [[ -d resources/themes ]]; then
        install -dm755 "$pkgdir/usr/share/brain/themes"
        cp -r resources/themes/* "$pkgdir/usr/share/brain/themes/"
    fi

    install -Dm644 LICENSE \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    if [[ -f BUILD_LINUX.md ]]; then
        install -Dm644 BUILD_LINUX.md \
            "$pkgdir/usr/share/doc/$pkgname/BUILD_LINUX.md"
    fi
}
