let
  commit = "f037791403b1ea4eeac225174de6f27291f66085";
  hash = "sha256:0w68b888dr1r18s001d2bbyjjmi27b8q7984flxvndjna8a9rkq3";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
