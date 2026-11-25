let
  commit = "a99918640d95d3e26e00772865a27d9e1d1a32dc";
  hash = "sha256:1nizsyxskwh2qlj9rp5wci7vflkwcwijw9fxl8clsf9xnf7khfq2";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
