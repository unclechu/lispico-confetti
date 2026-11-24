let
  commit = "b2928cb4715eaf509a6e9a942c8854ac4945e5f4";
  hash = "sha256:0v2za3z4zf7j0zrvgq04vif56nrgh3n1nwj4gs34bmy6ky12dscj";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
