let
  commit = "d7de35985d8c508cbf3c64d0ce9a72d46079dc86";
  hash = "sha256:08crykv1dbffg13lw723ig4i56f81n13pwz3yizmd8nb1ydlaf9l";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
