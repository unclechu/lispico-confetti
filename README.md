# Lispico Confetti

An experimental alternative frontend for
[Lispico](https://github.com/unclechu/lispico),
using [Confetti](https://github.com/hgs3/confetti) for parsing. Confetti’s
native directive and subdirective AST is translated into Lispico expressions and
evaluated by the original interpreter and standard library.

This frontend uses **Confetti’s native `{ … }` blocks exclusively** for nested
expressions. It does not enable Confetti’s parenthesized-expression extension,
and it does not tokenize or parse expression strings independently.

See [examples](examples) for runnable programs and
[examples/fail](examples/fail) for expected translation failures.

## Relationship to Lispico

Lispico Confetti is an alternative **frontend**, not another implementation
of Lispico’s runtime. Its standard library, value types, error behavior,
variable and array semantics, allocation limits, and memory management
(including manual garbage collection) come from Lispico itself.

**[Lispico](https://github.com/unclechu/lispico) is still in active
development.** Its features and behavior will continue to evolve. When this
project updates its pinned Lispico dependency, new runtime capabilities will
become available here too, although some may need additional translation rules.

For the complete feature set and runtime documentation, consult
**[Lispico’s README](https://github.com/unclechu/lispico#readme)**. This README
concentrates on expressing Lispico programs using Confetti syntax.

## Build and run

For a local build, initialize the Git submodules first:

```sh
git submodule update --init --recursive
./build.sh
./lispico-confetti examples/hello.confetti
./lispico-confetti examples/blocks.confetti
./lispico-confetti examples/arrays.confetti
./lispico-confetti examples/infix.confetti
./lispico-confetti examples/prefix.confetti
./lispico-confetti examples/strings.confetti
./tests/run-examples.sh
```

Or use Nix with pinned Lispico and Confetti sources:

```sh
nix-build -A lispico-confetti
./result/bin/lispico-confetti examples/hello.confetti
```

To use locally initialized submodules instead of the pins:

```sh
nix-build -A lispico-confetti --arg useLocalSubmodules true
```

## Translation rules

Confetti supplies the complete structural AST. Lispico Confetti interprets
directives and their arguments as Lispico expressions:

| Confetti                        | Lispico              | Meaning                                       |
| ---                             | ---                  | ---                                           |
| `message`                       | `(message)`          | Call a function without arguments             |
| `value message`                 | `message`            | Use a symbol’s value                          |
| `value 123`                     | `123`                | Use a literal value                           |
| `println message`               | `(println message)`  | Call a function with an argument              |
| `var "x" {gc}`                  | `(var "x" (gc))`     | A block supplies a call argument              |
| `str {value "a"; value "b"}`    | `(str "a" "b")`      | Block subdirectives supply separate arguments |
| `str {}`                        | `(str)`              | Empty block supplies no arguments             |
| `str do {value "a"; value "b"}` | `(str (do "a" "b"))` | Explicit sequence is one argument             |
| `value {gc}`                    | `(gc)`               | Pass the result of a nested call through      |
| `1 + 2`                         | `(+ 1 2)`            | Infix operator directive                      |
| `+ 1 2`                         | `(+ 1 2)`            | Ordinary prefix operator directive            |
| `1 + 2 + 3`                     | `(+ 1 2 3)`          | Chain the same arithmetic operator            |
| `do { … }`                      | `(do …)`             | Explicit sequence                             |

Quoted arguments represent string literals, while bare names represent
symbol references. Numeric arguments use Lispico’s existing numeric parser.
For example:

```confetti
var "name" "World"
println name
println "name"
```

```lisp
(var "name" "World")
(println name)
(println "name")
```

### Native argument blocks and infix operators

An ordinary block supplies **zero or more arguments** to the enclosing
function, one for each logical expression inside it. The block is not
implicitly grouped into a `(do)`. For example:

```confetti
var "joined" {str {str {value "Hello"}; str {value " world"}}}
assert {joined == "Hello world"}

var "shorter" {str {value "Hello"; value " world"}}
assert {shorter == joined}

var "empty" {str {}}
assert {empty == ""}
```

```lisp
(var "joined" (str (str "Hello") (str " world")))
(assert (== joined "Hello world"))
(var "shorter" (str "Hello" " world"))
(assert (== shorter joined))
(var "empty" (str))
(assert (== empty ""))
```

Literals and variable references inside argument blocks still use `value`.
For example, `str {value "Hello"}` is supported, but `str {"Hello"}`
does not implicitly convert its quoted-only subdirective into a value.
Likewise, `value message` passes the variable’s contents, while
`message` is a zero-argument function call.

The preferred operator syntax is infix:

```confetti
var "answer" {1 + {2 * 3}}
assert {answer == 7}
var "difference" {20 - 5 - 3}
assert {difference == 12}
```

```lisp
(var "answer" (+ 1 (* 2 3)))
(assert (== answer 7))
(var "difference" (- 20 5 3))
(assert (== difference 12))
```

There is **no operator precedence**. Each directive may use only one kind of
infix operator. Parent-child relationships in Confetti’s AST express nesting:

```confetti
# Accepted: explicit grouping
var "number" {1 + {2 * 3}}

# Rejected: mixed operators without grouping
# var "number" {1 + 2 * 3}
```

Arithmetic `+`, `-`, `*`, and `/` can be chained left to right. Comparisons
`<`, `<=`, `>`, `>=`, `==`, and `!=` accept two operands only. A long arithmetic
chain is split into left-associated Lispico calls when it exceeds Lispico’s
native argument capacity.

Infix directives work at the top level too, including with Lispico’s
`last-val` function:

```confetti
1 + 2
last-val
```

This returns `TYPE_U32(3)`. The regression examples also cover top-level
`value {1 + 2}`, composed `value` expressions, and long arithmetic chains.

Prefix calls are still supported: they are just ordinary Confetti directives.
The [prefix example](examples/prefix.confetti) demonstrates both forms:

```confetti
var "two" {+ 1 2}
var "six" {+ 1 2 3}
```

```lisp
(var "two" (+ 1 2))
(var "six" (+ 1 2 3))
```

### `value` and neighboring operator directives

Confetti allows a new directive immediately after another directive’s closing
brace. The translator can therefore combine a `value` directive with its
following operator directives inside the same native block:

```confetti
var "numbers" {arr 10 20 30}
assert {value {arr-get "numbers" 1} == 20}
```

```lisp
(var "numbers" (arr 10 20 30))
(assert (== (arr-get "numbers" 1) 20))
```

Confetti provides neighboring subdirectives, not an opaque expression string:

```text
assert
├── value
│   └── arr-get "numbers" 1
└── == 20
```

Here `value {arr-get "numbers" 1}` provides the left operand, and `== 20`
continues the expression. Subsequent operator continuations must use the
same operator. This is an interpretation of Confetti’s native AST; no extra
lexical scanning is involved.

One logical argument can span multiple directives, while a subsequent
`value` starts a new argument:

```confetti
assert {
  value {str-size "Hello"} == 5
  value "Expected exactly five bytes"
}
```

```lisp
(assert (== (str-size "Hello") 5) "Expected exactly five bytes")
```

Nested calls may also appear as the **last** operand without `value`:

```confetti
assert {32 == {2 + {arr-get "numbers" 2}}}
```

```lisp
(assert (== 32 (+ 2 (arr-get "numbers" 2))))
```

An anonymous `{ … }` cannot appear at the start of a Confetti directive.
Consequently, `assert {{arr-get "numbers" 1} == 20}` is not valid Confetti;
use the explicit `value` form above.

### Sequences and expression values

`do` creates a sequence, evaluating its expressions in order and returning the
last result. It also establishes Lispico’s existing sequence control-flow
boundary. An ordinary function block expands its expressions as separate
arguments; **explicit `do` is required to pass a sequence as one argument**.

```confetti
var "answer" do {
  var "first" 10
  first + 5
}

do {
  println "Computed a result"
  value answer
}
```

```lisp
(var "answer" (do (var "first" 10) (+ first 5)))
(do (println "Computed a result") answer)
```

`value answer` is important when the final sequence expression should be a
symbol reference rather than a zero-argument function call. An explicit
`value {call}` also works when the expression is a nested call.

An empty ordinary block supplies no arguments; an explicit empty sequence
supplies one argument if passed to another function:

```confetti
str {}    # Equivalent to (str)
str do {} # Equivalent to (str (do))
do {}     # Equivalent to (do)
```

Conditional branches are intentionally different: an `if` branch still
requires exactly one expression, or an explicit `do` for an empty or
multi-expression sequence. This prevents a second expression from becoming
an accidental false branch.

### Strings, escapes, and NUL bytes

Confetti first processes its own escaping (a backslash escapes the following
character). Lispico Confetti then decodes surviving backslash sequences using
Lispico’s string literal rules. This applies to ordinary and triple-quoted
arguments, including inside native `{ … }` blocks.

For instance, two backslashes in Confetti source preserve one backslash for
the second stage:

```confetti
var "message" "hello\\nworld"
var "binary" "A\\0B"
var "literal" "hello\\\\nworld"
```

```lisp
(var "message" "hello\nworld")
(var "binary" "A\0B")
(var "literal" "hello\\nworld")
```

| Confetti source | Result                            |
| ---             | ---                               |
| `\\n`           | Newline (LF)                      |
| `\\r`           | Carriage return (CR)              |
| `\\t`           | Tab                               |
| `\\0`           | NUL byte                          |
| `\\\\`          | Literal backslash                 |
| `\\\\n`         | Literal backslash followed by `n` |
| `\\\\0`         | Literal backslash followed by `0` |

Confetti handles ordinary escaped quotes such as `\"`. The second decoder
also recognizes Lispico’s quote escape when it survives Confetti’s first
pass. Unknown or incomplete surviving escapes cause translation errors.

For example, Confetti decodes `"\n"` to just `"n"`, which is **not** a
newline. Conversely, `"\\q"` fails during Lispico decoding, while
`"\\\\q"` represents a literal backslash followed by `q`.

Triple-quoted strings contain real line breaks and undergo the same second
decoding stage. Indentation inside the quotes is part of their value:

```confetti
var "message" """First line
Second line"""
assert {value {str-get "message" 10} == 10}
```

Lispico strings store explicit byte lengths, so an embedded NUL does not
truncate the value. A final terminator is stored separately.

### Conditionals

The translator supports `if`, `else if`, and `else`. Each branch is evaluated
only when selected, preserving Lispico’s ordinary lazy `if` behavior.
Conditions may be single values or infix comparisons:

```confetti
if 5 > 2 {
  println "bigger"
} else if 5 < 2 {
  println "smaller"
} else {
  println "equal"
}
```

Equivalent Lispico:

```lisp
(if (> 5 2)
  (println "bigger")
  (if (< 5 2)
    (println "smaller")
    (println "equal")))
```

`then` is optional when the condition fits in the directive’s argument list:

```confetti
if enabled {
  println "Enabled"
}
if enabled then {
  println "Enabled"
}
```

However, **Confetti permits only one trailing block per directive**.
If the condition itself needs a block, the branch must be an adjacent
`then` or `do` directive:

```confetti
if {5 > 2} then {
  println "bigger"
} else if {5 < 2} then {
  println "smaller"
} else {
  println "equal"
}
```

This is still entirely native Confetti AST: `if {5 > 2}` contains the condition
block, and the next sibling `then { … }` supplies its branch. No embedded
expression strings are re-parsed. Writing `if {condition} {branch}` directly
is invalid because the second block lacks its own directive.

A regular branch block must contain **exactly one expression**. Use `do` for
zero or multiple expressions; optionally combine it with `then`:

```confetti
if {enabled} then {
  println "Enabled"
} else do {
  println "Disabled"
  println "Try enabling it"
}

if enabled then do {
  println "First"
  println "Second"
}

if enabled do {}
```

`do` wraps its contents in Lispico `(do …)`. For example, the first conditional
above corresponds to:

```lisp
(if (enabled)
  (println "Enabled")
  (do
    (println "Disabled")
    (println "Try enabling it")))
```

To read the value of the variable `enabled`, use `if enabled` rather than
`if {enabled}`: the braces make `{enabled}` a zero-argument function call,
equivalent to Lispico `(enabled)`. Inside a condition block, use
`{value enabled}` if a variable reference must be explicitly grouped.

See `examples/conditionals.confetti` and `examples/conditionals-root.confetti`
for executable examples of both condition forms, sequences, and chains.

## Implementation roadmap

- [x] Translate Confetti directives into Lispico function calls and symbol references.
- [x] Preserve string and numeric literals, including Lispico string escapes.
- [x] Expand ordinary native `{ … }` blocks into function arguments.
- [x] Implement explicit `do { … }` sequences, including empty sequences.
- [x] Implement `value` for passing through atoms or nested expression results.
- [x] Translate homogeneous infix operators, including variadic arithmetic chains.
- [x] Support prefix operator calls as ordinary directives.
- [x] Compose `value` with neighboring operator continuation directives.
- [x] Disable Confetti’s parenthesized-expression extension and remove its custom parser.
- [x] Reject mixed operators, malformed continuations, and invalid conditional/value blocks.
- [x] Translate `if` / `else if` / `else` chains using only native directives.
- [ ] Translate chainable `&&` and `||` operators into Lispico’s `(and)` and `(or)` special forms.
- [ ] Expand failure coverage and port more original Lispico examples to `.confetti`.

## Development environment

### [direnv](https://direnv.net) example

Create `.envrc`:

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

" GCC checker that uses the source file’s directory as its working directory.
" For correct resolution of included headers with relative paths.
call ale#linter#Define('c', {
\ 'name': 'lispico_confetti_cc',
\ 'output_stream': 'stderr',
\ 'executable': function('ale_linters#c#cc#GetExecutable'),
\ 'command': {b -> ale#c#RunMakeCommand(b, function('ale_linters#c#cc#GetCommand'))},
\ 'cwd': {b -> fnamemodify(bufname(b), ':p:h')},
\ 'callback': 'ale#handlers#gcc#HandleGCCFormatWithIncludes',
\})

" Use this checker only for Lispico Confetti’s own C sources.
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
