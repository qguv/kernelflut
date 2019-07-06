# Maintainer: Your Name <youremail@domain.com>
_pkgname=kernelflut
pkgname=kernelflut-git
pkgver=0.1.0
pkgrel=1
pkgdesc="Connect a pixelflut screen as another (virtual) monitor. "
arch=('x86_64')
url="https://github.com/qguv/kernelflut"
license=('GPL')
depends=('evdi')
source=("git://github.com/qguv/${_pkgname}")
noextract=()
md5sums=('SKIP') #autofill using updpkgsums

build() {
  cd "$_pkgname"

  make
}

package() {
  cd "$_pkgname"

  install -d "$pkgdir/usr/bin"
  install -t "$pkgdir/usr/bin" kernelflut
}