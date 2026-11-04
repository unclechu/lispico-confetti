# Lispico Confetti

**WORK IN PROGRESS: Not ready yet.**

Lispico Confetti is an experimental alternative frontend for
[Lispico](https://github.com/unclechu/lispico), using
[Confetti](https://github.com/hgs3/confetti) as its parser.

The goal is to explore an alternative, more conventional-looking syntax for
Lispico programs while preserving the original language's evaluation semantics
and runtime functionality.

Instead of implementing another interpreter, Lispico Confetti uses Confetti
to parse source code into its syntax tree, then translates that tree into
Lispico expressions that can be executed by the existing Lispico interpreter.

The project aims to provide:

- A more readable, directive-oriented syntax without Lisp-style parentheses
  around every function call.
- Support for infix operators, nested expressions, and block-oriented control
  flow.
- Access to Lispico's existing native functions, variables, arrays, and other
  runtime features.
- Compatibility with Lispico's evaluation semantics without duplicating the
  interpreter implementation.

Both Lispico and Confetti are included as Git submodules, allowing the
alternative frontend to be developed independently of either upstream project.

The project uses a simple GCC-based build system without requiring CMake.
Confetti's generated Unicode data is included in this repository, so building
does not require Python or downloading additional resources.

## Usage examples

Locally make sure you have Lispico and Confetti Git submodules initialized:

```sh
git submodule update --init --recursive
```

**TODO:** Provide more instructions.

## Local Vim configuration example

```vim
se cc=81,101 tw=100

" Prevent *.h files from being detected as C++
augroup force_c_headers
  autocmd!
  autocmd BufReadPost,BufNewFile *.h setlocal filetype=c
augroup END

" I prefer different treesitter syntax highlighting over default commonlisp one
augroup force_custom_lisp_syntax_hl
  autocmd!
  autocmd BufReadPost,BufNewFile *.lisp setlocal filetype=scheme
augroup END

let g:ale_linters_explicit = 1
let g:ale_linters = {'c': ['cc'], 'sh': ['shellcheck']}
let g:ale_c_cc_executable = 'gcc'
let g:ale_c_cc_options = '-std=c23 -Wall -Wextra -Wpedantic -Wno-pragma-once-outside-header'
```

## License

SPDX: MIT

All files in this repository (apart from third-party dependencies) are
licensed under [MIT](LICENSE).
