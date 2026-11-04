# Lispico Confetti

An experimental alternative frontend for [Lispico](https://github.com/unclechu/lispico),
using [Confetti](https://github.com/hgs3/confetti) for parsing. Confetti directives
are translated into Lispico expressions and evaluated by the existing Lispico
interpreter and standard library.

The initial implementation supports function calls, string and numeric literals,
symbol references, and blocks of nested directives. Blocks become Lispico `(do)`
expressions and can supply the final argument to a function call. Parenthesized
infix expressions and `if`/`else` syntax are planned but not implemented yet.

See [examples](examples), which also serve as regression tests.

## Usage examples

Locally make sure you have Lispico and Confetti Git submodules initialized:

```sh
git submodule update --init --recursive
```

Build and run the examples:

```sh
./build.sh
./lispico-confetti examples/hello.confetti
./lispico-confetti examples/blocks.confetti
./lispico-confetti examples/arrays.confetti
./tests/run-examples.sh
```

Or build with Nix:

```sh
nix-build -A lispico-confetti
./result/bin/lispico-confetti examples/hello.confetti
```

## Language examples

The examples below show how Confetti directives map to Lispico expressions.
**Implemented** examples run with the current translator. **Planned** examples
describe the intended syntax; the translator does not accept them yet.

### Implemented: Calls, literals, and symbol references

A directive names a function followed by its arguments. Quoted words are
strings, while unquoted words are Lispico symbol references; numbers are
parsed using Lispico's numeric literal rules.

Confetti:

```confetti
var "name" "World"
println name
println "name"
```

Lispico:

```lisp
(var "name" "World")
(println name)
(println "name")
```

### Implemented: An expression as the last argument

Curly braces group nested directives. The last directive in a block supplies
the block's value. The translator wraps a block in `(do ...)`, making it one
argument to the surrounding function call.

Confetti:

```confetti
var "answer" {
  + 1 2
}

assert {
  == answer 3
}
```

Lispico:

```lisp
(var "answer" (do (+ 1 2)))
(assert (do (== answer 3)))
```

### Implemented: Sequences and nested blocks

Multiple directives inside a block are evaluated in order, and the last value
becomes the result. A standalone `do` block maps directly to one Lispico `do`
call, without adding an extra nested `do` scope.

Confetti:

```confetti
var "answer" {
  var "first" 10
  + first 5
}

do {
  println "The result is:"
  println answer
}
```

Lispico:

```lisp
(var "answer" (do (var "first" 10) (+ first 5)))
(do (println "The result is:") (println answer))
```

### Implemented: Arrays

Functions returning array values work as block results. Other array operations
are ordinary directives. This uses Lispico's array support unchanged.

Confetti:

```confetti
var "numbers" {
  arr 10 20 30
}
var "second" {
  arr-get "numbers" 1
}
assert {
  == second 20
}
```

Lispico:

```lisp
(var "numbers" (do (arr 10 20 30)))
(var "second" (do (arr-get "numbers" 1)))
(assert (do (== second 20)))
```

### Planned: Infix operators and parenthesized expressions

Symbolic binary functions such as `+`, `*`, `==`, and `<=` should be usable in
infix position. Parentheses should group subexpressions with well-defined
operator precedence, while ordinary directives should remain available.

Confetti (not yet supported):

```confetti
var "answer" (1 + (2 * 3))
assert (answer == 7)
```

Lispico:

```lisp
(var "answer" (+ 1 (* 2 3)))
(assert (== answer 7))
```

### Planned: Nested calls inside infix expressions

Confetti (not yet supported):

```confetti
var "numbers" {
  arr 10 20 30
}
assert ((arr-get "numbers" 1) == 20)
```

Lispico:

```lisp
(var "numbers" (arr 10 20 30))
(assert (== (arr-get "numbers" 1) 20))
```

### Planned: `if`, `else if`, and `else`

The intended conditional syntax should translate into Lispico's existing
`if` function with nested expressions. A branch's `{ ... }` block should be
one expression with `do` semantics.

Confetti (not yet supported):

```confetti
if (5 > 2) {
  println "bigger"
} else if (5 < 2) {
  println "smaller"
} else {
  println "equal"
}
```

Lispico:

```lisp
(if (> 5 2)
  (do (println "bigger"))
  (if (< 5 2)
    (do (println "smaller"))
    (do (println "equal"))))
```

## Implementation roadmap

- [x] Translate basic Confetti directives into Lispico function calls.
- [x] Preserve quoted strings, numbers, and unquoted symbol references.
- [x] Translate nested directive blocks into expression-valued arguments.
- [x] Support direct `do { ... }` blocks without an extra scope.
- [ ] Parse parenthesized expressions, nested function calls, and infix operators
      with precedence and associativity.
- [ ] Translate conditional `if` / `else if` / `else` chains.
- [ ] Define and test how Confetti's string escaping maps to Lispico strings,
      including `\n`, `\0`, and embedded NUL bytes.
- [ ] Expand working examples and failure tests to cover the new syntax.
- [ ] Port the original Lispico examples to `.confetti` equivalents.

The current implementation deliberately reports an error for parenthesized
expression arguments rather than silently interpreting them as plain strings.

## Development environment

### [direnv](https://direnv.net) configuration example

Create `.envrc` file:

```sh
use nix --arg useLocalSubmodules true
```

### Local Vim configuration example

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
