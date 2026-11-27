let
  commit = "09e00c58d0c8b9c9687eaaf143a1ab5db775e64d";
  hash = "sha256:0k0ghx117xix7r74ws7ya57y910rcpmhrhi226jhwrawahb7psik";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
