class Notecmd < Formula
  desc "Small CLI for saving commands with notes and replaying them later"
  homepage "https://github.com/ilaario/notecmd"
  url "https://github.com/ilaario/notecmd/archive/refs/tags/v1.1.1.tar.gz"
  sha256 "821e9f3175b394ecba5c5dbc23b389b60260392693aabb5b87512330fadb2ec6"

  def install
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_match "notecmd version 1.1.1", shell_output("#{bin}/notecmd --version")
  end
end
