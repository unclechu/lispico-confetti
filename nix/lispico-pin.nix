let
  commit = "64d04c3c67d9b6b325a7ff25a07c1c1eb2381994";
  hash = "sha256:1l9wajvfnnlzwyls4iifjhfx3kcq5iwzv2dk67bpdmwylxny5sw6";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
