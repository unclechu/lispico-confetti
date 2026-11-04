{ pkgs ? import <nixpkgs> {}
, lib ? pkgs.lib
, stdenv ? pkgs.stdenv

, inNixShell ? false
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

    # Note that `lispico` and `confetti` Git submodules must be initialized!
    # Run first: git submodule update --init --recursive
    src = pkgs.nix-gitignore.gitignoreFilterRecursiveSource (path: type:
      (type == "directory" && builtins.baseNameOf path == "src") ||
      (type == "directory" && builtins.baseNameOf path == "lispico") ||
      (type == "directory" && builtins.baseNameOf path == "confetti") ||
      (type == "directory" && builtins.baseNameOf path == "generated") ||
      (type == "regular" && (lib.hasSuffix ".c" path || lib.hasSuffix ".h" path))
    ) [] ./.;

    dontConfigure = true;

    nativeBuildInputs = [
      # …
    ];

    buildInputs = [
      # …
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
