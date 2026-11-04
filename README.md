# Lispico Confetti

An experimental alternative frontend for [Lispico](https://github.com/unclechu/lispico),
using [Confetti](https://github.com/hgs3/confetti) for parsing. Confetti directives
are translated into Lispico expressions and evaluated by the existing Lispico
interpreter and standard library.

The translator supports function calls, string and numeric literals, symbol
references, nested directives, and parenthesized expressions with infix
operators. A plain `{ … }` block supplies one expression as the final argument;
an explicit `do { … }` block creates a Lispico `(do)` sequence. Conditional
`else if` / `else` syntax is planned but not implemented yet.

See [examples](examples), which also serve as regression tests. Expected
translation failures are in [examples/fail](examples/fail).

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
./lispico-confetti examples/infix.confetti
./tests/run-examples.sh
```

Or build with Nix:

```sh
nix-build -A lispico-confetti
./result/bin/lispico-confetti examples/hello.confetti
```

In this case the Lispico and Confetti dependencies would be pulled using the Nix
pins. If you want to use submodules from your local copy of the repository then
just add `--arg useLocalSubmodules true`:

```sh
nix-build -A lispico-confetti --arg useLocalSubmodules true
./result/bin/lispico-confetti examples/hello.confetti
```

## Language examples

The examples below show how Confetti directives map to Lispico expressions.
**Implemented** examples run with the current translator. **Planned** examples
describe the intended syntax; the translator does not accept them yet.

### Implemented: Calls, literals, and symbol references

A directive names a function followed by its arguments. Quoted words are
strings, while unquoted words are Lispico symbol references; numbers are
parsed using Lispico’s numeric literal rules.

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

Plain curly braces group exactly one nested directive, which supplies one
argument to the surrounding function call without introducing a `(do)` scope.

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
(var "answer" (+ 1 2))
(assert (== answer 3))
```

### Implemented: Sequences and nested blocks

Use an explicit `do` marker to group multiple directives. They are evaluated
in order, and the last value becomes the result. A standalone `do` block maps
directly to one Lispico `do` call, without adding another nested `(do)` scope.
An explicit `do` also introduces a sequence when the block contains zero or
one directive.

Confetti:

```confetti
var "answer" do {
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

The following blocks are rejected during translation:

```confetti
var "empty" {} # Use `do {}` for an empty sequence
var "answer" { # Use `do { … }` for multiple expressions
  + 1 2
  + 3 4
}
```

This makes the control-flow boundary explicit: adding another directive to a
plain block cannot silently change the behavior of `(depth)`, `(leave)`,
`(defer)`, or `(goto)`.

### Implemented: Arrays

Functions returning array values work as block results. Other array operations
are ordinary directives. This uses Lispico’s array support unchanged.

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
(var "numbers" (arr 10 20 30))
(var "second" (arr-get "numbers" 1))
(assert (== second 20))
```

### Implemented: Infix operators and parenthesized expressions

Symbolic binary functions can be used in infix position inside parentheses.
The translator handles `*`, `/`, `+`, `-`, `<`, `<=`, `>`, `>=`, `==`, and `!=`.
Multiplication and division bind more tightly than addition and subtraction;
comparisons follow arithmetic, and equality follows comparisons. Operators
with equal precedence associate from left to right. Consecutive uses of the
same arithmetic operator (`+`, `-`, `*`, or `/`) become one variadic Lispico
call. Comparisons and equality remain binary calls. Lispico’s own functions
perform the actual calculations and type validation.

Confetti:

```confetti
var "answer" (1 + (2 * 3))
assert (answer == 7)
var "difference" (20 - 5 - 3)
assert (difference == 12)
```

Lispico:

```lisp
(var "answer" (+ 1 (* 2 3)))
(assert (== answer 7))
(var "difference" (- 20 5 3))
(assert (== difference 12))
```

Different operators and explicitly parenthesized expressions preserve their
grouping. For example, `(20 - 5 + 3)` translates to `(+ (- 20 5) 3)`, while
`(20 - (5 - 3))` translates to `(- 20 (- 5 3))`. When a chain exceeds Lispico’s
native argument capacity, the translator starts another nested call to avoid
exceeding that limit.

Parentheses also group subexpressions without introducing a Lispico `(do)`
scope. A signed numeric literal such as `-2` retains Lispico’s signed type.
Keep spaces around subtraction when using names containing hyphens (for
example, `arr-get`), so a hyphen in a name is not mistaken for subtraction.

### Implemented: Nested calls inside infix expressions

The first name in a parenthesized expression can also introduce a Lispico
function call when it is followed by arguments. Symbolic function names can
be used in prefix position too, as in `(+ 1 2)`.

Confetti:

```confetti
var "numbers" {
  arr 10 20 30
}
assert ((arr-get "numbers" 1) == 20)
assert (((arr-get "numbers" 2) + 2) == 32)
```

Lispico:

```lisp
(var "numbers" (arr 10 20 30))
(assert (== (arr-get "numbers" 1) 20))
(assert (== (+ (arr-get "numbers" 2) 2) 32))
```

An expression containing only one bare name, such as `(answer)`, groups that
symbol reference. It does not call a zero-argument function. For an ordinary
zero-argument function, use a Confetti directive instead. When an enclosing
function expects its result, place that directive in a single-expression block:

```confetti
var "collected" {
  gc
}
```

This translates to `(var "collected" (gc))`. Curly braces supply a nested
directive to the surrounding function; they do not replace the surrounding
function call.

### Planned: `if`, `else if`, and `else`

The intended conditional syntax should translate into Lispico’s existing
`if` function with nested expressions. A branch with one expression needs no
`(do)` wrapper; a branch containing a sequence must use `do { … }`.

An `if` directive with a parenthesized condition and a single-expression
branch already works. Chaining `else if` and `else` is still planned.

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
  (println "bigger")
  (if (< 5 2)
    (println "smaller")
    (println "equal")))
```

To execute more than one expression in a branch, explicitly introduce a
sequence (`else` syntax is still planned):

```confetti
if (5 > 2) do {
  println "bigger"
  println "definitely bigger"
} else {
  println "not bigger"
}
```

Lispico:

```lisp
(if (> 5 2)
  (do
    (println "bigger")
    (println "definitely bigger"))
  (println "not bigger"))
```

## Implementation roadmap

- [x] Translate basic Confetti directives into Lispico function calls.
- [x] Preserve quoted strings, numbers, and unquoted symbol references.
- [x] Translate single-expression `{ … }` blocks into expression-valued arguments.
- [x] Support explicit `do { … }` sequences without an extra scope.
- [x] Reject empty or multi-expression plain blocks with a suggestion to use `do`.
- [x] Parse parenthesized expressions, nested function calls, and infix operators
      with precedence and associativity.
- [x] Flatten consecutive arithmetic operators into variadic Lispico calls,
      retaining parentheses and native argument limits.
- [ ] Translate conditional `if` / `else if` / `else` chains.
- [ ] Define and test how Confetti’s string escaping maps to Lispico strings,
      including `\n`, `\0`, and embedded NUL bytes.
- [ ] Expand working examples and failure tests to cover the new syntax.
- [ ] Port the original Lispico examples to `.confetti` equivalents.

Parenthesized expressions have their own syntax and are never silently
interpreted as plain strings. The translator reports errors for empty
expressions, incomplete operators, and unsupported operators.

## Development environment

### [direnv](https://direnv.net) configuration example

Create `.envrc` file:

```sh
use nix --arg useLocalSubmodules true
```

Make sure you have submodules fetched:

```sh
git submodule update --init --recursive
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

" Use this checker only for Lispico Confetti’s own C sources
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
