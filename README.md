# Lispico Confetti

An experimental alternative frontend for [Lispico](https://github.com/unclechu/lispico),
using [Confetti](https://github.com/hgs3/confetti) for parsing. Confetti directives
are translated into Lispico expressions and evaluated by the existing Lispico
interpreter and standard library.

The initial implementation supports simple function calls, string and numeric
literals, and symbol references. Parenthesized infix expressions, blocks, and
conditional syntax are planned but not implemented yet.

See [examples](examples), they are also used as regression tests.

## Usage examples

Locally make sure you have Lispico and Confetti Git submodules initialized:

```sh
git submodule update --init --recursive
```

Build and run the first example:

```sh
./build.sh
./lispico-confetti examples/hello.confetti
```

Or build with Nix:

```sh
nix-build -A lispico-confetti
./result/bin/lispico-confetti examples/hello.confetti
```

Currently, simple directives map directly to Lispico function calls. For
example, `println "Hello"` corresponds to `(println "Hello")`, while
`var "name" "World"` corresponds to `(var "name" "World")`. Quoted arguments
remain string literals; unquoted names become symbol references.

## Local Vim configuration example

```vim
se cc=81,101 tw=100

" Prevent *.h files from being detected as C++
augroup force_c_headers
  autocmd!
  autocmd BufReadPost,BufNewFile *.h setlocal filetype=c
augroup END

" Use Nginx syntax highlighting for Confetti files
augroup force_confetti_syntax_hl
  autocmd!
  autocmd BufReadPost,BufNewFile *.confetti setlocal filetype=nginx
augroup END

let g:ale_linters_explicit = 1
let g:ale_linters = {'c': ['cc'], 'sh': ['shellcheck']}
let g:ale_c_cc_executable = 'gcc'
let g:ale_c_cc_options = '-std=c23 -Wall -Wextra -Wpedantic -Wno-pragma-once-outside-header'

" GCC checker that uses the source file's directory as its working directory.
" For correct resolution of included headers with relative paths.
call ale#linter#Define('c', {
\ 'name': 'lispico_confetti_cc',
\ 'output_stream': 'stderr',
\ 'executable': function('ale_linters#c#cc#GetExecutable'),
\ 'command': {b -> ale#c#RunMakeCommand(b, function('ale_linters#c#cc#GetCommand'))},
\ 'cwd': {b -> fnamemodify(bufname(b), ':p:h')},
\ 'callback': 'ale#handlers#gcc#HandleGCCFormatWithIncludes',
\})

" Use this checker only for Lispico Confetti's own C sources
augroup lispico_confetti_ale
  autocmd!
  autocmd BufReadPost,BufNewFile */lispico-confetti/src/*.c
    \ let b:ale_linters = ['lispico_confetti_cc']
augroup END
```

## License

SPDX: MIT

All files in this repository (apart from third-party dependencies) are
licensed under [MIT](LICENSE).
