let
  commit = "27d456affc57c34f44a33f99e4e7cdbce491af2d";
  hash = "sha256:0d283a640vc9ar5s7qw1j93l569kn0bzry725bsv57awxpgkbs8w";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/sublime-text-confetti/archive/${commit}.tar.gz";
  sha256 = hash;
}
