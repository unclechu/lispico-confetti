let
  commit = "6dc330ecebed838c2b2f97e75f6a4c62e70feb66";
  hash = "sha256:0c8v3apgib7ksrkkwac1d1mxkffijvr4bj1kypwdf0zlnzh24hhr";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
