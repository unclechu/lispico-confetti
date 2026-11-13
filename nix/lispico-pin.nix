let
  commit = "d3faa3f430894fd2f030a07d896842f8555acd81";
  hash = "sha256:1s23y3qf95y8xlschwk1zlsxcl2s2d5j63xykymsyprddja04kcg";
in
builtins.fetchTarball {
  url = "https://github.com/unclechu/lispico/archive/${commit}.tar.gz";
  sha256 = hash;
}
