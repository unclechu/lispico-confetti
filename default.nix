{ pkgs ? import <nixpkgs> {}
, lib ? pkgs.lib
, stdenv ? pkgs.stdenv
, callPackage ? pkgs.callPackage
, runCommand ? pkgs.runCommand
, nix-gitignore ? pkgs.nix-gitignore
, makeBinaryWrapper ? pkgs.makeBinaryWrapper
, mkShell ? pkgs.mkShell

, gcc ? pkgs.gcc
, shellcheck ? pkgs.shellcheck
, time ? pkgs.time
, gawk ? pkgs.gawk

, inNixShell ? false

# Run first before enabling:
#   git submodule update --init --recursive
, useLocalSubmodules ? false

, lispicoSrc ? import nix/lispico-pin.nix
, confettiSrc ? import nix/confetti-pin.nix
, sublimeTextConfettiSrc ? import nix/sublime-text-confetti.nix

, sublime-text-confetti ? callPackage sublimeTextConfettiSrc {}
, bat-with-confetti ? sublime-text-confetti.bat-with-confetti

, useBundledBat ? true
}:

let
  projectName = "lispico-confetti";

  shell = mkShell {
    pname = "${projectName}-shell";

    packages =
      [
        gcc
        shellcheck

        # For testing scripts
        time # For memory footprint measurements and evaluation time
        gawk
      ]
      # Fox syntax highlighting in source location excerpts
      ++ lib.optional useBundledBat bat-with-confetti;
  };

  lispico-confetti = stdenv.mkDerivation (lib.fix (self: {
    pname = projectName;
    version = "0.0.0";

    meta = {
      description = "An alternative frontend for Lispico using the Confetti syntax and parser";
      homepage = "https://github.com/unclechu/lispico-confetti";
      license = lib.licenses.mit;
      mainProgram = self.pname;
      platforms = lib.platforms.unix;
    };

    # Note that `lispico` and `confetti` Git submodules must be initialized
    # when `useLocalSubmodules` is `true`!
    # Run first:
    #   git submodule update --init --recursive
    src = nix-gitignore.gitignoreFilterRecursiveSource (path: type:
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

    nativeBuildInputs = lib.optional useBundledBat makeBinaryWrapper;

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

    postFixup = lib.optionalString useBundledBat ''
      wrapProgram "$out/bin/"${lib.escapeShellArg self.pname} \
        --prefix PATH : ${lib.escapeShellArg (lib.makeBinPath [
          bat-with-confetti
        ])}
    '';
  }));

  lispico-src =
    runCommand "${projectName}-lispico-src" {} ''
      mkdir -- "$out"
      cp -R -- ${lib.escapeShellArg "${lispicoSrc}"}/src/ "$out"/
    '';

  confetti-src =
    runCommand "${projectName}-confetti-src" {} ''
      mkdir -- "$out"
      cp -R -- ${lib.escapeShellArg "${confettiSrc}"}/confetti.{h,c} "$out"/
    '';
in

(if inNixShell then shell else {}) // {
  inherit
    shell
    lispico-confetti
    lispico-src
    confetti-src
    ;
}
