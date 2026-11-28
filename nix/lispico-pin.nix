let
  commit = "2bfd6e76eb98ccd53aa0446d3578ca58e0421b6a";
  hash = "sha256:0841zb06hp8dlpkqzkh7jgc6z1ardi7yp5w47dr07h4x53dhxv7j";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
