let
  commit = "03e114d30b78f9cd49abf170122ca624bbd164bd";
  hash = "sha256-GSmTOjomwJFbaFMoa4FjV0rBdOF6lwM1wbSNuz4h0nk=";
in
builtins.fetchTarball {
  # `master` slightly past `v1.0.0` release tag
  # (`v1.0.0` should work just as well).
  url = "https://github.com/hgs3/confetti/archive/${commit}.tar.gz";
  sha256 = hash;
}
