class Notecmd < Formula
  desc "Small CLI for saving commands with notes and replaying them later"
  homepage "https://github.com/ilaario/notecmd"
  url "https://github.com/ilaario/notecmd/archive/refs/tags/v1.1.tar.gz"
  sha256 "52395f8c453e46c07caaad74fa227de749cad08ab30b97bda13c80808c9c02c1"

  def install
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_match "Usage:", shell_output("#{bin}/notecmd --help")
  end
end
