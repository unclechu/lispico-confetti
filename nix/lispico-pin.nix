let
  commit = "2cfec388c54eae83f88a6d28405fafad5836a8a1";
  hash = "sha256:18qsxhgmrd2qz6jr8vkydzm9jinq875iisa55pwgdkm2zr4l8grs";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
