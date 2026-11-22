let
  commit = "3be155a608e063ab701f3bf015971d3ed58b3455";
  hash = "sha256:0d0427gz0xhyajvxwg0bws3pyql007dxl3nrlzbbkcvzikrvyq1d";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
