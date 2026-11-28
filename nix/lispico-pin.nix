let
  commit = "f21b76244d5c613b2f3b6c20e629e97b1174f2ef";
  hash = "sha256:05g5x8c83jx5f03xk6jr96llzpmw47fpvgi5w99g7w2r3yfflril";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
