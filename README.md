# Lispico Confetti

TODO: Write README

See [examples](examples), they are also used as regression tests.

## Usage examples

Locally make sure you have Lispico and Confetti Git submodules initialized:

```sh
git submodule update --init --recursive
```

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
