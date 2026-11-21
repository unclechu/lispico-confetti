let
  commit = "c3b72c4db49f911ddb37a460365e8065b42cfd52";
  hash = "sha256:1gv7fpn7ng7xidqafp37nwc6g9drfcpjlilaw71ss0iz5dbfdnrc";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
