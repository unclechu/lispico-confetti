let
  commit = "18c0c9ef21b3753c95a8a566789a6811d2f3b31c";
  hash = "sha256:07vzq0lq4cinvg7pmn25if7kbpp1ikkamwkzrx0m9fn7bkilanhw";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
