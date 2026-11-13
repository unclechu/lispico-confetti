let
  commit = "d7de35985d8c508cbf3c64d0ce9a72d46079dc86";
  hash = "0000000000000000000000000000000000000000000000000000";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
