let
  commit = "fc54fd678ab33e45cb12b2ebad20878cf2a96fd3";
  hash = "sha256-5fB9rAOquA7oGcmVhRTa0NN/xVmzaZyVQdXssO2xjlo=";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
