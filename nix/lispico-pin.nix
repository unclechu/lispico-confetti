let
  commit = "d0d7af17fbfd5373ecc4882b98f87519a1bbd2b7";
  hash = "sha256:14g45d32h98a8y5y3w0cn1jsgr10c8aaaljikpjf0jscr8aibqgz";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
