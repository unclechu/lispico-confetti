{ pkgs ? import <nixpkgs> {}
, lib ? pkgs.lib
, stdenv ? pkgs.stdenv

, inNixShell ? false

# Run first before enabling:
#   git submodule update --init --recursive
, useLocalSubmodules ? false

, lispicoSrc ? import nix/lispico-pin.nix
, confettiSrc ? import nix/confetti-pin.nix
}:

let
  projectName = "lispico-confetti";

  shell = pkgs.mkShell {
    pname = "${projectName}-shell";

    packages = [
      pkgs.gcc
      pkgs.shellcheck
    ];
  };

  lispico-confetti = stdenv.mkDerivation (lib.fix (self: {
    pname = projectName;
    meta.mainProgram = self.pname;
    version = "0.0.0";

    # Note that `lispico` and `confetti` Git submodules must be initialized
    # when `useLocalSubmodules` is `true`!
    # Run first:
    #   git submodule update --init --recursive
    src = pkgs.nix-gitignore.gitignoreFilterRecursiveSource (path: type:
      (type == "directory" && builtins.baseNameOf path == "src") ||
      (useLocalSubmodules && type == "directory" && builtins.baseNameOf path == "lispico") ||
      (useLocalSubmodules && type == "directory" && builtins.baseNameOf path == "confetti") ||
      (type == "directory" && builtins.baseNameOf path == "generated") ||
      (type == "regular" && (lib.hasSuffix ".c" path || lib.hasSuffix ".h" path))
    ) [] ./.;

    # Copy fetched dependencies into the source tree.
    # Local submodules are already included in `src`, so skip copying them.
    prePatch = lib.optionalString (!useLocalSubmodules) ''
      mkdir -p lispico confetti
      cp -R -- ${lib.escapeShellArg "${lispicoSrc}"}/src/ lispico/
      cp -R -- ${lib.escapeShellArg "${confettiSrc}"}/confetti.{h,c} confetti/
    '';

    dontConfigure = true;

    nativeBuildInputs = [
      # No extra dependencies needed
    ];

    buildInputs = [
      # No extra dependencies needed
    ];

    buildPhase = ''
      runHook preBuild
      (
        set -- # Clear "$@" in this subshell
        MAIN_SOURCE_FILE=interpreter.c
        OUTPUT_EXECUTABLE_NAME=${lib.escapeShellArg self.pname}
        ${builtins.readFile ./build.sh}
      )
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      install -Dm755 \
        ${lib.escapeShellArg self.pname} \
        "$out/bin/"${lib.escapeShellArg self.pname}
      runHook postInstall
    '';
  }));
in

(if inNixShell then shell else {}) // {
  inherit shell lispico-confetti;
}
